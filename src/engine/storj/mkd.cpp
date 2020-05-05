#include <filezilla.h>

#include "../directorycache.h"
#include "mkd.h"

enum mkdStates
{
	mkd_init = 0,
	mkd_mkbucket,
	mkd_resolve,
	mkd_put
};


int CStorjMkdirOpData::Send()
{
	switch (opState) {
	case mkd_init:
		if (path_.SegmentCount() < 1) {
			log(logmsg::error, _("Invalid path"));
			return FZ_REPLY_CRITICALERROR;
		}

		if (controlSocket_.operations_.size() == 1) {
			log(logmsg::status, _("Creating directory '%s'..."), path_.GetPath());
		}

		opState = mkd_mkbucket;
		return FZ_REPLY_CONTINUE;
	case mkd_mkbucket:
		return controlSocket_.SendCommand(L"mkbucket " + controlSocket_.QuoteFilename(path_.GetFirstSegment()));
	case mkd_put:
		{
			std::wstring path = path_.GetPath();
			auto pos = path.find('/', 1);
			if (pos == std::string::npos) {
				return FZ_REPLY_INTERNALERROR;
			}
			else {
				path = path.substr(pos + 1) + L"/";
			}
			return controlSocket_.SendCommand(L"put " + bucket_ + L" \"null\" " + controlSocket_.QuoteFilename(path));
		}
	}

	log(logmsg::debug_warning, L"Unknown opState in CStorjMkdirOpData::Send()");
	return FZ_REPLY_INTERNALERROR;
}

int CStorjMkdirOpData::ParseResponse()
{
	switch (opState) {
	case mkd_mkbucket:
		if (controlSocket_.result_ == FZ_REPLY_OK) {
			engine_.GetDirectoryCache().UpdateFile(currentServer_, CServerPath(L"/"), path_.GetFirstSegment(), true, CDirectoryCache::dir);
			controlSocket_.SendDirectoryListingNotification(CServerPath(L"/"), false);
		}

		if (path_.SegmentCount() > 1) {
			opState = mkd_resolve;
			controlSocket_.Resolve(path_, std::wstring(), bucket_, 0);
			return FZ_REPLY_CONTINUE;
		}
		else {
			return controlSocket_.result_;
		}
	case mkd_put:
		if (controlSocket_.result_ == FZ_REPLY_OK) {
			CServerPath path = path_;
			while (path.SegmentCount() > 1) {
				CServerPath parent = path.GetParent();
				engine_.GetDirectoryCache().UpdateFile(currentServer_, parent, path.GetLastSegment(), true, CDirectoryCache::dir);
				controlSocket_.SendDirectoryListingNotification(parent, false);
				path = parent;
			}
		}
		return controlSocket_.result_;
	}

	log(logmsg::debug_warning, L"Unknown opState in CStorjMkdirOpData::ParseResponse()");
	return FZ_REPLY_INTERNALERROR;
}

int CStorjMkdirOpData::SubcommandResult(int prevResult, COpData const&)
{
	if (prevResult != FZ_REPLY_OK) {
		return prevResult;
	}

	switch (opState) {
	case mkd_resolve:
		opState = mkd_put;
		return FZ_REPLY_CONTINUE;
	}

	log(logmsg::debug_warning, L"Unknown opState in CStorjMkdirOpData::SubcommandResult()");
	return FZ_REPLY_INTERNALERROR;
}
