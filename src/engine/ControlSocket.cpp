#include <filezilla.h>
#include "controlsocket.h"
#include "directorycache.h"
#include "engineprivate.h"
#include "local_path.h"
#include "lookup.h"
#include "logging_private.h"
#include "proxy.h"
#include "servercapabilities.h"
#include "sizeformatting_base.h"

#include <libfilezilla/event_loop.hpp>
#include <libfilezilla/iputils.hpp>
#include <libfilezilla/local_filesys.hpp>
#include <libfilezilla/rate_limited_layer.hpp>

#include <assert.h>
#include <string.h>

#ifndef FZ_WINDOWS
	#include <sys/stat.h>

	#define mutex mutex_override // Sadly on some platforms system headers include conflicting names
	#include <netdb.h>
	#undef mutex
	#ifndef AI_IDN
		#include <idna.h>
		extern "C" {
			#include <idn-free.h>
		}
	#endif
#endif

CControlSocket::CControlSocket(CFileZillaEnginePrivate & engine)
	: event_handler(engine.event_loop_)
	, engine_(engine)
	, opLockManager_(engine.opLockManager_)
	, logger_(engine.GetLogger())
{
}

CControlSocket::~CControlSocket()
{
	remove_handler();

	DoClose();
}

int CControlSocket::Disconnect()
{
	log(logmsg::status, _("Disconnected from server"));

	DoClose();
	return FZ_REPLY_OK;
}

Command CControlSocket::GetCurrentCommandId() const
{
	if (!operations_.empty()) {
		return operations_.back()->opId;
	}

	return Command::none;
}

void CControlSocket::LogTransferResultMessage(int nErrorCode, CFileTransferOpData *pData)
{
	bool tmp{};

	CTransferStatus const status = engine_.transfer_status_.Get(tmp);
	if (!status.empty() && (nErrorCode == FZ_REPLY_OK || status.madeProgress)) {
		int elapsed = static_cast<int>((fz::datetime::now() - status.started).get_seconds());
		if (elapsed <= 0) {
			elapsed = 1;
		}
		std::wstring time = fz::sprintf(fztranslate("%d second", "%d seconds", elapsed), elapsed);

		int64_t transferred = status.currentOffset - status.startOffset;
		std::wstring size = CSizeFormatBase::Format(&engine_.GetOptions(), transferred, true);

		logmsg::type msgType = logmsg::error;
		std::wstring msg;
		if (nErrorCode == FZ_REPLY_OK) {
			msgType = logmsg::status;
			msg = _("File transfer successful, transferred %s in %s");
		}
		else if ((nErrorCode & FZ_REPLY_CANCELED) == FZ_REPLY_CANCELED) {
			msg = _("File transfer aborted by user after transferring %s in %s");
		}
		else if ((nErrorCode & FZ_REPLY_CRITICALERROR) == FZ_REPLY_CRITICALERROR) {
			msg = _("Critical file transfer error after transferring %s in %s");
		}
		else {
			msg = _("File transfer failed after transferring %s in %s");
		}
		log(msgType, msg, size, time);
	}
	else {
		if ((nErrorCode & FZ_REPLY_CANCELED) == FZ_REPLY_CANCELED) {
			log(logmsg::error, _("File transfer aborted by user"));
		}
		else if (nErrorCode == FZ_REPLY_OK) {
			if (pData->transferInitiated_) {
				log(logmsg::status, _("File transfer successful"));
			}
			else {
				log(logmsg::status, _("File transfer skipped"));
			}
		}
		else if ((nErrorCode & FZ_REPLY_CRITICALERROR) == FZ_REPLY_CRITICALERROR) {
			log(logmsg::error, _("Critical file transfer error"));
		}
		else {
			log(logmsg::error, _("File transfer failed"));
		}
	}
}

void CControlSocket::Push(std::unique_ptr<COpData> && operation)
{
	operations_.emplace_back(std::move(operation));
}

int CControlSocket::ResetOperation(int nErrorCode)
{
	log(logmsg::debug_verbose, L"CControlSocket::ResetOperation(%d)", nErrorCode);

	if (nErrorCode & FZ_REPLY_WOULDBLOCK) {
		log(logmsg::debug_warning, L"ResetOperation with FZ_REPLY_WOULDBLOCK in nErrorCode (%d)", nErrorCode);
	}

	std::unique_ptr<COpData> oldOperation;
	if (!operations_.empty()) {
		oldOperation = std::move(operations_.back());
		operations_.pop_back();

		log(logmsg::debug_verbose, L"%s::Reset(%d) in state %d", oldOperation->name_, nErrorCode, oldOperation->opState);
		nErrorCode = oldOperation->Reset(nErrorCode);
	}
	if (!operations_.empty()) {
		if (nErrorCode == FZ_REPLY_OK ||
			nErrorCode == FZ_REPLY_ERROR ||
			nErrorCode == FZ_REPLY_CRITICALERROR ||
			nErrorCode == FZ_REPLY_ERROR_NOTFOUND)
		{
			if (!oldOperation->topLevelOperation_) {
				return ParseSubcommandResult(nErrorCode, *oldOperation);
			}
		}
		else {
			return ResetOperation(nErrorCode);
		}
	}

	std::wstring prefix;
	if ((nErrorCode & FZ_REPLY_CRITICALERROR) == FZ_REPLY_CRITICALERROR &&
		(!oldOperation || oldOperation->opId != Command::transfer))
	{
		prefix = _("Critical error:") + L" ";
	}

	if (oldOperation) {
		const Command commandId = oldOperation->opId;
		switch (commandId)
		{
		case Command::none:
			if (!prefix.empty()) {
				log(logmsg::error, _("Critical error"));
			}
			break;
		case Command::connect:
			if ((nErrorCode & FZ_REPLY_CANCELED) == FZ_REPLY_CANCELED) {
				log(logmsg::error, prefix + _("Connection attempt interrupted by user"));
			}
			else if (nErrorCode != FZ_REPLY_OK) {
				log(logmsg::error, prefix + _("Could not connect to server"));
			}
			break;
		case Command::list:
			if ((nErrorCode & FZ_REPLY_CANCELED) == FZ_REPLY_CANCELED) {
				log(logmsg::error, prefix + _("Directory listing aborted by user"));
			}
			else if (nErrorCode != FZ_REPLY_OK) {
				log(logmsg::error, prefix + _("Failed to retrieve directory listing"));
			}
			else {
				if (currentPath_.empty()) {
					log(logmsg::status, _("Directory listing successful"));
				}
				else {
					log(logmsg::status, _("Directory listing of \"%s\" successful"), currentPath_.GetPath());
				}
			}
			break;
		case Command::transfer:
			{
				auto & data = static_cast<CFileTransferOpData &>(*oldOperation);
				if (!data.download_ && data.transferInitiated_) {
					if (!currentServer_) {
						log(logmsg::debug_warning, L"currentServer_ is empty");
					}
					else {
						UpdateCache(data, data.remotePath_, data.remoteFile_, (nErrorCode == FZ_REPLY_OK) ? data.localFileSize_ : -1);
					}
				}
				LogTransferResultMessage(nErrorCode, &data);
			}
			break;
		default:
			if ((nErrorCode & FZ_REPLY_CANCELED) == FZ_REPLY_CANCELED) {
				log(logmsg::error, prefix + _("Interrupted by user"));
			}
			break;
		}
	}

	engine_.transfer_status_.Reset();

	if (m_invalidateCurrentPath) {
		currentPath_.clear();
		m_invalidateCurrentPath = false;
	}

	if (operations_.empty()) {
		SetWait(false);
		return engine_.ResetOperation(nErrorCode);
	}
	else {
		return SendNextCommand();
	}
}

void CControlSocket::UpdateCache(COpData const &, CServerPath const& serverPath, std::wstring const& remoteFile, int64_t fileSize)
{
	bool updated = engine_.GetDirectoryCache().UpdateFile(currentServer_, serverPath, remoteFile, true, CDirectoryCache::file, fileSize);
	if (updated) {
		SendDirectoryListingNotification(serverPath, false);
	}
}

int CControlSocket::DoClose(int nErrorCode)
{
	log(logmsg::debug_debug, L"CControlSocket::DoClose(%d)", nErrorCode);
	currentPath_.clear();
	return ResetOperation(FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED | nErrorCode);
}

std::wstring CControlSocket::ConvertDomainName(std::wstring const& domain)
{
#ifdef FZ_WINDOWS
	int len = IdnToAscii(IDN_ALLOW_UNASSIGNED, domain.c_str(), domain.size() + 1, nullptr, 0);
	if (!len) {
		log(logmsg::debug_warning, L"Could not convert domain name");
		return domain;
	}

	wchar_t* output = new wchar_t[len];
	int res = IdnToAscii(IDN_ALLOW_UNASSIGNED, domain.c_str(), domain.size() + 1, output, len);
	if (!res) {
		delete [] output;
		log(logmsg::debug_warning, L"Could not convert domain name");
		return domain;
	}

	std::wstring ret(output);
	delete [] output;
	return ret;
#elif defined(AI_IDN)
	return domain;
#else
	std::string const utf8 = fz::to_utf8(domain);

	char *output = 0;
	if (idna_to_ascii_8z(utf8.c_str(), &output, IDNA_ALLOW_UNASSIGNED)) {
		log(logmsg::debug_warning, L"Could not convert domain name");
		return domain;
	}

	std::wstring result = fz::to_wstring(std::string(output));
	idn_free(output);
	return result;
#endif
}

void CControlSocket::Cancel()
{
	if (GetCurrentCommandId() != Command::none) {
		if (GetCurrentCommandId() == Command::connect) {
			DoClose(FZ_REPLY_CANCELED);
		}
		else {
			ResetOperation(FZ_REPLY_CANCELED);
		}
	}
}

CServer const& CControlSocket::GetCurrentServer() const
{
	return currentServer_;
}

bool CControlSocket::ParsePwdReply(std::wstring reply, CServerPath const& defaultPath)
{
	size_t pos1 = reply.find('"');
	size_t pos2 = reply.rfind('"');
	// Due to searching the same character, pos1 is npos iff pos2 is npos

	if (pos1 == std::wstring::npos || pos1 >= pos2) {
		pos1 = reply.find('\'');
		pos2 = reply.rfind('\'');

		if (pos1 != std::wstring::npos && pos1 < pos2) {
			log(logmsg::debug_info, L"Broken server sending single-quoted path instead of double-quoted path.");
		}
	}
	if (pos1 == std::wstring::npos || pos1 >= pos2) {
		log(logmsg::debug_info, L"Broken server, no quoted path found in pwd reply, trying first token as path");
		pos1 = reply.find(' ');
		if (pos1 != std::wstring::npos) {
			reply = reply.substr(pos1 + 1);
			pos2 = reply.find(' ');
			if (pos2 != std::wstring::npos)
				reply = reply.substr(0, pos2);
		}
		else {
			reply.clear();
		}
	}
	else {
		reply = reply.substr(pos1 + 1, pos2 - pos1 - 1);
		fz::replace_substrings(reply, L"\"\"", L"\"");
	}

	currentPath_.SetType(currentServer_.GetType());
	if (reply.empty() || !currentPath_.SetPath(reply)) {
		if (reply.empty()) {
			log(logmsg::error, _("Server returned empty path."));
		}
		else {
			log(logmsg::error, _("Failed to parse returned path."));
		}

		if (!defaultPath.empty()) {
			log(logmsg::debug_warning, L"Assuming path is '%s'.", defaultPath.GetPath());
			currentPath_ = defaultPath;
			return true;
		}
		return false;
	}

	return true;
}

int CControlSocket::CheckOverwriteFile()
{
	log(logmsg::debug_debug, L"CControlSocket::CheckOverwriteFile()");
	if (operations_.empty() || operations_.back()->opId != Command::transfer) {
		log(logmsg::debug_info, L"CheckOverwriteFile called without active transfer.");
		return FZ_REPLY_INTERNALERROR;
	}

	auto & data = static_cast<CFileTransferOpData &>(*operations_.back());

	if (data.download_) {
		if (fz::local_filesys::get_file_type(fz::to_native(data.localFile_), true) != fz::local_filesys::file) {
			return FZ_REPLY_OK;
		}
	}

	CDirentry entry;
	bool dirDidExist;
	bool matchedCase;
	CServerPath remotePath;
	if (data.tryAbsolutePath_ || currentPath_.empty()) {
		remotePath = data.remotePath_;
	}
	else {
		remotePath = currentPath_;
	}
	bool found = engine_.GetDirectoryCache().LookupFile(entry, currentServer_, remotePath, data.remoteFile_, dirDidExist, matchedCase);

	// Ignore entries with wrong case
	if (found && !matchedCase)
		found = false;

	if (!data.download_) {
		if (!found && data.remoteFileSize_ < 0 && data.fileTime_.empty()) {
			return FZ_REPLY_OK;
		}
	}

	CFileExistsNotification *pNotification = new CFileExistsNotification;

	pNotification->download = data.download_;
	pNotification->localFile = data.localFile_;
	pNotification->remoteFile = data.remoteFile_;
	pNotification->remotePath = data.remotePath_;
	pNotification->localSize = data.localFileSize_;
	pNotification->remoteSize = data.remoteFileSize_;
	pNotification->remoteTime = data.fileTime_;
	pNotification->ascii = !data.transferSettings_.binary;

	if (data.download_ && pNotification->localSize >= 0) {
		pNotification->canResume = true;
	}
	else if (!data.download_ && pNotification->remoteSize >= 0) {
		pNotification->canResume = true;
	}
	else {
		pNotification->canResume = false;
	}

	pNotification->localTime = fz::local_filesys::get_modification_time(fz::to_native(data.localFile_));

	if (found) {
		if (pNotification->remoteTime.empty() && entry.has_date()) {
			pNotification->remoteTime = entry.time;
			data.fileTime_ = entry.time;
		}
	}

	SendAsyncRequest(pNotification);

	return FZ_REPLY_WOULDBLOCK;
}

SleepOpData::SleepOpData(CControlSocket & controlSocket, fz::duration const& delay)
	: COpData(Command::sleep, L"SleepOpData")
	, fz::event_handler(controlSocket.event_loop_)
	, controlSocket_(controlSocket)
{
	add_timer(delay, true);
	controlSocket_.SetWait(false);
}

void SleepOpData::operator()(fz::event_base const&)
{
	controlSocket_.ResetOperation(FZ_REPLY_OK);
}

CFileTransferOpData::CFileTransferOpData(wchar_t const* name, bool is_download, std::wstring const& local_file, std::wstring const& remote_file, CServerPath const& remote_path, CFileTransferCommand::t_transferSettings const& settings)
	: COpData(Command::transfer, name)
	, localFile_(local_file), remoteFile_(remote_file), remotePath_(remote_path)
	, download_(is_download)
	, transferSettings_(settings)
{
}

std::wstring CControlSocket::ConvToLocal(char const* buffer, size_t len)
{
	std::wstring ret;

	if (!len) {
		return ret;
	}

	if (m_useUTF8) {
		ret = fz::to_wstring_from_utf8(buffer, len);
		if (!ret.empty()) {
			return ret;
		}

		if (currentServer_.GetEncodingType() != ENCODING_UTF8) {
			log(logmsg::status, _("Invalid character sequence received, disabling UTF-8. Select UTF-8 option in site manager to force UTF-8."));
			m_useUTF8 = false;
		}
	}

	if (currentServer_.GetEncodingType() == ENCODING_CUSTOM) {
		ret = engine_.GetEncodingConverter().toLocal(currentServer_.GetCustomEncoding(), buffer, len);
		if (!ret.empty()) {
			return ret;
		}
	}

#ifdef FZ_WINDOWS
	// Only for Windows as other platforms should be UTF-8 anyhow.
	ret = fz::to_wstring(std::string(buffer, len));
	if (!ret.empty()) {
		return ret;
	}
#endif

	// Treat it as ISO8859-1
	ret.assign(reinterpret_cast<unsigned char const*>(buffer), reinterpret_cast<unsigned char const*>(buffer + len));

	return ret;
}

std::string CControlSocket::ConvToServer(std::wstring const& str, bool force_utf8)
{
	std::string ret;
	if (m_useUTF8 || force_utf8) {
		ret = fz::to_utf8(str);
		if (!ret.empty() || force_utf8) {
			return ret;
		}
	}

	if (currentServer_.GetEncodingType() == ENCODING_CUSTOM) {
		ret = engine_.GetEncodingConverter().toServer(currentServer_.GetCustomEncoding(), str.c_str(), str.size());
		if (!ret.empty()) {
			return ret;
		}
	}

	ret = fz::to_string(str);
	return ret;
}

void CControlSocket::OnTimer(fz::timer_id)
{
	m_timer = 0; // It's a one-shot timer, no need to stop it

	int const timeout = engine_.GetOptions().GetOptionVal(OPTION_TIMEOUT);
	if (timeout > 0) {
		fz::duration elapsed = fz::monotonic_clock::now() - m_lastActivity;

		if ((operations_.empty() || !operations_.back()->waitForAsyncRequest) && !opLockManager_.Waiting(this)) {
			if (elapsed > fz::duration::from_seconds(timeout)) {
				log(logmsg::error, fztranslate("Connection timed out after %d second of inactivity", "Connection timed out after %d seconds of inactivity", timeout), timeout);
				DoClose(FZ_REPLY_TIMEOUT);
				return;
			}
		}
		else {
			elapsed = fz::duration();
		}

		m_timer = add_timer(fz::duration::from_milliseconds(timeout * 1000) - elapsed, true);
	}
}

void CControlSocket::SetAlive()
{
	m_lastActivity = fz::monotonic_clock::now();
}

void CControlSocket::SetWait(bool wait)
{
	if (wait) {
		if (m_timer) {
			return;
		}

		m_lastActivity = fz::monotonic_clock::now();

		int timeout = engine_.GetOptions().GetOptionVal(OPTION_TIMEOUT);
		if (!timeout) {
			return;
		}

		m_timer = add_timer(fz::duration::from_milliseconds(timeout * 1000 + 100), true); // Add a bit of slack
	}
	else {
		stop_timer(m_timer);
		m_timer = 0;
	}
}

int CControlSocket::SendNextCommand()
{
	log(logmsg::debug_verbose, L"CControlSocket::SendNextCommand()");
	if (operations_.empty()) {
		log(logmsg::debug_warning, L"SendNextCommand called without active operation");
		ResetOperation(FZ_REPLY_ERROR);
		return FZ_REPLY_ERROR;
	}

	while (!operations_.empty()) {
		auto & data = *operations_.back();
		if (data.waitForAsyncRequest) {
			log(logmsg::debug_info, L"Waiting for async request, ignoring SendNextCommand...");
			return FZ_REPLY_WOULDBLOCK;
		}

		if (!CanSendNextCommand()) {
			SetWait(true);
			return FZ_REPLY_WOULDBLOCK;
		}

		log(data.sendLogLevel_, L"%s::Send() in state %d", data.name_, data.opState);
		int res = data.Send();
		if (res != FZ_REPLY_CONTINUE) {
			if (res == FZ_REPLY_OK) {
				return ResetOperation(res);
			}
			else if (res & FZ_REPLY_DISCONNECTED) {
				return DoClose(res);
			}
			else if (res & FZ_REPLY_ERROR) {
				return ResetOperation(res);
			}
			else if (res == FZ_REPLY_WOULDBLOCK) {
				return FZ_REPLY_WOULDBLOCK;
			}
			else if (res != FZ_REPLY_CONTINUE) {
				log(logmsg::debug_warning, L"Unknown result %d returned by COpData::Send()", res);
				return ResetOperation(FZ_REPLY_INTERNALERROR);
			}
		}
	}

	return FZ_REPLY_OK;
}

int CControlSocket::ParseSubcommandResult(int prevResult, COpData const& opData)
{
	if (operations_.empty()) {
		log(logmsg::debug_warning, L"CControlSocket::ParseSubcommandResult(%d) called without active operation", prevResult);
		ResetOperation(FZ_REPLY_ERROR);
		return FZ_REPLY_ERROR;
	}

	auto & data = *operations_.back();
	log(logmsg::debug_verbose, L"%s::SubcommandResult(%d) in state %d", data.name_, prevResult, data.opState);
	int res = data.SubcommandResult(prevResult, opData);
	if (res == FZ_REPLY_WOULDBLOCK) {
		return FZ_REPLY_WOULDBLOCK;
	}
	else if (res == FZ_REPLY_CONTINUE) {
		return SendNextCommand();
	}
	else {
		return ResetOperation(res);
	}
}

OpLock CControlSocket::Lock(locking_reason reason, CServerPath const& path, bool inclusive)
{
	return opLockManager_.Lock(this, reason, path, inclusive);
}

void CControlSocket::OnObtainLock()
{
	if (opLockManager_.ObtainWaiting(this)) {
		SendNextCommand();
	}
}

void CControlSocket::InvalidateCurrentWorkingDir(const CServerPath& path)
{
	assert(!path.empty());
	if (currentPath_.empty()) {
		return;
	}

	if (path.IsParentOf(currentPath_, false, true)) {
		if (!operations_.empty()) {
			m_invalidateCurrentPath = true;
		}
		else {
			currentPath_.clear();
		}
	}
}

fz::duration CControlSocket::GetTimezoneOffset() const
{
	fz::duration ret;
	if (currentServer_) {
		int seconds = 0;
		if (CServerCapabilities::GetCapability(currentServer_, timezone_offset, &seconds) == yes) {
			ret = fz::duration::from_seconds(seconds);
		}
	}
	return ret;
}

void CControlSocket::SendAsyncRequest(CAsyncRequestNotification* pNotification)
{
	assert(pNotification);
	assert(!operations_.empty());

	pNotification->requestNumber = engine_.GetNextAsyncRequestNumber();

	if (!operations_.empty()) {
		operations_.back()->waitForAsyncRequest = true;
	}
	engine_.AddNotification(pNotification);
}

// ------------------
// CRealControlSocket
// ------------------

CRealControlSocket::CRealControlSocket(CFileZillaEnginePrivate & engine)
	: CControlSocket(engine)
{
}

CRealControlSocket::~CRealControlSocket()
{
	ResetSocket();
}

bool CRealControlSocket::Connected() const
{
	return socket_ ? socket_->is_connected() : false;
}

int CRealControlSocket::Send(unsigned char const* buffer, unsigned int len)
{
	if (!active_layer_) {
		log(logmsg::debug_warning, L"Called internal CRealControlSocket::Send without m_pBackend");
		return FZ_REPLY_INTERNALERROR;
	}

	SetWait(true);
	if (send_buffer_) {
		send_buffer_.append(buffer, len);
	}
	else {
		int error;
		int written = active_layer_->write(buffer, len, error);
		if (written < 0) {
			if (error != EAGAIN) {
				log(logmsg::error, _("Could not write to socket: %s"), fz::socket_error_description(error));
				log(logmsg::error, _("Disconnected from server"));
				return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
			}
			written = 0;
		}

		if (written) {
			SetActive(CFileZillaEngine::send);
		}

		if (static_cast<unsigned int>(written) < len) {
			send_buffer_.append(buffer + written, len - written);
		}
	}

	return FZ_REPLY_WOULDBLOCK;
}

void CRealControlSocket::operator()(fz::event_base const& ev)
{
	if (!fz::dispatch<fz::socket_event, fz::hostaddress_event>(ev, this,
		&CRealControlSocket::OnSocketEvent,
		&CRealControlSocket::OnHostAddress))
	{
		CControlSocket::operator()(ev);
	}
}

void CRealControlSocket::OnSocketEvent(fz::socket_event_source*, fz::socket_event_flag t, int error)
{
	if (!active_layer_) {
		return;
	}

	switch (t)
	{
	case fz::socket_event_flag::connection_next:
		if (error) {
			log(logmsg::status, _("Connection attempt failed with \"%s\", trying next address."), fz::socket_error_description(error));
		}
		SetAlive();
		break;
	case fz::socket_event_flag::connection:
		if (error) {
			log(logmsg::status, _("Connection attempt failed with \"%s\"."), fz::socket_error_description(error));
			OnSocketError(error);
		}
		else {
			OnConnect();
		}
		break;
	case fz::socket_event_flag::read:
		if (error) {
			OnSocketError(error);
		}
		else {
			OnReceive();
		}
		break;
	case fz::socket_event_flag::write:
		if (error) {
			OnSocketError(error);
		}
		else {
			OnSend();
		}
		break;
	default:
		log(logmsg::debug_warning, L"Unhandled socket event %d", t);
		break;
	}
}

void CRealControlSocket::OnHostAddress(fz::socket_event_source*, std::string const& address)
{
	if (!active_layer_) {
		return;
	}

	log(logmsg::status, _("Connecting to %s..."), address);
}

void CRealControlSocket::OnConnect()
{
}

void CRealControlSocket::OnReceive()
{
}

int CRealControlSocket::OnSend()
{
	while (send_buffer_) {
		int error;
		int written = active_layer_->write(send_buffer_.get(), send_buffer_.size(), error);
		if (written < 0) {
			if (error != EAGAIN) {
				log(logmsg::error, _("Could not write to socket: %s"), fz::socket_error_description(error));
				if (GetCurrentCommandId() != Command::connect) {
					log(logmsg::error, _("Disconnected from server"));
				}
				DoClose();
				return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
			}
			return FZ_REPLY_WOULDBLOCK;
		}

		if (written) {
			SetActive(CFileZillaEngine::send);

			send_buffer_.consume(static_cast<size_t>(written));
		}
	}

	return FZ_REPLY_CONTINUE;
}

void CRealControlSocket::OnSocketError(int error)
{
	log(logmsg::debug_verbose, L"CRealControlSocket::OnSocketError(%d)", error);

	auto cmd = GetCurrentCommandId();
	if (cmd != Command::connect) {
		auto messageType = (cmd == Command::none) ? logmsg::status : logmsg::error;
		log(messageType, _("Disconnected from server: %s"), fz::socket_error_description(error));
	}
	DoClose();
}

int CRealControlSocket::DoConnect(std::wstring const& host, unsigned int port)
{
	SetWait(true);

	if (currentServer_.GetEncodingType() == ENCODING_CUSTOM) {
		log(logmsg::debug_info, L"Using custom encoding: %s", currentServer_.GetCustomEncoding());
	}

	ResetSocket();
	socket_ = std::make_unique<fz::socket>(engine_.GetThreadPool(), nullptr);
	ratelimit_layer_ = std::make_unique<fz::rate_limited_layer>(this, *socket_, &engine_.GetRateLimiter());
	active_layer_ = ratelimit_layer_.get();

	const int proxy_type = engine_.GetOptions().GetOptionVal(OPTION_PROXY_TYPE);
	if (proxy_type > static_cast<int>(ProxyType::NONE) && proxy_type < static_cast<int>(ProxyType::count) && !currentServer_.GetBypassProxy()) {
		log(logmsg::status, _("Connecting to %s through %s proxy"), currentServer_.Format(ServerFormat::with_optional_port), CProxySocket::Name(static_cast<ProxyType>(proxy_type)));

		fz::native_string proxy_host = fz::to_native(engine_.GetOptions().GetOption(OPTION_PROXY_HOST));

		proxy_layer_ = std::make_unique<CProxySocket>(this, *active_layer_, this, static_cast<ProxyType>(proxy_type),
			proxy_host, engine_.GetOptions().GetOptionVal(OPTION_PROXY_PORT),
			engine_.GetOptions().GetOption(OPTION_PROXY_USER),
			engine_.GetOptions().GetOption(OPTION_PROXY_PASS));
		active_layer_ = proxy_layer_.get();

		if (fz::get_address_type(proxy_host) == fz::address_type::unknown) {
			log(logmsg::status, _("Resolving address of %s"), proxy_host);
		}
	}
	else {
		if (fz::get_address_type(host) == fz::address_type::unknown) {
			log(logmsg::status, _("Resolving address of %s"), host);
		}
	}

	int res = active_layer_->connect(fz::to_native(ConvertDomainName(host)), port);

	if (res) {
		log(logmsg::error, _("Could not connect to server: %s"), fz::socket_error_description(res));
		return FZ_REPLY_DISCONNECTED | FZ_REPLY_ERROR; 
	}

	return FZ_REPLY_WOULDBLOCK;
}

int CRealControlSocket::DoClose(int nErrorCode)
{
	log(logmsg::debug_debug, L"CRealControlSocket::DoClose(%d)", nErrorCode);
	ResetSocket();

	return CControlSocket::DoClose(nErrorCode);
}

void CRealControlSocket::ResetSocket()
{
	active_layer_ = nullptr;

	// Destroy in reverse order
	proxy_layer_.reset();
	ratelimit_layer_.reset();
	socket_.reset();

	send_buffer_.clear();
}

bool CControlSocket::SetFileExistsAction(CFileExistsNotification *pFileExistsNotification)
{
	assert(pFileExistsNotification);

	if (operations_.empty() || operations_.back()->opId != Command::transfer) {
		log(logmsg::debug_info, L"SetFileExistsAction: No or invalid operation in progress, ignoring request reply %f", pFileExistsNotification->GetRequestID());
		return false;
	}

	auto & data = static_cast<CFileTransferOpData &>(*operations_.back());

	switch (pFileExistsNotification->overwriteAction)
	{
	case CFileExistsNotification::overwrite:
		SendNextCommand();
		break;
	case CFileExistsNotification::overwriteNewer:
		if (pFileExistsNotification->localTime.empty() || pFileExistsNotification->remoteTime.empty()) {
			SendNextCommand();
		}
		else if (pFileExistsNotification->download && pFileExistsNotification->localTime.earlier_than(pFileExistsNotification->remoteTime)) {
			SendNextCommand();
		}
		else if (!pFileExistsNotification->download && pFileExistsNotification->localTime.later_than(pFileExistsNotification->remoteTime)) {
			SendNextCommand();
		}
		else {
			if (data.download_) {
				std::wstring filename = data.remotePath_.FormatFilename(data.remoteFile_);
				log(logmsg::status, _("Skipping download of %s"), filename);
			}
			else {
				log(logmsg::status, _("Skipping upload of %s"), data.localFile_);
			}
			ResetOperation(FZ_REPLY_OK);
		}
		break;
	case CFileExistsNotification::overwriteSize:
		// First compare flags both size known but different, one size known and the other not (obviously they are different).
		// Second compare flags the remaining case in which we need to send command : both size unknown
		if ((pFileExistsNotification->localSize != pFileExistsNotification->remoteSize) || (pFileExistsNotification->localSize < 0)) {
			SendNextCommand();
		}
		else {
			if (data.download_) {
				std::wstring filename = data.remotePath_.FormatFilename(data.remoteFile_);
				log(logmsg::status, _("Skipping download of %s"), filename);
			}
			else {
				log(logmsg::status, _("Skipping upload of %s"), data.localFile_);
			}
			ResetOperation(FZ_REPLY_OK);
		}
		break;
	case CFileExistsNotification::overwriteSizeOrNewer:
		if (pFileExistsNotification->localTime.empty() || pFileExistsNotification->remoteTime.empty()) {
			SendNextCommand();
		}
		// First compare flags both size known but different, one size known and the other not (obviously they are different).
		// Second compare flags the remaining case in which we need to send command : both size unknown
		else if ((pFileExistsNotification->localSize != pFileExistsNotification->remoteSize) || (pFileExistsNotification->localSize < 0)) {
			SendNextCommand();
		}
		else if (pFileExistsNotification->download && pFileExistsNotification->localTime.earlier_than(pFileExistsNotification->remoteTime)) {
			SendNextCommand();
		}
		else if (!pFileExistsNotification->download && pFileExistsNotification->localTime.later_than(pFileExistsNotification->remoteTime)) {
			SendNextCommand();
		}
		else {
			if (data.download_) {
				auto const filename = data.remotePath_.FormatFilename(data.remoteFile_);
				log(logmsg::status, _("Skipping download of %s"), filename);
			}
			else {
				log(logmsg::status, _("Skipping upload of %s"), data.localFile_);
			}
			ResetOperation(FZ_REPLY_OK);
		}
		break;
	case CFileExistsNotification::resume:
		if (data.download_ && data.localFileSize_ >= 0) {
			data.resume_ = true;
		}
		else if (!data.download_ && data.remoteFileSize_ >= 0) {
			data.resume_ = true;
		}
		SendNextCommand();
		break;
	case CFileExistsNotification::rename:
		if (data.download_) {
			{
				std::wstring tmp;
				CLocalPath l(data.localFile_, &tmp);
				if (l.empty() || tmp.empty()) {
					ResetOperation(FZ_REPLY_INTERNALERROR);
					return false;
				}
				if (!l.ChangePath(pFileExistsNotification->newName)) {
					ResetOperation(FZ_REPLY_INTERNALERROR);
					return false;
				}
				if (!l.HasParent() || !l.MakeParent(&tmp)) {
					ResetOperation(FZ_REPLY_INTERNALERROR);
					return false;
				}

				data.localFile_ = l.GetPath() + tmp;
			}

			int64_t size;
			bool isLink;
			if (fz::local_filesys::get_file_info(fz::to_native(data.localFile_), isLink, &size, nullptr, nullptr) == fz::local_filesys::file) {
				data.localFileSize_ = size;
			}
			else {
				data.localFileSize_ = -1;
			}

			if (CheckOverwriteFile() == FZ_REPLY_OK) {
				SendNextCommand();
			}
		}
		else {
			data.remoteFile_ = pFileExistsNotification->newName;
			data.fileTime_ = fz::datetime();
			data.remoteFileSize_ = -1;

			CDirentry entry;
			bool dir_did_exist;
			bool matched_case;
			if (engine_.GetDirectoryCache().LookupFile(entry, currentServer_, data.tryAbsolutePath_ ? data.remotePath_ : currentPath_, data.remoteFile_, dir_did_exist, matched_case) &&
				matched_case)
			{
				data.remoteFileSize_ = entry.size;
				if (entry.has_date()) {
					data.fileTime_ = entry.time;
				}

				if (CheckOverwriteFile() != FZ_REPLY_OK) {
					break;
				}
			}

			SendNextCommand();
		}
		break;
	case CFileExistsNotification::skip:
		if (data.download_) {
			std::wstring filename = data.remotePath_.FormatFilename(data.remoteFile_);
			log(logmsg::status, _("Skipping download of %s"), filename);
		}
		else {
			log(logmsg::status, _("Skipping upload of %s"), data.localFile_);
		}
		ResetOperation(FZ_REPLY_OK);
		break;
	default:
		log(logmsg::debug_warning, L"Unknown file exists action: %d", pFileExistsNotification->overwriteAction);
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return false;
	}

	return true;
}

void CControlSocket::CreateLocalDir(std::wstring const& local_file)
{
	std::wstring file;
	CLocalPath local_path(local_file, &file);
	if (local_path.empty() || !local_path.HasParent()) {
		return;
	}

	CLocalPath last_successful;
	local_path.Create(&last_successful);

	if (!last_successful.empty()) {
		// Send out notification
		CLocalDirCreatedNotification *n = new CLocalDirCreatedNotification;
		n->dir = last_successful;
		engine_.AddNotification(n);
	}
}

void CControlSocket::List(CServerPath const&, std::wstring const&, int)
{
	Push(std::make_unique<CNotSupportedOpData>());
}

void CControlSocket::RawCommand(std::wstring const&)
{
	Push(std::make_unique<CNotSupportedOpData>());
}

void CControlSocket::Delete(CServerPath const&, std::vector<std::wstring>&&)
{
	Push(std::make_unique<CNotSupportedOpData>());
}

void CControlSocket::RemoveDir(CServerPath const&, std::wstring const&)
{
	Push(std::make_unique<CNotSupportedOpData>());
}

void CControlSocket::Mkdir(CServerPath const&)
{
	Push(std::make_unique<CNotSupportedOpData>());
}

void CControlSocket::Rename(CRenameCommand const&)
{
	Push(std::make_unique<CNotSupportedOpData>());
}

void CControlSocket::Chmod(CChmodCommand const&)
{
	Push(std::make_unique<CNotSupportedOpData>());
}

void CControlSocket::Lookup(CServerPath const& path, std::wstring const& file, CDirentry * entry)
{
	Push(std::make_unique<LookupOpData>(*this, path, file, entry));
}

void CControlSocket::Lookup(CServerPath const& path, std::vector<std::wstring> const& files)
{
	Push(std::make_unique<LookupManyOpData>(*this, path, files));
}

void CControlSocket::operator()(fz::event_base const& ev)
{
	fz::dispatch<fz::timer_event, CObtainLockEvent>(ev, this,
		&CControlSocket::OnTimer,
		&CControlSocket::OnObtainLock);
}

void CControlSocket::SetActive(CFileZillaEngine::_direction direction)
{
	SetAlive();
	engine_.SetActive(direction);
}

void CControlSocket::SendDirectoryListingNotification(CServerPath const& path, bool failed)
{
	if (!currentServer_) {
		return;
	}

	engine_.AddNotification(new CDirectoryListingNotification(path, operations_.size() == 1 && operations_.back()->opId == Command::list, failed));
}

void CControlSocket::CallSetAsyncRequestReply(CAsyncRequestNotification *pNotification)
{
	if (operations_.empty() || !operations_.back()->waitForAsyncRequest) {
		log(logmsg::debug_info, L"Not waiting for request reply, ignoring request reply %d", pNotification->GetRequestID());
		return;
	}

	operations_.back()->waitForAsyncRequest = false;

	SetAlive();
	SetAsyncRequestReply(pNotification);
}

void CControlSocket::Sleep(fz::duration const& delay)
{
	Push(std::make_unique<SleepOpData>(*this, delay));
}

int64_t CalculateNextChunkSize(int64_t remaining, int64_t lastChunkSize, fz::duration const& lastChunkDuration, int64_t minChunkSize, int64_t multiple, int64_t partCount, int64_t maxPartCount, int64_t maxChunkSize)
{
	if (remaining <= 0) {
		return 0;
	}

	int64_t newChunkSize = minChunkSize;

	if (lastChunkDuration && lastChunkSize) {
		int64_t size = lastChunkSize * 30000 / lastChunkDuration.get_milliseconds();
		if (size > newChunkSize) {
			newChunkSize = size;
		}
	}

	if (maxPartCount) {
		if (newChunkSize * (maxPartCount - partCount) < remaining) {
			if (maxPartCount - partCount <= 1) {
				newChunkSize = remaining;
			}
			else {
				newChunkSize = remaining / (maxPartCount - partCount - 1);
			}
		}
	}

	if (multiple) {
		// Round up
		int modulus = newChunkSize % multiple;
		if (modulus) {
			newChunkSize += multiple - modulus;
		}
	}

	if (maxChunkSize && newChunkSize > maxChunkSize) {
		newChunkSize = maxChunkSize;
	}

	if (newChunkSize > remaining) {
		newChunkSize = remaining;
	}

	return newChunkSize;
}

int64_t CalculateNextChunkSize(int64_t remaining, int64_t lastChunkSize, fz::monotonic_clock const& lastChunkStart, int64_t minChunkSize, int64_t multiple, int64_t partCount, int64_t maxPartCount, int64_t maxChunkSize)
{
	return CalculateNextChunkSize(remaining, lastChunkSize, fz::monotonic_clock::now() - lastChunkStart, minChunkSize, multiple, partCount, maxPartCount, maxChunkSize);
}
