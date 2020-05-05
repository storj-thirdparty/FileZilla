#include <filezilla.h>
#include "engine_context.h"

#include "directorycache.h"
#include "logging_private.h"
#include "oplock_manager.h"
#include "option_change_event_handler.h"
#include "pathcache.h"

#include <libfilezilla/event_loop.hpp>
#include <libfilezilla/rate_limiter.hpp>
#include <libfilezilla/thread_pool.hpp>
#include <libfilezilla/tls_system_trust_store.hpp>

class CFileZillaEngineContext::Impl final : private COptionChangeEventHandler
{
public:
	Impl(COptionsBase& options)
		: options_(options)
		, rate_limit_mgr_(loop_)
		, tlsSystemTrustStore_(pool_)
	{
		directory_cache_.SetTtl(fz::duration::from_seconds(options.GetOptionVal(OPTION_CACHE_TTL)));
		rate_limit_mgr_.add(&rate_limiter_);

		RegisterOption(OPTION_SPEEDLIMIT_ENABLE);
		RegisterOption(OPTION_SPEEDLIMIT_INBOUND);
		RegisterOption(OPTION_SPEEDLIMIT_OUTBOUND);
		RegisterOption(OPTION_SPEEDLIMIT_BURSTTOLERANCE);

		UpdateRateLimit();
	}

	~Impl()
	{
		UnregisterAllOptions();
	}

	virtual void OnOptionsChanged(changed_options_t const& options) override;
	void UpdateRateLimit();

	COptionsBase& options_;
	fz::thread_pool pool_;
	fz::event_loop loop_{pool_};
	fz::rate_limit_manager rate_limit_mgr_;
	fz::rate_limiter rate_limiter_;
	CDirectoryCache directory_cache_;
	CPathCache path_cache_;
	OpLockManager opLockManager_;
	fz::tls_system_trust_store tlsSystemTrustStore_;
};

void CFileZillaEngineContext::Impl::UpdateRateLimit()
{
	fz::rate::type tolerance;
	switch (options_.GetOptionVal(OPTION_SPEEDLIMIT_BURSTTOLERANCE)) {
	case 1:
		tolerance = 2;
		break;
	case 2:
		tolerance = 5;
		break;
	default:
		tolerance = 1;
	}
	rate_limit_mgr_.set_burst_tolerance(tolerance);

	fz::rate::type limits[2]{fz::rate::unlimited, fz::rate::unlimited};
	if (options_.GetOptionVal(OPTION_SPEEDLIMIT_ENABLE)) {
		auto const inbound = options_.GetOptionVal(OPTION_SPEEDLIMIT_INBOUND);
		if (inbound > 0) {
			limits[0] = inbound * 1024;
		}
		auto const outbound = options_.GetOptionVal(OPTION_SPEEDLIMIT_OUTBOUND);
		if (outbound > 0) {
			limits[1] = outbound * 1024;
		}
	}
	rate_limiter_.set_limits(limits[0], limits[1]);
}

void CFileZillaEngineContext::Impl::OnOptionsChanged(changed_options_t const&)
{
	UpdateRateLimit();
}

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

fz::rate_limiter& CFileZillaEngineContext::GetRateLimiter()
{
	return impl_->rate_limiter_;
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
