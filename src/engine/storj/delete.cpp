#include <filezilla.h>

#include "../directorycache.h"
#include "delete.h"

enum DeleteStates
{
	delete_init,
	delete_resolve,
	delete_delete
};

int CStorjDeleteOpData::Send()
{
	switch (opState) {
	case delete_init:
		if (files_.empty()) {
			return FZ_REPLY_CRITICALERROR;
		}

		opState = delete_resolve;
		return FZ_REPLY_CONTINUE;
	case delete_resolve:
		opState = delete_resolve;
		controlSocket_.Resolve(path_, files_, bucket_, fileIds_);
		return FZ_REPLY_CONTINUE;
	case delete_delete:
		if (files_.empty()) {
			return deleteFailed_ ? FZ_REPLY_ERROR : FZ_REPLY_OK;
		}

		std::wstring const& file = files_.back();
		std::wstring const& id = fileIds_.back();
		if (id.empty()) {
			files_.pop_back();
			fileIds_.pop_back();
			return FZ_REPLY_CONTINUE;
		}

		if (time_.empty()) {
			time_ = fz::datetime::now();
		}

		engine_.GetDirectoryCache().InvalidateFile(currentServer_, path_, file);

		return controlSocket_.SendCommand(L"rm " + bucket_ + L" " + id);
	}

	log(logmsg::debug_warning, L"Unknown opState in CStorjDeleteOpData::Send()");
	return FZ_REPLY_INTERNALERROR;
}

int CStorjDeleteOpData::ParseResponse()
{
	if (controlSocket_.result_ != FZ_REPLY_OK) {
		deleteFailed_ = true;
	}
	else {
		std::wstring const& file = files_.back();

		engine_.GetDirectoryCache().RemoveFile(currentServer_, path_, file);

		auto const now = fz::datetime::now();
		if (!time_.empty() && (now - time_).get_seconds() >= 1) {
			controlSocket_.SendDirectoryListingNotification(path_, false);
			time_ = now;
			needSendListing_ = false;
		}
		else {
			needSendListing_ = true;
		}
	}

	files_.pop_back();
	fileIds_.pop_back();

	if (!files_.empty()) {
		return FZ_REPLY_CONTINUE;
	}

	return deleteFailed_ ? FZ_REPLY_ERROR : FZ_REPLY_OK;
}

int CStorjDeleteOpData::SubcommandResult(int prevResult, COpData const&)
{
	if (prevResult != FZ_REPLY_OK) {
		return prevResult;
	}

	if (files_.size() != fileIds_.size()) {
		return FZ_REPLY_INTERNALERROR;
	}

	opState = delete_delete;
	return FZ_REPLY_CONTINUE;
}
