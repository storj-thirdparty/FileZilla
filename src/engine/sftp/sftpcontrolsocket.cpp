#include <filezilla.h>

#include "chmod.h"
#include "connect.h"
#include "cwd.h"
#include "delete.h"
#include "directorycache.h"
#include "directorylistingparser.h"
#include "engineprivate.h"
#include "event.h"
#include "filetransfer.h"
#include "list.h"
#include "input_thread.h"
#include "mkd.h"
#include "pathcache.h"
#include "proxy.h"
#include "rename.h"
#include "rmd.h"
#include "servercapabilities.h"
#include "sftpcontrolsocket.h"

#include <libfilezilla/event_loop.hpp>
#include <libfilezilla/local_filesys.hpp>
#include <libfilezilla/process.hpp>

#include <algorithm>
#include <cwchar>

CSftpControlSocket::CSftpControlSocket(CFileZillaEnginePrivate & engine)
	: CControlSocket(engine)
{
	m_useUTF8 = true;
}

CSftpControlSocket::~CSftpControlSocket()
{
	remove_handler();
	DoClose();
}

void CSftpControlSocket::Connect(CServer const& server, Credentials const& credentials)
{
	log(logmsg::status, _("Connecting to %s..."), server.Format(ServerFormat::with_optional_port, credentials));
	SetWait(true);

	m_sftpEncryptionDetails = CSftpEncryptionNotification();

	if (server.GetEncodingType() == ENCODING_CUSTOM) {
		log(logmsg::debug_info, L"Using custom encoding: %s", server.GetCustomEncoding());
		m_useUTF8 = false;
	}
	else {
		m_useUTF8 = true;
	}

	currentServer_ = server;

	auto pData = std::make_unique<CSftpConnectOpData>(*this, credentials);
	if (!credentials.keyFile_.empty()) {
		pData->keyfiles_ = fz::strtok(credentials.keyFile_, L"\r\n");
	}
	else {
		pData->keyfiles_ = fz::strtok(engine_.GetOptions().GetOption(OPTION_SFTP_KEYFILES), L"\r\n");
	}

	pData->keyfiles_.erase(
		std::remove_if(pData->keyfiles_.begin(), pData->keyfiles_.end(),
			[this](std::wstring const& keyfile) {
				if (fz::local_filesys::get_file_type(fz::to_native(keyfile), true) != fz::local_filesys::file) {
					log(logmsg::status, _("Skipping non-existing key file \"%s\""), keyfile);
					return true;
				}
				return false;
		}), pData->keyfiles_.end());

	pData->keyfile_ = pData->keyfiles_.cbegin();

	process_ = std::make_unique<fz::process>();

	engine_.GetRateLimiter().AddObject(this);
	Push(std::move(pData));
}

void CSftpControlSocket::OnSftpEvent(sftp_message const& message)
{
	if (!currentServer_) {
		return;
	}

	if (!input_thread_) {
		return;
	}

	switch (message.type)
	{
	case sftpEvent::Reply:
		log_raw(logmsg::reply, message.text[0]);
		ProcessReply(FZ_REPLY_OK, message.text[0]);
		break;
	case sftpEvent::Done:
		{
			int result;
			if (message.text[0] == L"1") {
				result = FZ_REPLY_OK;
			}
			else if (message.text[0] == L"2") {
				result = FZ_REPLY_CRITICALERROR;
			}
			else {
				result = FZ_REPLY_ERROR;
			}
			ProcessReply(result, std::wstring());
		}
		break;
	case sftpEvent::Error:
		log_raw(logmsg::error, message.text[0]);
		break;
	case sftpEvent::Verbose:
		log_raw(logmsg::debug_info, message.text[0]);
		break;
	case sftpEvent::Info:
		log_raw(logmsg::command, message.text[0]); // Not exactly the right message type, but it's a silent one.
		break;
	case sftpEvent::Status:
		log_raw(logmsg::status, message.text[0]);
		break;
	case sftpEvent::Recv:
		SetActive(CFileZillaEngine::recv);
		break;
	case sftpEvent::Send:
		SetActive(CFileZillaEngine::send);
		break;
	case sftpEvent::Transfer:
		{
			auto value = fz::to_integral<int64_t>(message.text[0]);

			bool tmp;
			CTransferStatus status = engine_.transfer_status_.Get(tmp);
			if (!status.empty() && !status.madeProgress) {
				if (!operations_.empty() && operations_.back()->opId == Command::transfer) {
					auto & data = static_cast<CSftpFileTransferOpData &>(*operations_.back());
					if (data.download_) {
						if (value > 0) {
							engine_.transfer_status_.SetMadeProgress();
						}
					}
					else {
						if (status.currentOffset > status.startOffset + 65565) {
							engine_.transfer_status_.SetMadeProgress();
						}
					}
				}
			}

			engine_.transfer_status_.Update(value);
		}
		break;
	case sftpEvent::AskHostkey:
	case sftpEvent::AskHostkeyChanged:
		{
			auto port = fz::to_integral<int>(message.text[1]);
			if (port <= 0 || port > 65535) {
				DoClose(FZ_REPLY_INTERNALERROR);
				break;
			}
			SendAsyncRequest(new CHostKeyNotification(message.text[0], port, m_sftpEncryptionDetails, message.type == sftpEvent::AskHostkeyChanged));
		}
		break;
	case sftpEvent::AskHostkeyBetteralg:
		log(logmsg::error, L"Got sftpReqHostkeyBetteralg when we shouldn't have. Aborting connection.");
		DoClose(FZ_REPLY_INTERNALERROR);
		break;
	case sftpEvent::AskPassword:
		if (operations_.empty() || operations_.back()->opId != Command::connect) {
			log(logmsg::debug_warning, L"sftpReqPassword outside connect operation, ignoring.");
			break;
		}
		else {
			auto & data = static_cast<CSftpConnectOpData&>(*operations_.back());

			std::wstring const challengeIdentifier = m_requestPreamble + L"\n" + m_requestInstruction + L"\n" + message.text[0];

			CInteractiveLoginNotification::type t = CInteractiveLoginNotification::interactive;
			if (data.credentials_.logonType_ == LogonType::interactive || m_requestPreamble == L"SSH key passphrase") {
				if (m_requestPreamble == L"SSH key passphrase") {
					t = CInteractiveLoginNotification::keyfile;
				}

				std::wstring challenge;
				if (!m_requestPreamble.empty() && t != CInteractiveLoginNotification::keyfile) {
					challenge += m_requestPreamble + L"\n";
				}
				if (!m_requestInstruction.empty()) {
					challenge += m_requestInstruction + L"\n";
				}
				if (message.text[0] != L"Password:") {
					challenge += message.text[0];
				}
				CInteractiveLoginNotification *pNotification = new CInteractiveLoginNotification(t, challenge, data.lastChallenge == challengeIdentifier);
				pNotification->server = currentServer_;
				pNotification->handle_ = handle_;
				pNotification->credentials = data.credentials_;

				SendAsyncRequest(pNotification);
			}
			else {
				if (!data.lastChallenge.empty() && data.lastChallengeType != CInteractiveLoginNotification::keyfile) {
					// Check for same challenge. Will most likely fail as well, so abort early.
					if (data.lastChallenge == challengeIdentifier) {
						log(logmsg::error, _("Authentication failed."));
					}
					else {
						log(logmsg::error, _("Server sent an additional login prompt. You need to use the interactive login type."));
					}
					DoClose(FZ_REPLY_CRITICALERROR | FZ_REPLY_PASSWORDFAILED);
					return;
				}

				std::wstring const pass = (data.credentials_.logonType_ == LogonType::anonymous) ? L"anonymous@example.com" : data.credentials_.GetPass();
				std::wstring show = L"Pass: ";
				show.append(pass.size(), '*');
				SendCommand(pass, show);
			}
			data.lastChallenge = challengeIdentifier;
			data.lastChallengeType = t;
		}
		break;
	case sftpEvent::RequestPreamble:
		m_requestPreamble = message.text[0];
		break;
	case sftpEvent::RequestInstruction:
		m_requestInstruction = message.text[0];
		break;
	case sftpEvent::UsedQuotaRecv:
		OnQuotaRequest(CRateLimiter::inbound);
		break;
	case sftpEvent::UsedQuotaSend:
		OnQuotaRequest(CRateLimiter::outbound);
		break;
	case sftpEvent::KexAlgorithm:
		m_sftpEncryptionDetails.kexAlgorithm = message.text[0];
		break;
	case sftpEvent::KexHash:
		m_sftpEncryptionDetails.kexHash = message.text[0];
		break;
	case sftpEvent::KexCurve:
		m_sftpEncryptionDetails.kexCurve = message.text[0];
		break;
	case sftpEvent::CipherClientToServer:
		m_sftpEncryptionDetails.cipherClientToServer = message.text[0];
		break;
	case sftpEvent::CipherServerToClient:
		m_sftpEncryptionDetails.cipherServerToClient = message.text[0];
		break;
	case sftpEvent::MacClientToServer:
		m_sftpEncryptionDetails.macClientToServer = message.text[0];
		break;
	case sftpEvent::MacServerToClient:
		m_sftpEncryptionDetails.macServerToClient = message.text[0];
		break;
	case sftpEvent::Hostkey:
		{
			auto tokens = fz::strtok(message.text[0], ' ');
			if (!tokens.empty()) {
				m_sftpEncryptionDetails.hostKeyFingerprintSHA256 = tokens.back();
				tokens.pop_back();
			}
			if (!tokens.empty()) {
				m_sftpEncryptionDetails.hostKeyFingerprintMD5 = tokens.back();
				tokens.pop_back();
			}
			for (auto const& token : tokens) {
				if (!m_sftpEncryptionDetails.hostKeyAlgorithm.empty()) {
					m_sftpEncryptionDetails.hostKeyAlgorithm += ' ';
				}
				m_sftpEncryptionDetails.hostKeyAlgorithm += token;
			}
		}
		break;
	default:
		log(logmsg::debug_warning, L"Message type %d not handled", message.type);
		break;
	}
}

void CSftpControlSocket::OnSftpListEvent(sftp_list_message const& message)
{
	if (!currentServer_) {
		return;
	}

	if (!input_thread_) {
		return;
	}

	if (operations_.empty() || operations_.back()->opId != Command::list) {
		log(logmsg::debug_warning, L"sftpEvent::Listentry outside list operation, ignoring.");
		return;
	}
	else {
		int res = static_cast<CSftpListOpData&>(*operations_.back()).ParseEntry(std::move(message.text), message.mtime, std::move(message.name));
		if (res != FZ_REPLY_WOULDBLOCK) {
			ResetOperation(res);
		}
	}
}

void CSftpControlSocket::OnTerminate(std::wstring const& error)
{
	if (!error.empty()) {
		log_raw(logmsg::error, error);
	}
	else {
		log_raw(logmsg::debug_info, L"CSftpControlSocket::OnTerminate without error");
	}
	if (process_) {
		DoClose();
	}
}

int CSftpControlSocket::SendCommand(std::wstring const& cmd, std::wstring const& show)
{
	SetWait(true);

	log_raw(logmsg::command, show.empty() ? cmd : show);

	// Check for newlines in command
	// a command like "ls\nrm foo/bar" is dangerous
	if (cmd.find('\n') != std::wstring::npos ||
		cmd.find('\r') != std::wstring::npos)
	{
		log(logmsg::debug_warning, L"Command containing newline characters, aborting.");
		return FZ_REPLY_INTERNALERROR;
	}

	return AddToStream(cmd + L"\n");
}

int CSftpControlSocket::AddToStream(std::wstring const& cmd)
{
	std::string const str = ConvToServer(cmd);
	if (str.empty()) {
		log(logmsg::error, _("Could not convert command to server encoding"));
		return FZ_REPLY_ERROR;
	}

	return AddToStream(str);
}

int CSftpControlSocket::AddToStream(std::string const& cmd)
{
	if (!process_) {
		return FZ_REPLY_INTERNALERROR;
	}

	if (!process_->write(cmd)) {
		return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
	}

	return FZ_REPLY_WOULDBLOCK;
}

bool CSftpControlSocket::SetAsyncRequestReply(CAsyncRequestNotification *pNotification)
{
	log(logmsg::debug_verbose, L"CSftpControlSocket::SetAsyncRequestReply");

	RequestId const requestId = pNotification->GetRequestID();
	switch(requestId)
	{
	case reqId_fileexists:
		{
			auto *pFileExistsNotification = static_cast<CFileExistsNotification *>(pNotification);
			return SetFileExistsAction(pFileExistsNotification);
		}
	case reqId_hostkey:
	case reqId_hostkeyChanged:
		{
			if (GetCurrentCommandId() != Command::connect ||
				!currentServer_)
			{
				log(logmsg::debug_info, L"SetAsyncRequestReply called to wrong time");
				return false;
			}

			auto *pHostKeyNotification = static_cast<CHostKeyNotification *>(pNotification);
			std::wstring show;
			if (requestId == reqId_hostkey) {
				show = _("Trust new Hostkey:");
			}
			else {
				show = _("Trust changed Hostkey:");
			}
			show += ' ';
			if (!pHostKeyNotification->m_trust) {
				SendCommand(std::wstring(), show + _("No"));
				if (operations_.back()->opId == Command::connect) {
					auto &data = static_cast<CSftpConnectOpData &>(*operations_.back());
					data.criticalFailure = true;
				}
			}
			else if (pHostKeyNotification->m_alwaysTrust) {
				SendCommand(L"y", show + _("Yes"));
			}
			else {
				SendCommand(L"n", show + _("Once"));
			}
		}
		break;
	case reqId_interactiveLogin:
		{
			if (operations_.empty() || operations_.back()->opId != Command::connect) {
				log(logmsg::debug_info, L"No or invalid operation in progress, ignoring request reply %d", pNotification->GetRequestID());
				return false;
			}

			auto & data = static_cast<CSftpConnectOpData&>(*operations_.back());

			auto *pInteractiveLoginNotification = static_cast<CInteractiveLoginNotification *>(pNotification);

			if (!pInteractiveLoginNotification->passwordSet) {
				DoClose(FZ_REPLY_CANCELED);
				return false;
			}
			std::wstring const& pass = pInteractiveLoginNotification->credentials.GetPass();
			if (pInteractiveLoginNotification->GetType() != CInteractiveLoginNotification::keyfile) {
				data.credentials_.SetPass(pass);
			}
			std::wstring show = L"Pass: ";
			show.append(pass.size(), '*');
			SendCommand(pass, show);
		}
		break;
	default:
		log(logmsg::debug_warning, L"Unknown async request reply id: %d", requestId);
		return false;
	}

	return true;
}

void CSftpControlSocket::List(CServerPath const& path, std::wstring const& subDir, int flags)
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

	Push(std::make_unique<CSftpListOpData>(*this, path, subDir, flags));
}

void CSftpControlSocket::ChangeDir(CServerPath const& path, std::wstring const& subDir, bool link_discovery)
{
	auto pData = std::make_unique<CSftpChangeDirOpData>(*this);
	pData->path_ = path;
	pData->subDir_ = subDir;
	pData->link_discovery_ = link_discovery;

	if (!operations_.empty() && operations_.back()->opId == Command::transfer &&
		!static_cast<CSftpFileTransferOpData&>(*operations_.back()).download_)
	{
		pData->tryMkdOnFail_ = true;
		assert(subDir.empty());
	}

	Push(std::move(pData));
}

void CSftpControlSocket::ProcessReply(int result, std::wstring const& reply)
{
	result_ = result;
	response_ = reply;

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
		if (data.opId == Command::connect) {
			DoClose(res | FZ_REPLY_DISCONNECTED);
		}
		else {
			ResetOperation(res);
		}
	}
}
void CSftpControlSocket::FileTransfer(std::wstring const& localFile, CServerPath const& remotePath,
									std::wstring const& remoteFile, bool download,
									CFileTransferCommand::t_transferSettings const& transferSettings)
{
	auto pData = std::make_unique<CSftpFileTransferOpData>(*this, download, localFile, remoteFile, remotePath, transferSettings);
	Push(std::move(pData));
}

int CSftpControlSocket::DoClose(int nErrorCode)
{
	engine_.GetRateLimiter().RemoveObject(this);

	if (process_) {
		process_->kill();
	}

	if (input_thread_) {
		input_thread_.reset();

		auto threadEventsFilter = [&](fz::event_loop::Events::value_type const& ev) -> bool {
			if (ev.first != this) {
				return false;
			}
			else if (ev.second->derived_type() == CSftpEvent::type() || ev.second->derived_type() == CTerminateEvent::type()) {
				return true;
			}
			return false;
		};

		event_loop_.filter_events(threadEventsFilter);
	}
	process_.reset();
	return CControlSocket::DoClose(nErrorCode);
}

void CSftpControlSocket::Cancel()
{
	if (GetCurrentCommandId() != Command::none) {
		DoClose(FZ_REPLY_CANCELED);
	}
}

void CSftpControlSocket::Mkdir(CServerPath const& path)
{
	/* Directory creation works like this: First find a parent directory into
	 * which we can CWD, then create the subdirs one by one. If either part
	 * fails, try MKD with the full path directly.
	 */

	if (operations_.empty()) {
		log(logmsg::status, _("Creating directory '%s'..."), path.GetPath());
	}

	auto pData = std::make_unique<CSftpMkdirOpData>(*this);
	pData->path_ = path;
	Push(std::move(pData));
}

std::wstring CSftpControlSocket::QuoteFilename(std::wstring const& filename)
{
	return L"\"" + fz::replaced_substrings(filename, L"\"", L"\"\"") + L"\"";
}

void CSftpControlSocket::Delete(const CServerPath& path, std::deque<std::wstring>&& files)
{
	// CFileZillaEnginePrivate should have checked this already
	assert(!files.empty());

	log(logmsg::debug_verbose, L"CSftpControlSocket::Delete");
	
	auto pData = std::make_unique<CSftpDeleteOpData>(*this);
	pData->path_ = path;
	pData->files_ = std::move(files);
	Push(std::move(pData));
}

void CSftpControlSocket::RemoveDir(CServerPath const& path, std::wstring const& subDir)
{
	log(logmsg::debug_verbose, L"CSftpControlSocket::RemoveDir");

	auto pData = std::make_unique<CSftpRemoveDirOpData>(*this);
	pData->path_ = path;
	pData->subDir_ = subDir;
	Push(std::move(pData));
}

void CSftpControlSocket::Chmod(CChmodCommand const& command)
{
	log(logmsg::status, _("Setting permissions of '%s' to '%s'"), command.GetPath().FormatFilename(command.GetFile()), command.GetPermission());
	Push(std::make_unique<CSftpChmodOpData>(*this, command));
}

void CSftpControlSocket::Rename(CRenameCommand const& command)
{
	log(logmsg::status, _("Renaming '%s' to '%s'"), command.GetFromPath().FormatFilename(command.GetFromFile()), command.GetToPath().FormatFilename(command.GetToFile()));
	Push(std::make_unique<CSftpRenameOpData>(*this, command));
}

std::wstring CSftpControlSocket::WildcardEscape(std::wstring const& file)
{
	std::wstring ret;
	// see src/putty/wildcard.c

	ret.reserve(file.size());
	for (size_t i = 0; i < file.size(); ++i) {
		auto const& c = file[i];
		switch (c)
		{
		case '[':
		case ']':
		case '*':
		case '?':
		case '\\':
			ret.push_back('\\');
			break;
		default:
			break;
		}
		ret.push_back(c);
	}
	return ret;
}

void CSftpControlSocket::OnRateAvailable(CRateLimiter::rate_direction direction)
{
	OnQuotaRequest(direction);
}

void CSftpControlSocket::OnQuotaRequest(CRateLimiter::rate_direction direction)
{
	int64_t bytes = GetAvailableBytes(direction);
	if (bytes > 0) {
		int b;
		if (bytes > std::numeric_limits<int>::max()) {
			b = std::numeric_limits<int>::max();
		}
		else {
			b = static_cast<int>(bytes);
		}
		AddToStream(fz::sprintf("-%d%d,%d\n", direction, b, engine_.GetOptions().GetOptionVal(OPTION_SPEEDLIMIT_INBOUND + static_cast<int>(direction))));
		UpdateUsage(direction, b);
	}
	else if (bytes == 0) {
		Wait(direction);
	}
	else if (bytes < 0) {
		AddToStream(fz::sprintf("-%d-\n", direction));
	}
}

void CSftpControlSocket::operator()(fz::event_base const& ev)
{
	if (fz::dispatch<CSftpEvent, CSftpListEvent, CTerminateEvent>(ev, this,
		&CSftpControlSocket::OnSftpEvent,
		&CSftpControlSocket::OnSftpListEvent,
		&CSftpControlSocket::OnTerminate)) {
		return;
	}

	CControlSocket::operator()(ev);
}
