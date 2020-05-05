#include <filezilla.h>

#include "../directorycache.h"
#include "pathcache.h"
#include "rename.h"

enum renameStates
{
	rename_init,
	rename_waitcwd,
	rename_rename
};

int CSftpRenameOpData::Send()
{
	switch (opState)
	{
	case rename_init:
		log(logmsg::status, _("Renaming '%s' to '%s'"), command_.GetFromPath().FormatFilename(command_.GetFromFile()), command_.GetToPath().FormatFilename(command_.GetToFile()));
		controlSocket_.ChangeDir(command_.GetFromPath());
		opState = rename_waitcwd;
		return FZ_REPLY_CONTINUE;
	case rename_rename:
	{
		engine_.GetDirectoryCache().InvalidateFile(currentServer_, command_.GetFromPath(), command_.GetFromFile());
		engine_.GetDirectoryCache().InvalidateFile(currentServer_, command_.GetToPath(), command_.GetToFile());

		std::wstring fromQuoted = controlSocket_.QuoteFilename(command_.GetFromPath().FormatFilename(command_.GetFromFile(), !useAbsolute_));
		std::wstring toQuoted = controlSocket_.QuoteFilename(command_.GetToPath().FormatFilename(command_.GetToFile(), !useAbsolute_ && command_.GetFromPath() == command_.GetToPath()));

		engine_.GetPathCache().InvalidatePath(currentServer_, command_.GetFromPath(), command_.GetFromFile());
		engine_.GetPathCache().InvalidatePath(currentServer_, command_.GetToPath(), command_.GetToFile());

		// Need to invalidate current working directories
		CServerPath path = engine_.GetPathCache().Lookup(currentServer_, command_.GetFromPath(), command_.GetFromFile());
		if (path.empty()) {
			path = command_.GetFromPath();
			path.AddSegment(command_.GetFromFile());
		}
		engine_.InvalidateCurrentWorkingDirs(path);

		return controlSocket_.SendCommand(L"mv " + fromQuoted + L" " + toQuoted);
	}
	default:
		log(logmsg::debug_warning, L"unknown op state: %d", opState);
		break;
	}

	return FZ_REPLY_INTERNALERROR;
}

int CSftpRenameOpData::ParseResponse()
{
	if (controlSocket_.result_ != FZ_REPLY_OK) {
		return controlSocket_.result_;
	}

	const CServerPath& fromPath = command_.GetFromPath();
	const CServerPath& toPath = command_.GetToPath();

	engine_.GetDirectoryCache().Rename(currentServer_, fromPath, command_.GetFromFile(), toPath, command_.GetToFile());

	controlSocket_.SendDirectoryListingNotification(fromPath, false);
	if (fromPath != toPath) {
		controlSocket_.SendDirectoryListingNotification(toPath, false);
	}

	return FZ_REPLY_OK;
}

int CSftpRenameOpData::SubcommandResult(int prevResult, COpData const&)
{
	if (prevResult != FZ_REPLY_OK) {
		useAbsolute_ = true;
	}

	opState = rename_rename;
	return FZ_REPLY_CONTINUE;
}
