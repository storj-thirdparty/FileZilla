#include <filezilla.h>

#include "rawcommand.h"
#include "../directorycache.h"
#include "../pathcache.h"

int CFtpRawCommandOpData::Send()
{
	engine_.GetDirectoryCache().InvalidateServer(currentServer_);
	engine_.GetPathCache().InvalidateServer(currentServer_);
	currentPath_.clear();

	controlSocket_.m_lastTypeBinary = -1;

	return controlSocket_.SendCommand(command_, false, false);
}

int CFtpRawCommandOpData::ParseResponse()
{
	int code = controlSocket_.GetReplyCode();
	if (code == 2 || code == 3) {
		return FZ_REPLY_OK;
	}
	else {
		return FZ_REPLY_ERROR;
	}
}
