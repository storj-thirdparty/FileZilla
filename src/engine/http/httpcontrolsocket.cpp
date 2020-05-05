#include <filezilla.h>

#include "connect.h"
#include "controlsocket.h"
#include "engineprivate.h"
#include "filetransfer.h"
#include "httpcontrolsocket.h"
#include "internalconnect.h"
#include "request.h"

#include <libfilezilla/file.hpp>
#include <libfilezilla/iputils.hpp>
#include <libfilezilla/local_filesys.hpp>
#include <libfilezilla/tls_layer.hpp>
#include <libfilezilla/uri.hpp>

#include <assert.h>
#include <string.h>

int simple_body::data_request(unsigned char* data, unsigned int & len)
{
	len = static_cast<unsigned int>(std::min(static_cast<size_t>(len), body_.size() - written_));
	memcpy(data, body_.c_str() + written_, len);
	written_ += len;
	return FZ_REPLY_CONTINUE;
}


file_body::file_body(fz::file & file, uint64_t start, uint64_t size, fz::logger_interface & logger)
	: file_(file)
	, start_(start)
	, size_(size)
	, logger_(logger)
{
}

int file_body::data_request(unsigned char* data, unsigned int & len)
{
	assert(size_ >= written_);
	assert(len > 0);
	len = static_cast<unsigned int>(std::min(static_cast<uint64_t>(len), size_ - written_));
	if (!len) {
		return FZ_REPLY_CONTINUE;
	}
	auto bytes_read = file_.read(data, len);
	if (bytes_read < 0) {
		len = 0;
		logger_.log(logmsg::error, _("Reading from local file failed"));
		return FZ_REPLY_ERROR;
	}
	else if (bytes_read == 0) {
		len = 0;
		return FZ_REPLY_ERROR;
	}

	if (progress_callback_) {
		progress_callback_(bytes_read);
	}

	len = static_cast<unsigned int>(bytes_read);
	written_ += len;
	return FZ_REPLY_CONTINUE;
}

int file_body::rewind()
{
	if (progress_callback_) {
		progress_callback_(-static_cast<int64_t>(written_));
	}
	written_ = 0;

	int64_t s = static_cast<int64_t>(start_);
	if (file_.seek(s, fz::file::begin) != s) {
		if (!start_) {
			logger_.log(logmsg::error, _("Could not seek to the beginning of the file"));
		}
		else {
			logger_.log(logmsg::error, _("Could not seek to offset %d within file"), start_);
		}
		return FZ_REPLY_ERROR;
	}

	return FZ_REPLY_CONTINUE;
}


int HttpRequest::reset()
{
	flags_ = 0;

	if (body_) {
		int res = body_->rewind();
		if (res != FZ_REPLY_CONTINUE) {
			return res;
		}
	}
	return FZ_REPLY_CONTINUE;
}

int HttpResponse::reset()
{
	flags_ = 0;
	code_ = 0;
	headers_.clear();

	return FZ_REPLY_CONTINUE;
}

void RequestThrottler::throttle(std::string const& hostname, fz::datetime const& backoff)
{
	if (hostname.empty() || !backoff) {
		return;
	}

	fz::scoped_lock l(mtx_);

	bool found{};
	auto now = fz::datetime::now();
	for (size_t i = 0; i < backoff_.size(); ) {
		auto & entry = backoff_[i];
		if (entry.first == hostname) {
			found = true;
			if (entry.second < backoff) {
				entry.second = backoff;
			}
		}
		if (entry.second < now) {
			backoff_[i] = std::move(backoff_.back());
			backoff_.pop_back();
		}
		else {
			++i;
		}
	}
	if (!found) {
		backoff_.emplace_back(hostname, backoff);
	}
}

fz::duration RequestThrottler::get_throttle(std::string const& hostname)
{
	fz::scoped_lock l(mtx_);

	fz::duration ret;

	auto now = fz::datetime::now();
	for (size_t i = 0; i < backoff_.size(); ) {
		auto & entry = backoff_[i];
		if (entry.second < now) {
			backoff_[i] = std::move(backoff_.back());
			backoff_.pop_back();
		}
		else {
			if (entry.first == hostname) {
				ret = entry.second - now;
			}
			++i;
		}
	}

	return ret;
}

RequestThrottler CHttpControlSocket::throttler_;

CHttpControlSocket::CHttpControlSocket(CFileZillaEnginePrivate & engine)
	: CRealControlSocket(engine)
{
}

CHttpControlSocket::~CHttpControlSocket()
{
	remove_handler();
	DoClose();
}

bool CHttpControlSocket::SetAsyncRequestReply(CAsyncRequestNotification *pNotification)
{
	log(logmsg::debug_verbose, L"CHttpControlSocket::SetAsyncRequestReply");

	switch (pNotification->GetRequestID())
	{
	case reqId_fileexists:
		{
			if (operations_.back()->opId != Command::transfer) {
				log(logmsg::debug_info, L"No or invalid operation in progress, ignoring request reply %f", pNotification->GetRequestID());
				return false;
			}

			CFileExistsNotification *pFileExistsNotification = static_cast<CFileExistsNotification *>(pNotification);
			return SetFileExistsAction(pFileExistsNotification);
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
		}
		break;
	default:
		log(logmsg::debug_warning, L"Unknown request %d", pNotification->GetRequestID());
		ResetOperation(FZ_REPLY_INTERNALERROR);
		return false;
	}

	return true;
}


void CHttpControlSocket::OnReceive()
{
	if (operations_.empty() || operations_.back()->opId != PrivCommand::http_request) {
		uint8_t buffer;
		int error{};
		int read = active_layer_->read(&buffer, 1, error);
		if (!read) {
			log(logmsg::debug_warning, L"Idle socket got closed");
			ResetSocket();
		}
		else if (read == -1) {
			if (error != EAGAIN) {
				log(logmsg::debug_warning, L"OnReceive called while not processing http request. Reading fails with error %d, closing socket.", error);
				ResetSocket();
			}
		}
		else if (read) {
			log(logmsg::debug_warning, L"Server sent data while not in an active HTTP request, closing socket.");
			ResetSocket();
		}
		return;
	}

	int res = static_cast<CHttpRequestOpData&>(*operations_.back()).OnReceive();
	if (res == FZ_REPLY_CONTINUE) {
		SendNextCommand();
	}
	else if (res != FZ_REPLY_WOULDBLOCK) {
		ResetOperation(res);
	}
}

void CHttpControlSocket::OnConnect()
{
	if (operations_.empty() || operations_.back()->opId != PrivCommand::http_connect) {
		log(logmsg::debug_warning, L"Discarding stale OnConnect");
		return;
	}

	socket_->set_flags(fz::socket::flag_nodelay, true);

	auto & data = static_cast<CHttpInternalConnectOpData &>(*operations_.back());

	if (data.tls_) {
		if (!tls_layer_) {
			log(logmsg::status, _("Connection established, initializing TLS..."));

			tls_layer_ = std::make_unique<fz::tls_layer>(event_loop_, this, *active_layer_, &engine_.GetContext().GetTlsSystemTrustStore(), logger_);
			active_layer_ = tls_layer_.get();

			if (!tls_layer_->client_handshake(&data)) {
				DoClose();
			}
		}
		else {
			log(logmsg::status, _("TLS connection established, sending HTTP request"));
			ResetOperation(FZ_REPLY_OK);
		}
	}
	else {
		log(logmsg::status, _("Connection established, sending HTTP request"));
		ResetOperation(FZ_REPLY_OK);
	}
}

void CHttpControlSocket::FileTransfer(std::wstring const& localFile, CServerPath const& remotePath,
									std::wstring const& remoteFile, bool download,
									CFileTransferCommand::t_transferSettings const& settings)
{
	log(logmsg::debug_verbose, L"CHttpControlSocket::FileTransfer()");

	if (download) {
		log(logmsg::status, _("Downloading %s"), remotePath.FormatFilename(remoteFile));
	}

	Push(std::make_unique<CHttpFileTransferOpData>(*this, download, localFile, remoteFile, remotePath, settings));
}

void CHttpControlSocket::FileTransfer(CHttpRequestCommand const& command)
{
	log(logmsg::debug_verbose, L"CHttpControlSocket::FileTransfer()");

	log(logmsg::status, _("Requesting %s"), command.uri_.to_string());

	Push(std::make_unique<CHttpFileTransferOpData>(*this, command.uri_, command.verb_, command.body_));
}

void CHttpControlSocket::Request(std::shared_ptr<HttpRequestResponseInterface> const& request)
{
	log(logmsg::debug_verbose, L"CHttpControlSocket::Request()");

	auto op = dynamic_cast<CHttpRequestOpData*>(operations_.empty() ? nullptr : operations_.back().get());
	if (op) {
		op->AddRequest(request);
	}
	else {
		Push(std::make_unique<CHttpRequestOpData>(*this, request));
	}
}

void CHttpControlSocket::Request(std::deque<std::shared_ptr<HttpRequestResponseInterface>> && requests)
{
	log(logmsg::debug_verbose, L"CHttpControlSocket::Request()");
	Push(std::make_unique<CHttpRequestOpData>(*this, std::move(requests)));
}

int CHttpControlSocket::InternalConnect(std::wstring const& host, unsigned short port, bool tls, bool allowDisconnect)
{
	log(logmsg::debug_verbose, L"CHttpControlSocket::InternalConnect()");

	if (!Connected()) {
		return FZ_REPLY_INTERNALERROR;
	}

	if (active_layer_) {
		if (host == connected_host_ && port == connected_port_ && tls == connected_tls_) {
			log(logmsg::debug_verbose, L"Reusing an existing connection");
			return FZ_REPLY_OK;
		}
		if (!allowDisconnect) {
			return FZ_REPLY_WOULDBLOCK;
		}
	}

	ResetSocket();
	connected_host_ = host;
	connected_port_ = port;
	connected_tls_ = tls;
	Push(std::make_unique<CHttpInternalConnectOpData>(*this, ConvertDomainName(host), port, tls));

	return FZ_REPLY_CONTINUE;
}

void CHttpControlSocket::OnSocketError(int error)
{
	log(logmsg::debug_verbose, L"CHttpControlSocket::OnClose(%d)", error);

	if (operations_.empty() || (operations_.back()->opId != PrivCommand::http_connect && operations_.back()->opId != PrivCommand::http_request)) {
		log(logmsg::debug_warning, L"Idle socket got closed");
		ResetSocket();
		return;
	}

	log(logmsg::error, _("Disconnected from server: %s"), fz::socket_error_description(error));
	ResetOperation(FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED);
}

void CHttpControlSocket::ResetSocket()
{
	log(logmsg::debug_verbose, L"CHttpControlSocket::ResetSocket()");

	active_layer_ = nullptr;

	tls_layer_.reset();

	CRealControlSocket::ResetSocket();
}

int CHttpControlSocket::Disconnect()
{
	DoClose();
	return FZ_REPLY_OK;
}

void CHttpControlSocket::Connect(CServer const& server, Credentials const& credentials)
{
	currentServer_ = server;
	credentials_ = credentials;
	Push(std::make_unique<CHttpConnectOpData>(*this));
}

int CHttpControlSocket::OnSend()
{
	int res = CRealControlSocket::OnSend();
	if (res == FZ_REPLY_CONTINUE) {
		if (!operations_.empty() && operations_.back()->opId == PrivCommand::http_request && (operations_.back()->opState & request_send_mask)) {
			return SendNextCommand();
		}
	}
	return res;
}
