#include <filezilla.h>

#include "connect.h"
#include "delete.h"
#include "event.h"
#include "input_thread.h"
#include "../directorycache.h"
#include "directorylistingparser.h"
#include "engineprivate.h"
#include "file_transfer.h"
#include "list.h"
#include "mkd.h"
#include "pathcache.h"
#include "proxy.h"
#include "resolve.h"
#include "rmd.h"
#include "servercapabilities.h"
#include "storjcontrolsocket.h"

#include <libfilezilla/event_loop.hpp>
#include <libfilezilla/local_filesys.hpp>
#include <libfilezilla/process.hpp>
#include <libfilezilla/thread_pool.hpp>

#include <algorithm>

#include <assert.h>

CStorjControlSocket::CStorjControlSocket(CFileZillaEnginePrivate & engine)
	: CControlSocket(engine)
{
	m_useUTF8 = true;
}

CStorjControlSocket::~CStorjControlSocket()
{
	remove_handler();
	DoClose();
}

void CStorjControlSocket::Connect(CServer const &server, Credentials const& credentials)
{
	currentServer_ = server;
	credentials_ = credentials;

	Push(std::make_unique<CStorjConnectOpData>(*this));
}

void CStorjControlSocket::List(CServerPath const& path, std::wstring const& subDir, int flags)
{
	Push(std::make_unique<CStorjListOpData>(*this, path, subDir, flags));
}

void CStorjControlSocket::FileTransfer(std::wstring const& localFile, CServerPath const& remotePath,
						 std::wstring const& remoteFile, bool download,
						 CFileTransferCommand::t_transferSettings const& transferSettings)
{
	auto pData = std::make_unique<CStorjFileTransferOpData>(*this, download, localFile, remoteFile, remotePath, transferSettings);
	Push(std::move(pData));
}


void CStorjControlSocket::Delete(CServerPath const& path, std::vector<std::wstring>&& files)
{
	// CFileZillaEnginePrivate should have checked this already
	assert(!files.empty());

	log(logmsg::debug_verbose, L"CStorjControlSocket::Delete");

	Push(std::make_unique<CStorjDeleteOpData>(*this, path, std::move(files)));
}

void CStorjControlSocket::Resolve(CServerPath const& path, std::wstring const& file, std::wstring & bucket, std::wstring * fileId, bool ignore_missing_file)
{
	Push(std::make_unique<CStorjResolveOpData>(*this, path, file, bucket, fileId, ignore_missing_file));
}

void CStorjControlSocket::Resolve(CServerPath const& path, std::vector<std::wstring> const& files, std::wstring & bucket, std::vector<std::wstring> & fileIds)
{
	Push(std::make_unique<CStorjResolveManyOpData>(*this, path, files, bucket, fileIds));
}

void CStorjControlSocket::Mkdir(CServerPath const& path)
{
	auto pData = std::make_unique<CStorjMkdirOpData>(*this);
	pData->path_ = path;
	Push(std::move(pData));
}

void CStorjControlSocket::RemoveDir(CServerPath const& path, std::wstring const& subDir)
{
	log(logmsg::debug_verbose, L"CStorjControlSocket::RemoveDir");

	auto pData = std::make_unique<CStorjRemoveDirOpData>(*this);
	pData->path_ = path;
	if (!subDir.empty()) {
		pData->path_.ChangePath(subDir);
	}
	Push(std::move(pData));
}

void CStorjControlSocket::OnStorjEvent(storj_message const& message)
{
	if (!currentServer_) {
		return;
	}

	if (!input_thread_) {
		return;
	}

	switch (message.type)
	{
	case storjEvent::Reply:
		log_raw(logmsg::reply, message.text[0]);
		ProcessReply(FZ_REPLY_OK, message.text[0]);
		break;
	case storjEvent::Done:
		ProcessReply(FZ_REPLY_OK, std::wstring());
		break;
	case storjEvent::Error:
		log_raw(logmsg::error, message.text[0]);
		ProcessReply(FZ_REPLY_ERROR, message.text[0]);
		break;
	case storjEvent::ErrorMsg:
		log_raw(logmsg::error, message.text[0]);
		break;
	case storjEvent::Verbose:
		log_raw(logmsg::debug_info, message.text[0]);
		break;
	case storjEvent::Info:
		log_raw(logmsg::command, message.text[0]); // Not exactly the right message type, but it's a silent one.
		break;
	case storjEvent::Status:
		log_raw(logmsg::status, message.text[0]);
		break;
	case storjEvent::Recv:
		SetActive(CFileZillaEngine::recv);
		break;
	case storjEvent::Send:
		SetActive(CFileZillaEngine::send);
		break;
	case storjEvent::Listentry:
		if (operations_.empty() || operations_.back()->opId != Command::list) {
			log(logmsg::debug_warning, L"storjEvent::Listentry outside list operation, ignoring.");
			break;
		}
		else {
			int res = static_cast<CStorjListOpData&>(*operations_.back()).ParseEntry(std::move(message.text[0]), message.text[1], std::move(message.text[2]), message.text[3]);
			if (res != FZ_REPLY_WOULDBLOCK) {
				ResetOperation(res);
			}
		}
		break;
	case storjEvent::Transfer:
		{
			auto value = fz::to_integral<int64_t>(message.text[0]);

			if (!operations_.empty() && operations_.back()->opId == Command::transfer) {
				auto & data = static_cast<CStorjFileTransferOpData &>(*operations_.back());

				SetActive(data.download_ ? CFileZillaEngine::recv : CFileZillaEngine::send);

				bool tmp;
				CTransferStatus status = engine_.transfer_status_.Get(tmp);
				if (!status.empty() && !status.madeProgress) {
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
	default:
		log(logmsg::debug_warning, L"Message type %d not handled", message.type);
		break;
	}
}

void CStorjControlSocket::OnTerminate(std::wstring const& error)
{
	if (!error.empty()) {
		log_raw(logmsg::error, error);
	}
	else {
		log_raw(logmsg::debug_info, L"CStorjControlSocket::OnTerminate without error");
	}
	if (process_) {
		DoClose();
	}
}

int CStorjControlSocket::SendCommand(std::wstring const& cmd, std::wstring const& show)
{
	if (cmd.substr(0, 4) != L"get " && cmd.substr(0, 4) != L"put ") {
		SetWait(true);
	}

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

int CStorjControlSocket::AddToStream(std::wstring const& cmd)
{
	if (!process_) {
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return false;
	}

	std::string const str = ConvToServer(cmd, true);
	if (str.empty()) {
		log(logmsg::error, _("Could not convert command to server encoding"));
		return FZ_REPLY_ERROR;
	}

	if (!process_->write(str)) {
		return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
	}

	return FZ_REPLY_WOULDBLOCK;
}

bool CStorjControlSocket::SetAsyncRequestReply(CAsyncRequestNotification *pNotification)
{
	log(logmsg::debug_verbose, L"CStorjControlSocket::SetAsyncRequestReply");

	RequestId const requestId = pNotification->GetRequestID();
	switch(requestId)
	{
	case reqId_fileexists:
		{
			CFileExistsNotification *pFileExistsNotification = static_cast<CFileExistsNotification *>(pNotification);
			return SetFileExistsAction(pFileExistsNotification);
		}
	default:
		log(logmsg::debug_warning, L"Unknown async request reply id: %d", requestId);
		return false;
	}

	return true;
}

void CStorjControlSocket::ProcessReply(int result, std::wstring const& reply)
{
	result_ = result;
	response_ = reply;

	SetWait(false);

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

int CStorjControlSocket::ResetOperation(int nErrorCode)
{
	if (!operations_.empty() && operations_.back()->opId == Command::connect) {
		auto &data = static_cast<CStorjConnectOpData &>(*operations_.back());
		if (data.opState == connect_init && nErrorCode & FZ_REPLY_ERROR && (nErrorCode & FZ_REPLY_CANCELED) != FZ_REPLY_CANCELED) {
			log(logmsg::error, _("fzstorj could not be started"));
		}
	}
	if (!operations_.empty() && operations_.back()->opId == Command::del && !(nErrorCode & FZ_REPLY_DISCONNECTED)) {
		auto &data = static_cast<CStorjDeleteOpData &>(*operations_.back());
		if (data.needSendListing_) {
			SendDirectoryListingNotification(data.path_, false);
		}
	}

	return CControlSocket::ResetOperation(nErrorCode);
}

int CStorjControlSocket::DoClose(int nErrorCode)
{
	if (process_) {
		process_->kill();
	}

	if (input_thread_) {
		input_thread_.reset();

		auto threadEventsFilter = [&](fz::event_loop::Events::value_type const& ev) -> bool {
			if (ev.first != this) {
				return false;
			}
			else if (ev.second->derived_type() == CStorjEvent::type() || ev.second->derived_type() == StorjTerminateEvent::type()) {
				return true;
			}
			return false;
		};

		event_loop_.filter_events(threadEventsFilter);
	}
	process_.reset();
	return CControlSocket::DoClose(nErrorCode);
}

void CStorjControlSocket::Cancel()
{
	if (GetCurrentCommandId() != Command::none) {
		DoClose(FZ_REPLY_CANCELED);
	}
}

void CStorjControlSocket::operator()(fz::event_base const& ev)
{
	if (fz::dispatch<CStorjEvent, StorjTerminateEvent>(ev, this,
		&CStorjControlSocket::OnStorjEvent,
		&CStorjControlSocket::OnTerminate)) {
		return;
	}

	CControlSocket::operator()(ev);
}

std::wstring CStorjControlSocket::QuoteFilename(std::wstring const& filename)
{
	return L"\"" + fz::replaced_substrings(filename, L"\"", L"\"\"") + L"\"";
}

void CStorjControlSocket::Push(std::unique_ptr<COpData> && pNewOpData)
{
	CControlSocket::Push(std::move(pNewOpData));
	if (operations_.size() == 1 && operations_.back()->opId != Command::connect) {
		if (!process_) {
			std::unique_ptr<COpData> connOp = std::make_unique<CStorjConnectOpData>(*this);
			connOp->topLevelOperation_ = true;
			CControlSocket::Push(std::move(connOp));
		}
	}
}
