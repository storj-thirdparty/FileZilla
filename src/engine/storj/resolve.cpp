#include <filezilla.h>

#include "../directorycache.h"
#include "resolve.h"

enum resolveStates
{
	resolve_init = 0,
	resolve_waitlistbuckets,
	resolve_id,
	resolve_waitlist
};

namespace {
std::wstring ExtractId(std::wstring const& metaData)
{
	std::wstring ret;

	auto begin = metaData.find(L"id:");
	if (begin != std::wstring::npos) {
		auto end = metaData.find(' ', begin);
		if (end == std::wstring::npos) {
			ret = metaData.substr(begin + 3);
		}
		else {
			ret = metaData.substr(begin + 3, end - begin - 3);
		}
	}
	return ret;
}
}

int CStorjResolveOpData::Send()
{
	switch (opState) {
	case resolve_init:
		bucket_.clear();
		if (fileId_) {
			fileId_->clear();
		}

		if (path_.empty()) {
			return FZ_REPLY_INTERNALERROR;
		}
		else if (!path_.SegmentCount()) {
			// It's the root, nothing to resolve here.

			if (fileId_ || !file_.empty()) {
				return FZ_REPLY_INTERNALERROR;
			}

			return FZ_REPLY_OK;
		}
		else {
			CDirectoryListing buckets;

			bool outdated{};
			bool found = engine_.GetDirectoryCache().Lookup(buckets, currentServer_, CServerPath(L"/"), false, outdated);
			if (found && !outdated) {
				size_t pos = buckets.FindFile_CmpCase(path_.GetFirstSegment());
				if (pos != std::string::npos) {
					bucket_ = ExtractId(*buckets[pos].ownerGroup);
					log(logmsg::debug_info, L"Directory is in bucket %s", bucket_);
					opState = resolve_id;
					return FZ_REPLY_CONTINUE;
				}
				else {
					log(logmsg::error, _("Bucket not found"));
					return FZ_REPLY_ERROR;
				}
			}

			opState = resolve_waitlistbuckets;
			controlSocket_.List(CServerPath(L"/"), std::wstring(), 0);

			return FZ_REPLY_CONTINUE;
		}
		break;
	case resolve_id:
		if (!fileId_) {
			return FZ_REPLY_OK;
		}
		else {
			if (file_.empty()) {
				return FZ_REPLY_INTERNALERROR;
			}
			CDirectoryListing listing;

			bool outdated{};
			bool found = engine_.GetDirectoryCache().Lookup(listing, currentServer_, path_, ignore_missing_file_, outdated);
			if (found && !outdated) {
				size_t pos = listing.FindFile_CmpCase(file_);
				if (pos != std::string::npos) {
					*fileId_ = ExtractId(*listing[pos].ownerGroup);
					if (!fileId_->empty()) {
						log(logmsg::debug_info, L"File %s has id %s", path_.FormatFilename(file_), *fileId_);
						return FZ_REPLY_OK;
					}
					else {
						// Must refresh listing
						opState = resolve_waitlist;
						controlSocket_.List(path_, std::wstring(), 0);
						return FZ_REPLY_CONTINUE;
					}
				}

				if (ignore_missing_file_) {
					return FZ_REPLY_OK;
				}

				log(logmsg::error, _("File not found"));
				return FZ_REPLY_ERROR;
			}

			opState = resolve_waitlist;
			controlSocket_.List(path_, std::wstring(), 0);
			return FZ_REPLY_CONTINUE;
		}
		break;
	}

	log(logmsg::debug_warning, L"Unknown opState in CStorjResolveOpData::Send()");
	return FZ_REPLY_INTERNALERROR;
}

int CStorjResolveOpData::SubcommandResult(int prevResult, COpData const&)
{
	switch (opState) {
	case resolve_waitlistbuckets:
		if (prevResult != FZ_REPLY_OK) {
			return prevResult;
		}
		else {
			CDirectoryListing buckets;

			bool outdated{};
			bool found = engine_.GetDirectoryCache().Lookup(buckets, currentServer_, CServerPath(L"/"), false, outdated);
			if (found && !outdated) {
				size_t pos = buckets.FindFile_CmpCase(path_.GetFirstSegment());
				if (pos != std::string::npos) {
					bucket_ = ExtractId(*buckets[pos].ownerGroup);
					log(logmsg::debug_info, L"Directory is in bucket %s", bucket_);
					opState = resolve_id;
					return FZ_REPLY_CONTINUE;
				}
			}
		}
		log(logmsg::error, _("Bucket not found"));
		return FZ_REPLY_ERROR;
	case resolve_waitlist:
		if (prevResult != FZ_REPLY_OK) {
			if (ignore_missing_file_) {
				return FZ_REPLY_OK;
			}
			return prevResult;
		}
		else {
			CDirectoryListing listing;

			bool outdated{};
			bool found = engine_.GetDirectoryCache().Lookup(listing, currentServer_, path_, false, outdated);
			if (found && !outdated) {
				size_t pos = listing.FindFile_CmpCase(file_);
				if (pos != std::string::npos) {
					*fileId_ = ExtractId(*listing[pos].ownerGroup);
					log(logmsg::debug_info, L"File %s has id %s", path_.FormatFilename(file_), *fileId_);
					return FZ_REPLY_OK;
				}
				if (ignore_missing_file_) {
					return FZ_REPLY_OK;
				}
			}
		}
		log(logmsg::error, _("File not found"));
		return FZ_REPLY_ERROR;
	}

	log(logmsg::debug_warning, L"Unknown opState in CStorjResolveOpData::SubcommandResult()");
	return FZ_REPLY_INTERNALERROR;
}





int CStorjResolveManyOpData::Send()
{
	switch (opState) {
	case resolve_init:
		bucket_.clear();
		fileIds_.clear();

		if (path_.empty() || files_.empty()) {
			return FZ_REPLY_INTERNALERROR;
		}
		else if (!path_.SegmentCount()) {
			// It's the root, nothing to resolve here.
			return FZ_REPLY_INTERNALERROR;
		}
		else {
			CDirectoryListing buckets;

			bool outdated{};
			bool found = engine_.GetDirectoryCache().Lookup(buckets, currentServer_, CServerPath(L"/"), false, outdated);
			if (found && !outdated) {
				size_t pos = buckets.FindFile_CmpCase(path_.GetFirstSegment());
				if (pos != std::string::npos) {
					bucket_ = ExtractId(*buckets[pos].ownerGroup);
					log(logmsg::debug_info, L"Directory is in bucket %s", bucket_);
					opState = resolve_id;
					return FZ_REPLY_CONTINUE;
				}
				else {
					log(logmsg::error, _("Bucket not found"));
					return FZ_REPLY_ERROR;
				}
			}

			opState = resolve_waitlistbuckets;
			controlSocket_.List(CServerPath(L"/"), std::wstring(), 0);

			return FZ_REPLY_CONTINUE;
		}
		break;
	case resolve_id:
		{
			CDirectoryListing listing;

			bool outdated{};
			bool found = engine_.GetDirectoryCache().Lookup(listing, currentServer_, path_, false, outdated);
			if (found && !outdated) {
				for (auto const& file : files_ ) {
					size_t pos = listing.FindFile_CmpCase(file);
					if (pos != std::string::npos) {
						log(logmsg::debug_info, L"File %s has id %s", path_.FormatFilename(file), *listing[pos].ownerGroup);
						fileIds_.emplace_back(ExtractId(*listing[pos].ownerGroup));
					}
					else {
						fileIds_.emplace_back();
					}
				}
				return FZ_REPLY_OK;
			}

			opState = resolve_waitlist;
			controlSocket_.List(path_, std::wstring(), 0);
			return FZ_REPLY_CONTINUE;
		}
		break;
	}

	log(logmsg::debug_warning, L"Unknown opState in CStorjResolveManyOpData::Send()");
	return FZ_REPLY_INTERNALERROR;
}

int CStorjResolveManyOpData::SubcommandResult(int prevResult, COpData const&)
{
	if (prevResult != FZ_REPLY_OK) {
		return prevResult;
	}

	switch (opState) {
	case resolve_waitlistbuckets:
		{
			CDirectoryListing buckets;

			bool outdated{};
			bool found = engine_.GetDirectoryCache().Lookup(buckets, currentServer_, CServerPath(L"/"), false, outdated);
			if (found && !outdated) {
				size_t pos = buckets.FindFile_CmpCase(path_.GetFirstSegment());
				if (pos != std::string::npos) {
					bucket_ = ExtractId(*buckets[pos].ownerGroup);
					log(logmsg::debug_info, L"Directory is in bucket %s", bucket_);
					opState = resolve_id;
					return FZ_REPLY_CONTINUE;
				}
			}
		}
		log(logmsg::error, _("Bucket not found"));
		return FZ_REPLY_ERROR;
	case resolve_waitlist:
		{
			CDirectoryListing listing;

			bool outdated{};
			bool found = engine_.GetDirectoryCache().Lookup(listing, currentServer_, path_, false, outdated);
			if (found && !outdated) {
				for (auto const& file : files_ ) {
					size_t pos = listing.FindFile_CmpCase(file);
					if (pos != std::string::npos) {
						log(logmsg::debug_info, L"File %s has id %s", path_.FormatFilename(file), *listing[pos].ownerGroup);
						fileIds_.emplace_back(ExtractId(*listing[pos].ownerGroup));
					}
					else {
						fileIds_.emplace_back();
					}
				}
				return FZ_REPLY_OK;
			}
		}
		log(logmsg::error, _("Files not found"));
		return FZ_REPLY_ERROR;
	}

	log(logmsg::debug_warning, L"Unknown opState in CStorjResolveManyOpData::SubcommandResult()");
	return FZ_REPLY_INTERNALERROR;
}
