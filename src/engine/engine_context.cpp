#include <filezilla.h>
#include "engine_context.h"

#include "directorycache.h"
#include "logging_private.h"
#include "oplock_manager.h"
#include "pathcache.h"
#include "ratelimiter.h"

#include <libfilezilla/event_loop.hpp>
#include <libfilezilla/thread_pool.hpp>
#include <libfilezilla/tls_system_trust_store.hpp>

class CFileZillaEngineContext::Impl final
{
public:
	Impl(COptionsBase& options)
		: limiter_(loop_, options)
		, tlsSystemTrustStore_(pool_)
	{
		directory_cache_.SetTtl(fz::duration::from_seconds(options.GetOptionVal(OPTION_CACHE_TTL)));
	}

	~Impl()
	{
	}

	fz::thread_pool pool_;
	fz::event_loop loop_{pool_};
	CRateLimiter limiter_;
	CDirectoryCache directory_cache_;
	CPathCache path_cache_;
	OpLockManager opLockManager_;
	fz::tls_system_trust_store tlsSystemTrustStore_;
};

CFileZillaEngineContext::CFileZillaEngineContext(COptionsBase & options, CustomEncodingConverterBase const& customEncodingConverter)
: options_(options)
, customEncodingConverter_(customEncodingConverter)
, impl_(new Impl(options))
{
}

CFileZillaEngineContext::~CFileZillaEngineContext()
{
}

fz::thread_pool& CFileZillaEngineContext::GetThreadPool()
{
	return impl_->pool_;
}

fz::event_loop& CFileZillaEngineContext::GetEventLoop()
{
	return impl_->loop_;
}

CRateLimiter& CFileZillaEngineContext::GetRateLimiter()
{
	return impl_->limiter_;
}

CDirectoryCache& CFileZillaEngineContext::GetDirectoryCache()
{
	return impl_->directory_cache_;
}

CPathCache& CFileZillaEngineContext::GetPathCache()
{
	return impl_->path_cache_;
}

OpLockManager& CFileZillaEngineContext::GetOpLockManager()
{
	return impl_->opLockManager_;
}

fz::tls_system_trust_store& CFileZillaEngineContext::GetTlsSystemTrustStore()
{
	return impl_->tlsSystemTrustStore_;
}
