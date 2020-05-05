#include <filezilla.h>

#include "delete.h"
#include "../directorycache.h"

enum rmdStates
{
	del_init,
	del_waitcwd,
	del_del
};

int CFtpDeleteOpData::Send()
{
	if (opState == del_init) {
		controlSocket_.ChangeDir(path_);
		opState = del_waitcwd;
		return FZ_REPLY_CONTINUE;
	}
	else if (opState == del_del) {
		std::wstring const& file = files_.back();
		if (file.empty()) {
			log(logmsg::debug_info, L"Empty filename");
			return FZ_REPLY_INTERNALERROR;
		}

		std::wstring filename = path_.FormatFilename(file, omitPath_);
		if (filename.empty()) {
			log(logmsg::error, _("Filename cannot be constructed for directory %s and filename %s"), path_.GetPath(), file);
			return FZ_REPLY_ERROR;
		}

		engine_.GetDirectoryCache().InvalidateFile(currentServer_, path_, file);

		return controlSocket_.SendCommand(L"DELE " + filename);
	}

	log(logmsg::debug_warning, L"Unkown op state %d", opState);
	return FZ_REPLY_INTERNALERROR;
}

int CFtpDeleteOpData::ParseResponse()
{
	int code = controlSocket_.GetReplyCode();
	if (code != 2 && code != 3) {
		deleteFailed_ = true;
	}
	else {
		std::wstring const& file = files_.back();

		engine_.GetDirectoryCache().RemoveFile(currentServer_, path_, file);

		auto now = fz::monotonic_clock::now();
		if (time_ && (now - time_).get_seconds() >= 1) {
			controlSocket_.SendDirectoryListingNotification(path_, false);
			time_ = now;
			needSendListing_ = false;
		}
		else {
			needSendListing_ = true;
		}
	}

	files_.pop_back();

	if (!files_.empty()) {
		return FZ_REPLY_CONTINUE;
	}

	return deleteFailed_ ? FZ_REPLY_ERROR : FZ_REPLY_OK;
}

int CFtpDeleteOpData::SubcommandResult(int prevResult, COpData const&)
{
	if (opState == del_waitcwd) {
		opState = del_del;

		if (prevResult != FZ_REPLY_OK) {
			omitPath_ = false;
		}

		time_ = fz::monotonic_clock::now();
		return FZ_REPLY_CONTINUE;
	}
	else {
		return FZ_REPLY_INTERNALERROR;
	}
}

int CFtpDeleteOpData::Reset(int result)
{
	if (needSendListing_ && !(result & FZ_REPLY_DISCONNECTED)) {
		controlSocket_.SendDirectoryListingNotification(path_, false);
	}
	return result;
}
