#include <filezilla.h>

#include "../directorycache.h"
#include "pathcache.h"
#include "rmd.h"

int CSftpRemoveDirOpData::Send()
{
	CServerPath fullPath = engine_.GetPathCache().Lookup(currentServer_, path_, subDir_);
	if (fullPath.empty()) {
		fullPath = path_;

		if (!fullPath.AddSegment(subDir_)) {
			log(logmsg::error, _("Path cannot be constructed for directory %s and subdir %s"), path_.GetPath(), subDir_);
			return FZ_REPLY_ERROR;
		}
	}

	engine_.GetDirectoryCache().InvalidateFile(currentServer_, path_, subDir_);

	engine_.GetPathCache().InvalidatePath(currentServer_, path_, subDir_);

	engine_.InvalidateCurrentWorkingDirs(fullPath);
	std::wstring quotedFilename = controlSocket_.QuoteFilename(fullPath.GetPath());
	return controlSocket_.SendCommand(L"rmdir " + quotedFilename);
}

int CSftpRemoveDirOpData::ParseResponse()
{
	if (controlSocket_.result_ != FZ_REPLY_OK) {
		return controlSocket_.result_;
	}

	if (path_.empty()) {
		log(logmsg::debug_info, L"Empty pData->path");
		return FZ_REPLY_INTERNALERROR;
	}

	engine_.GetDirectoryCache().RemoveDir(currentServer_, path_, subDir_, engine_.GetPathCache().Lookup(currentServer_, path_, subDir_));
	controlSocket_.SendDirectoryListingNotification(path_, false);

	return FZ_REPLY_OK;
}