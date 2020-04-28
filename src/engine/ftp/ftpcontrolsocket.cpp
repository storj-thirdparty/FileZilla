#include <filezilla.h>

#include "cwd.h"
#include "chmod.h"
#include "delete.h"
#include "directorycache.h"
#include "directorylistingparser.h"
#include "engineprivate.h"
#include "externalipresolver.h"
#include "filetransfer.h"
#include "ftpcontrolsocket.h"
#include "iothread.h"
#include "list.h"
#include "logon.h"
#include "mkd.h"
#include "pathcache.h"
#include "proxy.h"
#include "rawcommand.h"
#include "rawtransfer.h"
#include "rename.h"
#include "rmd.h"
#include "servercapabilities.h"
#include "transfersocket.h"

#include <libfilezilla/file.hpp>
#include <libfilezilla/iputils.hpp>
#include <libfilezilla/local_filesys.hpp>
#include <libfilezilla/tls_layer.hpp>
#include <libfilezilla/util.hpp>

#include <algorithm>

CFtpControlSocket::CFtpControlSocket(CFileZillaEnginePrivate & engine)
	: CRealControlSocket(engine)
{
}

CFtpControlSocket::~CFtpControlSocket()
{
	remove_handler();

	DoClose();
}

void CFtpControlSocket::OnReceive()
{
	log(logmsg::debug_verbose, L"CFtpControlSocket::OnReceive()");

	for (;;) {
		int error;
		int read = active_layer_->read(m_receiveBuffer + m_bufferLen, RECVBUFFERSIZE - m_bufferLen, error);

		if (read < 0) {
			if (error != EAGAIN) {
				log(logmsg::error, _("Could not read from socket: %s"), fz::socket_error_description(error));
				if (GetCurrentCommandId() != Command::connect) {
					log(logmsg::error, _("Disconnected from server"));
				}
				DoClose();
			}
			return;
		}

		if (!read) {
			auto messageType = (GetCurrentCommandId() == Command::none) ? logmsg::status : logmsg::error;
			log(messageType, _("Connection closed by server"));
			DoClose();
			return;
		}

		SetActive(CFileZillaEngine::recv);

		char* start = m_receiveBuffer;
		m_bufferLen += read;

		for (int i = start - m_receiveBuffer; i < m_bufferLen; ++i) {
			char const& p = m_receiveBuffer[i];
			if (p == '\r' ||
				p == '\n' ||
				p == 0)
			{
				int len = i - (start - m_receiveBuffer);
				if (!len) {
					++start;
					continue;
				}

				std::wstring line = ConvToLocal(start, i - (start - m_receiveBuffer));
				start = m_receiveBuffer + i + 1;

				ParseLine(line);

				// Abort if connection got closed
				if (!currentServer_) {
					return;
				}
			}
		}
		memmove(m_receiveBuffer, start, m_bufferLen - (start - m_receiveBuffer));
		m_bufferLen -= (start -m_receiveBuffer);
		if (m_bufferLen > MAXLINELEN) {
			m_bufferLen = MAXLINELEN;
		}
	}
}

void CFtpControlSocket::ParseLine(std::wstring line)
{
	m_rtt.Stop();
	log_raw(logmsg::reply, line);
	SetAlive();

	if (!operations_.empty() && operations_.back()->opId == Command::connect) {
		auto & data = static_cast<CFtpLogonOpData &>(*operations_.back());
		if (data.waitChallenge) {
			std::wstring& challenge = data.challenge;
			if (!challenge.empty())
#ifdef FZ_WINDOWS
				challenge += L"\r\n";
#else
				challenge += L"\n";
#endif
			challenge += line;
		}
		else if (data.opState == LOGON_FEAT) {
			data.ParseFeat(line);
		}
		else if (data.opState == LOGON_WELCOME) {
			if (!data.gotFirstWelcomeLine) {
				if (fz::str_tolower_ascii(line).substr(0, 3) == L"ssh") {
					log(logmsg::error, _("Cannot establish FTP connection to an SFTP server. Please select proper protocol."));
					DoClose(FZ_REPLY_CRITICALERROR);
					return;
				}
				data.gotFirstWelcomeLine = true;
			}
		}
	}
	//Check for multi-line responses
	if (line.size() > 3) {
		if (!m_MultilineResponseCode.empty()) {
			if (line.substr(0, 4) == m_MultilineResponseCode) {
				// end of multi-line found
				m_MultilineResponseCode.clear();
				m_Response = line;
				ParseResponse();
				m_Response.clear();
				m_MultilineResponseLines.clear();
			}
			else {
				m_MultilineResponseLines.push_back(line);
			}
		}
		// start of new multi-line
		else if (line[3] == '-') {
			// DDD<SP> is the end of a multi-line response
			m_MultilineResponseCode = line.substr(0, 3) + L" ";
			m_MultilineResponseLines.push_back(line);
		}
		else {
			m_Response = line;
			ParseResponse();
			m_Response.clear();
		}
	}
}

void CFtpControlSocket::OnConnect()
{
	m_lastTypeBinary = -1;

	SetAlive();

	if (currentServer_.GetProtocol() == FTPS) {
		if (!tls_layer_) {
			log(logmsg::status, _("Connection established, initializing TLS..."));

			tls_layer_ = std::make_unique<fz::tls_layer>(event_loop_, this, *active_layer_, &engine_.GetContext().GetTlsSystemTrustStore(), logger_);
			active_layer_ = tls_layer_.get();

			if (!tls_layer_->client_handshake(this)) {
				DoClose();
			}

			return;
		}
		else {
			log(logmsg::status, _("TLS connection established, waiting for welcome message..."));
		}
	}
	else if ((currentServer_.GetProtocol() == FTPES || currentServer_.GetProtocol() == FTP) && tls_layer_) {
		log(logmsg::status, _("TLS connection established."));
		SendNextCommand();
		return;
	}
	else {
		log(logmsg::status, _("Connection established, waiting for welcome message..."));
	}
	m_pendingReplies = 1;
	m_repliesToSkip = 0;
}

void CFtpControlSocket::ParseResponse()
{
	if (m_Response.empty()) {
		log(logmsg::debug_warning, L"No reply in ParseResponse");
		return;
	}

	if (m_Response[0] != '1') {
		if (m_pendingReplies > 0) {
			m_pendingReplies--;
		}
		else {
			log(logmsg::debug_warning, L"Unexpected reply, no reply was pending.");
			return;
		}
	}

	if (m_repliesToSkip) {
		log(logmsg::debug_info, L"Skipping reply after cancelled operation or keepalive command.");
		if (m_Response[0] != '1') {
			--m_repliesToSkip;
		}

		if (!m_repliesToSkip) {
			SetWait(false);
			if (operations_.empty()) {
				StartKeepaliveTimer();
			}
			else if (!m_pendingReplies) {
				SendNextCommand();
			}
		}

		return;
	}

	if (operations_.empty()) {
		log(logmsg::debug_info, L"Skipping reply without active operation.");
		return;
	}

	auto & data = *operations_.back();
	log(logmsg::debug_verbose, L"%s::ParseResponse() in state %d", data.name_, data.opState);
	int res = data.ParseResponse();
	if (res == FZ_REPLY_OK) {
		ResetOperation(FZ_REPLY_OK);
	}
	else if (res == FZ_REPLY_CONTINUE) {
		SendNextCommand();
	}
	else if (res & FZ_REPLY_DISCONNECTED) {
		DoClose(res);
	}
	else if (res & FZ_REPLY_ERROR) {
		if (operations_.back()->opId == Command::connect) {
			DoClose(res | FZ_REPLY_DISCONNECTED);
		}
		else {
			ResetOperation(res);
		}
	}
}

int CFtpControlSocket::GetReplyCode() const
{
	if (m_Response.empty()) {
		return 0;
	}
	else if (m_Response[0] < '0' || m_Response[0] > '9') {
		return 0;
	}
	else {
		return m_Response[0] - '0';
	}
}

int CFtpControlSocket::SendCommand(std::wstring const& str, bool maskArgs, bool measureRTT)
{
	size_t pos;
	if (maskArgs && (pos = str.find(' ')) != std::wstring::npos) {
		std::wstring stars(str.size() - pos - 1, '*');
		log_raw(logmsg::command, str.substr(0, pos + 1) + stars);
	}
	else {
		log_raw(logmsg::command, str);
	}

	std::string buffer = ConvToServer(str);
	if (buffer.empty()) {
		log(logmsg::error, _("Failed to convert command to 8 bit charset"));
		return FZ_REPLY_ERROR;
	}
	buffer += "\r\n";
	bool res = CRealControlSocket::Send(buffer.c_str(), buffer.size());
	if (res) {
		++m_pendingReplies;
	}

	if (measureRTT) {
		m_rtt.Start();
	}

	return res ? FZ_REPLY_WOULDBLOCK : FZ_REPLY_ERROR;
}

void CFtpControlSocket::List(CServerPath const& path, std::wstring const& subDir, int flags)
{
	CServerPath newPath = currentPath_;
	if (!path.empty()) {
		newPath = path;
	}
	if (!newPath.ChangePath(subDir)) {
		newPath.clear();
	}

	if (newPath.empty()) {
		log(logmsg::status, _("Retrieving directory listing..."));
	}
	else {
		log(logmsg::status, _("Retrieving directory listing of \"%s\"..."), newPath.GetPath());
	}

	Push(std::make_unique<CFtpListOpData>(*this, path, subDir, flags));
}

int CFtpControlSocket::ResetOperation(int nErrorCode)
{
 	log(logmsg::debug_verbose, L"CFtpControlSocket::ResetOperation(%d)", nErrorCode);

	m_pTransferSocket.reset();
	m_pIPResolver.reset();

	m_repliesToSkip = m_pendingReplies;

	if (!operations_.empty() && operations_.back()->opId == Command::transfer) {
		auto & data = static_cast<CFtpFileTransferOpData &>(*operations_.back());
		if (data.tranferCommandSent) {
			if (data.transferEndReason == TransferEndReason::transfer_failure_critical) {
				nErrorCode |= FZ_REPLY_CRITICALERROR | FZ_REPLY_WRITEFAILED;
			}
			if (data.transferEndReason != TransferEndReason::transfer_command_failure_immediate || GetReplyCode() != 5) {
				data.transferInitiated_ = true;
			}
			else {
				if (nErrorCode == FZ_REPLY_ERROR) {
					nErrorCode |= FZ_REPLY_CRITICALERROR;
				}
			}
		}
		if (nErrorCode != FZ_REPLY_OK && data.download_ && !data.fileDidExist_) {
			data.ioThread_.reset();
			int64_t size;
			bool isLink;
			if (fz::local_filesys::get_file_info(fz::to_native(data.localFile_), isLink, &size, nullptr, nullptr) == fz::local_filesys::file && size == 0) {
				// Download failed and a new local file was created before, but
				// nothing has been written to it. Remove it again, so we don't
				// leave a bunch of empty files all over the place.
				log(logmsg::debug_verbose, L"Deleting empty file");
				fz::remove_file(fz::to_native(data.localFile_));
			}
		}
	}

	if (!operations_.empty() && operations_.back()->opId == PrivCommand::rawtransfer &&
		nErrorCode != FZ_REPLY_OK)
	{
		auto & data = static_cast<CFtpRawTransferOpData &>(*operations_.back());
		if (data.pOldData->transferEndReason == TransferEndReason::successful) {
			if ((nErrorCode & FZ_REPLY_TIMEOUT) == FZ_REPLY_TIMEOUT) {
				data.pOldData->transferEndReason = TransferEndReason::timeout;
			}
			else if (!data.pOldData->tranferCommandSent) {
				data.pOldData->transferEndReason = TransferEndReason::pre_transfer_command_failure;
			}
			else {
				data.pOldData->transferEndReason = TransferEndReason::failure;
			}
		}
	}

	m_lastCommandCompletionTime = fz::monotonic_clock::now();
	if (!operations_.empty() && !(nErrorCode & FZ_REPLY_DISCONNECTED)) {
		StartKeepaliveTimer();
	}
	else {
		stop_timer(m_idleTimer);
		m_idleTimer = 0;
	}

	return CControlSocket::ResetOperation(nErrorCode);
}

bool CFtpControlSocket::CanSendNextCommand()
{
	if (m_repliesToSkip) {
		log(logmsg::status, L"Waiting for replies to skip before sending next command...");
		return false;
	}

	return true;
}

void CFtpControlSocket::ChangeDir(CServerPath const& path, std::wstring const& subDir, bool link_discovery)
{
	auto pData = std::make_unique<CFtpChangeDirOpData>(*this);
	pData->path_ = path;
	pData->subDir_ = subDir;
	pData->link_discovery_ = link_discovery;

	if (!operations_.empty() && operations_.back()->opId == Command::transfer &&
		!static_cast<CFtpFileTransferOpData &>(*operations_.back()).download_)
	{
		pData->tryMkdOnFail_ = true;
		assert(subDir.empty());
	}

	Push(std::move(pData));
}

void CFtpControlSocket::FileTransfer(std::wstring const& localFile, CServerPath const& remotePath,
									std::wstring const& remoteFile, bool download,
									CFileTransferCommand::t_transferSettings const& transferSettings)
{
	log(logmsg::debug_verbose, L"CFtpControlSocket::FileTransfer()");

	auto pData = std::make_unique<CFtpFileTransferOpData>(*this, download, localFile, remoteFile, remotePath, transferSettings);
	pData->binary = transferSettings.binary;
	Push(std::move(pData));
}

void CFtpControlSocket::TransferEnd()
{
	log(logmsg::debug_verbose, L"CFtpControlSocket::TransferEnd()");

	// If m_pTransferSocket is zero, the message was sent by the previous command.
	// We can safely ignore it.
	// It does not cause problems, since before creating the next transfer socket, other
	// messages which were added to the queue later than this one will be processed first.
	if (operations_.empty() || !m_pTransferSocket || operations_.back()->opId != PrivCommand::rawtransfer) {
		log(logmsg::debug_verbose, L"Call to TransferEnd at unusual time, ignoring");
		return;
	}

	TransferEndReason reason = m_pTransferSocket->GetTransferEndreason();
	if (reason == TransferEndReason::none) {
		log(logmsg::debug_info, L"Call to TransferEnd at unusual time");
		return;
	}

	if (reason == TransferEndReason::successful) {
		SetAlive();
	}

	auto & data = static_cast<CFtpRawTransferOpData &>(*operations_.back());
	if (data.pOldData->transferEndReason == TransferEndReason::successful) {
		data.pOldData->transferEndReason = reason;
	}

	switch (data.opState)
	{
	case rawtransfer_transfer:
		data.opState = rawtransfer_waittransferpre;
		break;
	case rawtransfer_waitfinish:
		data.opState = rawtransfer_waittransfer;
		break;
	case rawtransfer_waitsocket:
		ResetOperation((reason == TransferEndReason::successful) ? FZ_REPLY_OK : FZ_REPLY_ERROR);
		break;
	default:
		log(logmsg::debug_info, L"TransferEnd at unusual op state %d, ignoring", data.opState);
		break;
	}
}

bool CFtpControlSocket::SetAsyncRequestReply(CAsyncRequestNotification *pNotification)
{
	log(logmsg::debug_verbose, L"CFtpControlSocket::SetAsyncRequestReply");

	const RequestId requestId = pNotification->GetRequestID();
	switch (requestId)
	{
	case reqId_fileexists:
		{
			if (operations_.empty() || operations_.back()->opId != Command::transfer) {
				log(logmsg::debug_info, L"No or invalid operation in progress, ignoring request reply %d", pNotification->GetRequestID());
				return false;
			}

			CFileExistsNotification *pFileExistsNotification = static_cast<CFileExistsNotification *>(pNotification);
			return SetFileExistsAction(pFileExistsNotification);
		}
		break;
	case reqId_interactiveLogin:
		{
			if (operations_.empty() || operations_.back()->opId != Command::connect) {
				log(logmsg::debug_info, L"No or invalid operation in progress, ignoring request reply %d", pNotification->GetRequestID());
				return false;
			}

			auto & data = static_cast<CFtpLogonOpData&>(*operations_.back());

			CInteractiveLoginNotification *pInteractiveLoginNotification = static_cast<CInteractiveLoginNotification *>(pNotification);
			if (!pInteractiveLoginNotification->passwordSet) {
				ResetOperation(FZ_REPLY_CANCELED);
				return false;
			}
			data.credentials_.SetPass(pInteractiveLoginNotification->credentials.GetPass());
			SendNextCommand();
		}
		break;
	case reqId_certificate:
		{
			if (!tls_layer_ || tls_layer_->get_state() != fz::socket_state::connecting) {
				log(logmsg::debug_info, L"No or invalid operation in progress, ignoring request reply %d", pNotification->GetRequestID());
				return false;
			}

			CCertificateNotification* pCertificateNotification = static_cast<CCertificateNotification *>(pNotification);
			tls_layer_->set_verification_result(pCertificateNotification->trusted_);

			if (!pCertificateNotification->trusted_) {
				DoClose(FZ_REPLY_CRITICALERROR);
				return false;
			}

			if (!operations_.empty() && operations_.back()->opId == Command::connect &&
				operations_.back()->opState == LOGON_AUTH_WAIT)
			{
				operations_.back()->opState = LOGON_LOGON;
			}
		}
		break;
	case reqId_insecure_connection:
		{
			auto & notification = static_cast<CInsecureConnectionNotification&>(*pNotification);
			if (!notification.allow_) {
				ResetOperation(FZ_REPLY_CANCELED);
				return false;
			}
			else {
				SendNextCommand();
				return true;
			}
		}
	default:
		log(logmsg::debug_warning, L"Unknown request %d", pNotification->GetRequestID());
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return false;
	}

	return true;
}

void CFtpControlSocket::RawCommand(std::wstring const& command)
{
	assert(!command.empty());
	Push(std::make_unique<CFtpRawCommandOpData>(*this, command));
}

void CFtpControlSocket::Delete(CServerPath const& path, std::deque<std::wstring>&& files)
{
	auto pData = std::make_unique<CFtpDeleteOpData>(*this);
	pData->path_ = path;
	pData->files_ = std::move(files);
	pData->omitPath_ = true;

	Push(std::move(pData));
}

void CFtpControlSocket::RemoveDir(CServerPath const& path, std::wstring const& subDir)
{
	auto pData = std::make_unique<CFtpRemoveDirOpData>(*this);
	pData->path_ = path;
	pData->subDir_ = subDir;
	pData->omitPath_ = true;
	pData->fullPath_ = path;
	Push(std::move(pData));
}

void CFtpControlSocket::Mkdir(CServerPath const& path)
{
	/* Directory creation works like this: First find a parent directory into
	 * which we can CWD, then create the subdirs one by one. If either part
	 * fails, try MKD with the full path directly.
	 */

	if (operations_.empty() && !path.empty()) {
		log(logmsg::status, _("Creating directory '%s'..."), path.GetPath());
	}

	auto pData = std::make_unique<CFtpMkdirOpData>(*this);
	pData->path_ = path;

	Push(std::move(pData));
}

void CFtpControlSocket::Rename(CRenameCommand const& command)
{
	log(logmsg::status, _("Renaming '%s' to '%s'"), command.GetFromPath().FormatFilename(command.GetFromFile()), command.GetToPath().FormatFilename(command.GetToFile()));

	Push(std::make_unique<CFtpRenameOpData>(*this, command));
}

void CFtpControlSocket::Chmod(CChmodCommand const& command)
{
	log(logmsg::status, _("Setting permissions of '%s' to '%s'"), command.GetPath().FormatFilename(command.GetFile()), command.GetPermission());

	Push(std::make_unique<CFtpChmodOpData>(*this, command));
}

int CFtpControlSocket::GetExternalIPAddress(std::string& address)
{
	// Local IP should work. Only a complete moron would use IPv6
	// and NAT at the same time.
	if (socket_->address_family() != fz::address_type::ipv6) {
		int mode = engine_.GetOptions().GetOptionVal(OPTION_EXTERNALIPMODE);

		if (mode) {
			if (engine_.GetOptions().GetOptionVal(OPTION_NOEXTERNALONLOCAL) &&
				!fz::is_routable_address(socket_->peer_ip()))
				// Skip next block, use local address
				goto getLocalIP;
		}

		if (mode == 1) {
			std::wstring ip = engine_.GetOptions().GetOption(OPTION_EXTERNALIP);
			if (!ip.empty()) {
				address = fz::to_string(ip);
				return FZ_REPLY_OK;
			}

			log(logmsg::debug_warning, _("No external IP address set, trying default."));
		}
		else if (mode == 2) {
			if (!m_pIPResolver) {
				std::string localAddress = socket_->local_ip(true);

				if (!localAddress.empty() && localAddress == fz::to_string(engine_.GetOptions().GetOption(OPTION_LASTRESOLVEDIP))) {
					log(logmsg::debug_verbose, L"Using cached external IP address");

					address = localAddress;
					return FZ_REPLY_OK;
				}

				std::wstring resolverAddress = engine_.GetOptions().GetOption(OPTION_EXTERNALIPRESOLVER);

				log(logmsg::debug_info, _("Retrieving external IP address from %s"), resolverAddress);

				m_pIPResolver = std::make_unique<CExternalIPResolver>(engine_.GetThreadPool(), *this);
				m_pIPResolver->GetExternalIP(resolverAddress, fz::address_type::ipv4);
				if (!m_pIPResolver->Done()) {
					log(logmsg::debug_verbose, L"Waiting for resolver thread");
					return FZ_REPLY_WOULDBLOCK;
				}
			}
			if (!m_pIPResolver->Successful()) {
				m_pIPResolver.reset();

				log(logmsg::debug_warning, _("Failed to retrieve external IP address, using local address"));
			}
			else {
				log(logmsg::debug_info, L"Got external IP address");
				address = m_pIPResolver->GetIP();

				engine_.GetOptions().SetOption(OPTION_LASTRESOLVEDIP, fz::to_wstring(address));

				m_pIPResolver.reset();

				return FZ_REPLY_OK;
			}
		}
	}

getLocalIP:
	address = socket_->local_ip(true);
	if (address.empty()) {
		log(logmsg::error, _("Failed to retrieve local IP address."), 1);
		return FZ_REPLY_ERROR;
	}

	return FZ_REPLY_OK;
}

void CFtpControlSocket::OnExternalIPAddress()
{
	log(logmsg::debug_verbose, L"CFtpControlSocket::OnExternalIPAddress()");
	if (!m_pIPResolver) {
		log(logmsg::debug_info, L"Ignoring event");
		return;
	}

	SendNextCommand();
}

void CFtpControlSocket::Transfer(std::wstring const& cmd, CFtpTransferOpData* oldData)
{
	assert(oldData);
	oldData->tranferCommandSent = false;

	auto pData = std::make_unique<CFtpRawTransferOpData>(*this);

	pData->cmd_ = cmd;
	pData->pOldData = oldData;
	pData->pOldData->transferEndReason = TransferEndReason::successful;

	if (proxy_layer_) {
		// Only passive suported
		// Theoretically could use reverse proxy ability in SOCKS5, but
		// it is too fragile to set up with all those broken routers and
		// firewalls sabotaging connections. Regular active mode is hard
		// enough already
		pData->bPasv = true;
		pData->bTriedActive = true;
	}
	else {
		switch (currentServer_.GetPasvMode())
		{
		case MODE_PASSIVE:
			pData->bPasv = true;
			break;
		case MODE_ACTIVE:
			pData->bPasv = false;
			break;
		default:
			pData->bPasv = engine_.GetOptions().GetOptionVal(OPTION_USEPASV) != 0;
			break;
		}
	}

	if ((pData->pOldData->binary && m_lastTypeBinary == 1) ||
		(!pData->pOldData->binary && m_lastTypeBinary == 0))
	{
		pData->opState = rawtransfer_port_pasv;
	}
	else {
		pData->opState = rawtransfer_type;
	}

	Push(std::move(pData));
}

void CFtpControlSocket::Connect(CServer const& server, Credentials const& credentials)
{
	if (!operations_.empty()) {
		log(logmsg::debug_warning, L"CFtpControlSocket::Connect(): deleting stale operations");
		operations_.clear();
	}

	currentServer_ = server;

	Push(std::make_unique<CFtpLogonOpData>(*this, credentials));
}

void CFtpControlSocket::OnTimer(fz::timer_id id)
{
	if (id != m_idleTimer) {
		CControlSocket::OnTimer(id);
		return;
	}

	if (!operations_.empty()) {
		return;
	}

	if (m_pendingReplies || m_repliesToSkip) {
		return;
	}

	log(logmsg::status, _("Sending keep-alive command"));

	std::wstring cmd;
	auto i = fz::random_number(0, 2);
	if (!i) {
		cmd = L"NOOP";
	}
	else if (i == 1) {
		if (m_lastTypeBinary) {
			cmd = L"TYPE I";
		}
		else {
			cmd = L"TYPE A";
		}
	}
	else {
		cmd = L"PWD";
	}

	int res = SendCommand(cmd);
	if (res == FZ_REPLY_WOULDBLOCK) {
		++m_repliesToSkip;
	}
	else {
		DoClose(res);
	}
}

void CFtpControlSocket::StartKeepaliveTimer()
{
	if (!engine_.GetOptions().GetOptionVal(OPTION_FTP_SENDKEEPALIVE)) {
		return;
	}

	if (m_repliesToSkip || m_pendingReplies) {
		return;
	}

	if (!m_lastCommandCompletionTime) {
		return;
	}

	fz::duration const span = fz::monotonic_clock::now() - m_lastCommandCompletionTime;
	if (span.get_minutes() >= 30) {
		return;
	}

	stop_timer(m_idleTimer);
	m_idleTimer = add_timer(fz::duration::from_seconds(30), true);
}

void CFtpControlSocket::operator()(fz::event_base const& ev)
{
	if (fz::dispatch<fz::timer_event>(ev, this, &CFtpControlSocket::OnTimer)) {
		return;
	}

	if (fz::dispatch<CExternalIPResolveEvent>(ev, this, &CFtpControlSocket::OnExternalIPAddress)) {
		return;
	}

	if (fz::dispatch<TransferEndEvent>(ev, this, &CFtpControlSocket::TransferEnd)) {
		return;
	}

	if (fz::dispatch<fz::certificate_verification_event>(ev, this, &CFtpControlSocket::OnVerifyCert)) {
		return;
	}

	CRealControlSocket::operator()(ev);
}

void CFtpControlSocket::ResetSocket()
{
	tls_layer_.reset();
	CRealControlSocket::ResetSocket();
}

void CFtpControlSocket::OnVerifyCert(fz::tls_layer* source, fz::tls_session_info & info)
{
	if (!tls_layer_ || source != tls_layer_.get()) {
		return;
	}

	SendAsyncRequest(new CCertificateNotification(std::move(info)));
}
