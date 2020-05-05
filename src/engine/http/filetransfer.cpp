#include <filezilla.h>

#include "filetransfer.h"

#include <libfilezilla/local_filesys.hpp>

#include <assert.h>
#include <string.h>

enum filetransferStates
{
	filetransfer_init = 0,
	filetransfer_waitfileexists,
	filetransfer_transfer,
	filetransfer_waittransfer
};

CHttpFileTransferOpData::CHttpFileTransferOpData(CHttpControlSocket & controlSocket, bool is_download, std::wstring const& local_file, std::wstring const& remote_file, CServerPath const& remote_path, CFileTransferCommand::t_transferSettings const& settings)
	: CFileTransferOpData(L"CHttpFileTransferOpData", is_download, local_file, remote_file, remote_path, settings)
	, CHttpOpData(controlSocket)
{
	rr_.request_.uri_ = fz::uri(fz::to_utf8(currentServer_.Format(ServerFormat::url)) + fz::percent_encode(fz::to_utf8(remotePath_.FormatFilename(remoteFile_)), true));
	rr_.request_.verb_ = "GET";

}

CHttpFileTransferOpData::CHttpFileTransferOpData(CHttpControlSocket & controlSocket, fz::uri const& uri, std::string const& verb, std::string const& body)
	: CFileTransferOpData(L"CHttpFileTransferOpData", true, std::wstring(), std::wstring(), CServerPath(), CFileTransferCommand::t_transferSettings())
	, CHttpOpData(controlSocket)
{
	rr_.request_.uri_ = uri;
	rr_.request_.body_ = std::make_unique<simple_body>(body);
	rr_.request_.verb_ = verb;
}


int CHttpFileTransferOpData::Send()
{
	switch (opState) {
	case filetransfer_init:
		if (!download_) {
			return FZ_REPLY_NOTSUPPORTED;
		}

		if (rr_.request_.uri_.empty()) {
			log(logmsg::error, _("Could not create URI for this transfer."));
			return FZ_REPLY_ERROR;
		}

		opState = filetransfer_waitfileexists;
		if (!localFile_.empty()) {
			localFileSize_ = fz::local_filesys::get_size(fz::to_native(localFile_));

			int res = controlSocket_.CheckOverwriteFile();
			if (res != FZ_REPLY_OK) {
				return res;
			}
		}
		return FZ_REPLY_CONTINUE;
	case filetransfer_waitfileexists:
		if (!localFile_.empty()) {
			int res = OpenFile();
			if (res != FZ_REPLY_OK) {
				return res;
			}
		}
		opState = filetransfer_transfer;
		return FZ_REPLY_CONTINUE;
	case filetransfer_transfer:
		if (resume_) {
			rr_.request_.headers_["Range"] = fz::sprintf("bytes=%d-", localFileSize_);
		}

		rr_.response_ = HttpResponse();
		rr_.response_.on_header_ = [this](auto const&) { return this->OnHeader(); };
		rr_.response_.on_data_ = [this](auto data, auto len) { return this->OnData(data, len); };

		opState = filetransfer_waittransfer;
		controlSocket_.Request(make_simple_rr(&rr_));
		return FZ_REPLY_CONTINUE;
	default:
		break;
	}

	return FZ_REPLY_INTERNALERROR;
}

int CHttpFileTransferOpData::OpenFile()
{
	log(logmsg::debug_verbose, L"CHttpFileTransferOpData::OpenFile");
	if (file_.opened()) {
		if (transferSettings_.fsync) {
			file_.fsync();
		}
		file_.close();
	}

	controlSocket_.CreateLocalDir(localFile_);

	if (!file_.open(fz::to_native(localFile_),
		download_ ? fz::file::writing : fz::file::reading,
		fz::file::existing))
	{
		log(logmsg::error, _("Failed to open \"%s\" for writing"), localFile_);
		return FZ_REPLY_ERROR;
	}

	assert(download_);
	int64_t end = file_.seek(0, fz::file::end);
	if (end < 0) {
		log(logmsg::error, _("Could not seek to the end of the file"));
		return FZ_REPLY_ERROR;
	}
	if (!end) {
		resume_ = false;
	}
	localFileSize_ = fz::local_filesys::get_size(fz::to_native(localFile_));
	return FZ_REPLY_OK;
}

int CHttpFileTransferOpData::OnHeader()
{
	log(logmsg::debug_verbose, L"CHttpFileTransferOpData::OnHeader");

	if (rr_.response_.code_ == 416 && resume_) {
		assert(file_.opened());
		if (file_.seek(0, fz::file::begin) != 0) {
			log(logmsg::error, _("Could not seek to the beginning of the file"));
			return FZ_REPLY_ERROR;
		}
		resume_ = false;

		opState = filetransfer_transfer;
		return FZ_REPLY_ERROR;
	}

	if (rr_.response_.code_ < 200 || rr_.response_.code_ >= 400) {
		return FZ_REPLY_ERROR;
	}

	// Handle any redirects
	if (rr_.response_.code_ >= 300) {

		if (++redirectCount_ >= 6) {
			log(logmsg::error, _("Too many redirects"));
			return FZ_REPLY_ERROR;
		}

		if (rr_.response_.code_ == 305) {
			log(logmsg::error, _("Unsupported redirect"));
			return FZ_REPLY_ERROR;
		}

		fz::uri location = fz::uri(rr_.response_.get_header("Location"));
		if (!location.empty()) {
			location.resolve(rr_.request_.uri_);
		}
		
		if (location.scheme_.empty() || location.host_.empty() || !location.is_absolute()) {
			log(logmsg::error, _("Redirection to invalid or unsupported URI: %s"), location.to_string());
			return FZ_REPLY_ERROR;
		}

		ServerProtocol protocol = CServer::GetProtocolFromPrefix(fz::to_wstring_from_utf8(location.scheme_));
		if (protocol != HTTP && protocol != HTTPS) {
			log(logmsg::error, _("Redirection to invalid or unsupported address: %s"), location.to_string());
			return FZ_REPLY_ERROR;
		}

		// International domain names
		std::wstring host = fz::to_wstring_from_utf8(location.host_);
		if (host.empty()) {
			log(logmsg::error, _("Invalid hostname: %s"), location.to_string());
			return FZ_REPLY_ERROR;
		}

		rr_.request_.uri_ = location;

		opState = filetransfer_transfer;
		return FZ_REPLY_OK;
	}

	// Check if the server disallowed resume
	if (resume_ && rr_.response_.code_ != 206) {
		assert(file_.opened());
		if (file_.seek(0, fz::file::begin) != 0) {
			log(logmsg::error, _("Could not seek to the beginning of the file"));
			return FZ_REPLY_ERROR;
		}
		resume_ = false;
	}

	int64_t totalSize = fz::to_integral<int64_t>(rr_.response_.get_header("Content-Length"), -1);
	if (totalSize == -1) {
		if (remoteFileSize_ != -1) {
			totalSize = remoteFileSize_;
		}
	}

	if (engine_.transfer_status_.empty()) {
		engine_.transfer_status_.Init(totalSize, resume_ ? localFileSize_ : 0, false);
		engine_.transfer_status_.SetStartTime();
	}

	return FZ_REPLY_CONTINUE;
}

int CHttpFileTransferOpData::OnData(unsigned char const* data, unsigned int len)
{
	if (opState != filetransfer_waittransfer) {
		return FZ_REPLY_INTERNALERROR;
	}

	if (localFile_.empty()) {
		char* q = new char[len];
		memcpy(q, data, len);
		engine_.AddNotification(new CDataNotification(q, len));
	}
	else {
		assert(file_.opened());

		auto write = static_cast<int64_t>(len);
		if (file_.write(data, write) != write) {
			log(logmsg::error, _("Failed to write to file %s"), localFile_);
			return FZ_REPLY_ERROR;
		}
	}

	engine_.transfer_status_.Update(len);

	return FZ_REPLY_CONTINUE;
}

int CHttpFileTransferOpData::SubcommandResult(int prevResult, COpData const&)
{
	if (opState == filetransfer_transfer) {
		return FZ_REPLY_CONTINUE;
	}

	if (opState == filetransfer_waittransfer) {
		if (file_.opened()) {
			if (transferSettings_.fsync) {
				file_.fsync();
			}
		}
	}

	return prevResult;
}
