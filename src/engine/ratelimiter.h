#ifndef FILEZILLA_ENGINE_RATELIMITER_HEADER
#define FILEZILLA_ENGINE_RATELIMITER_HEADER

#include <option_change_event_handler.h>

#include <libfilezilla/event_handler.hpp>

class COptionsBase;

class CRateLimiterObject;

// This class implements a simple rate limiter based on the Token Bucket algorithm.
class CRateLimiter final : protected fz::event_handler, COptionChangeEventHandler
{
public:
	CRateLimiter(fz::event_loop& loop, COptionsBase& options);
	virtual ~CRateLimiter();

	enum rate_direction
	{
		inbound,
		outbound
	};

	void AddObject(CRateLimiterObject* pObject);
	void RemoveObject(CRateLimiterObject* pObject);

private:
	void UpdateLimits();

	std::vector<CRateLimiterObject*> objects_;
	std::vector<CRateLimiterObject*> wakeupList_[2];
	std::vector<CRateLimiterObject*> scratchBuf_;

	fz::timer_id timer_{};

	int64_t limits_[2]{0, 0};
	int64_t bucketSize_{};
	int64_t tokenDebt_[2]{0, 0};

	COptionsBase& options_;

	void WakeupWaitingObjects(fz::scoped_lock & l);

	void OnOptionsChanged(changed_options_t const& options);

	void operator()(fz::event_base const& ev);
	void OnTimer(fz::timer_id id);
	void OnRateChanged();

	fz::mutex sync_{false};
};

struct ratelimit_changed_event_type{};
typedef fz::simple_event<ratelimit_changed_event_type> CRateLimitChangedEvent;

class CRateLimiterObject
{
	friend class CRateLimiter;

public:
	CRateLimiterObject() = default;
	virtual ~CRateLimiterObject() = default;
	int64_t GetAvailableBytes(CRateLimiter::rate_direction direction) const { return bytesAvailable_[direction]; }

	bool IsWaiting(CRateLimiter::rate_direction direction) const;

protected:
	void UpdateUsage(CRateLimiter::rate_direction direction, int usedBytes);
	void Wait(CRateLimiter::rate_direction direction);

	virtual void OnRateAvailable(CRateLimiter::rate_direction) = 0;

private:
	bool waiting_[2]{};
	int64_t bytesAvailable_[2]{-1, -1};
};

#endif
