#include <filezilla.h>

#include "rmd.h"
#include "../directorycache.h"
#include "../pathcache.h"

enum rmdStates
{
	rmd_init,
	rmd_waitcwd,
	rmd_rmd
};

int CFtpRemoveDirOpData::Send()
{
	if (opState == rmd_init) {
		controlSocket_.ChangeDir(path_);
		opState = rmd_waitcwd;
		return FZ_REPLY_CONTINUE;
	}
	else if (opState == rmd_rmd) {
		CServerPath path(engine_.GetPathCache().Lookup(currentServer_, path_, subDir_));
		if (path.empty()) {
			path = path_;
			if (!path.AddSegment(subDir_)) {
				log(logmsg::error, _("Path cannot be constructed for directory %s and subdir %s"), path_.GetPath(), subDir_);
				return FZ_REPLY_ERROR;
			}
		}

		engine_.GetDirectoryCache().InvalidateFile(currentServer_, path_, subDir_);

		engine_.GetPathCache().InvalidatePath(currentServer_, path_, subDir_);

		engine_.InvalidateCurrentWorkingDirs(path);

		if (omitPath_) {
			return controlSocket_.SendCommand(L"RMD " + subDir_);
		}
		else {
			if (!fullPath_.AddSegment(subDir_)) {
				log(logmsg::error, _("Path cannot be constructed for directory %s and subdir %s"), path_.GetPath(), subDir_);
				return FZ_REPLY_ERROR;
			}

			return controlSocket_.SendCommand(L"RMD " + fullPath_.GetPath());
		}
	}

	log(logmsg::debug_warning, L"Unkown op state %d", opState);
	return FZ_REPLY_INTERNALERROR;
}

int CFtpRemoveDirOpData::ParseResponse()
{
	int code = controlSocket_.GetReplyCode();
	if (code != 2 && code != 3) {
		return FZ_REPLY_ERROR;
	}

	engine_.GetDirectoryCache().RemoveDir(currentServer_, path_, subDir_, engine_.GetPathCache().Lookup(currentServer_, path_, subDir_));
	controlSocket_.SendDirectoryListingNotification(path_, false);

	return FZ_REPLY_OK;
}

int CFtpRemoveDirOpData::SubcommandResult(int prevResult, COpData const&)
{
	if (opState == rmd_waitcwd) {
		if (prevResult != FZ_REPLY_OK) {
			omitPath_ = false;
		}
		else {
			path_ = currentPath_;
		}
		opState = rmd_rmd;
		return FZ_REPLY_CONTINUE;
	}
	else {
		return FZ_REPLY_INTERNALERROR;
	}
}
