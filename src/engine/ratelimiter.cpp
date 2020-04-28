#include <filezilla.h>
#include "ratelimiter.h"

#include <libfilezilla/event_handler.hpp>

#include <assert.h>

static int const tickDelay = 250;

CRateLimiter::CRateLimiter(fz::event_loop& loop, COptionsBase& options)
	: event_handler(loop)
	, options_(options)
{
	RegisterOption(OPTION_SPEEDLIMIT_ENABLE);
	RegisterOption(OPTION_SPEEDLIMIT_INBOUND);
	RegisterOption(OPTION_SPEEDLIMIT_OUTBOUND);
	RegisterOption(OPTION_SPEEDLIMIT_BURSTTOLERANCE);

	UpdateLimits();
}

CRateLimiter::~CRateLimiter()
{
	remove_handler();
	UnregisterAllOptions();
}

void CRateLimiter::UpdateLimits()
{
	if (options_.GetOptionVal(OPTION_SPEEDLIMIT_ENABLE) != 0) {
		limits_[inbound] = static_cast<int64_t>(options_.GetOptionVal(OPTION_SPEEDLIMIT_INBOUND)) * 1024;
		limits_[outbound] = static_cast<int64_t>(options_.GetOptionVal(OPTION_SPEEDLIMIT_OUTBOUND)) * 1024;
	}
	else {
		limits_[inbound] = 0;
		limits_[outbound] = 0;
	}

	const int burst_tolerance = options_.GetOptionVal(OPTION_SPEEDLIMIT_BURSTTOLERANCE);

	bucketSize_ = 1000 / tickDelay;
	switch (burst_tolerance)
	{
	case 1:
		bucketSize_ *= 2;
		break;
	case 2:
		bucketSize_ *= 5;
		break;
	default:
		break;
	}
}

void CRateLimiter::AddObject(CRateLimiterObject* pObject)
{
	fz::scoped_lock lock(sync_);

	objects_.push_back(pObject);
	scratchBuf_.push_back(nullptr);

	for (int i = 0; i < 2; ++i) {
		if (limits_[i] > 0) {
			int64_t tokens = limits_[i] / (1000 / tickDelay);

			tokens /= objects_.size();
			if (tokenDebt_[i] > 0) {
				if (tokens >= tokenDebt_[i]) {
					tokens -= tokenDebt_[i];
					tokenDebt_[i] = 0;
				}
				else {
					tokens = 0;
					tokenDebt_[i] -= tokens;
				}
			}

			pObject->bytesAvailable_[i] = tokens;

			if (!timer_) {
				timer_ = add_timer(fz::duration::from_milliseconds(tickDelay), false);
			}
		}
		else {
			pObject->bytesAvailable_[i] = -1;
		}
	}
}

void CRateLimiter::RemoveObject(CRateLimiterObject* pObject)
{
	fz::scoped_lock lock(sync_);

	for (size_t i = 0; i < objects_.size(); ++i) {
		auto * const object = objects_[i];
		if (object == pObject) {
			for (int direction = 0; direction < 2; ++direction) {
				// If an object already used up some of its assigned tokens, add them to tokenDebt_,
				// so that newly created objects get less initial tokens.
				// That ensures that rapidly adding and removing objects does not exceed the rate
				if (limits_[direction] && object->bytesAvailable_[direction] != -1) {
					int64_t tokens = limits_[direction] / (1000 / tickDelay);
					tokens /= objects_.size();
					if (object->bytesAvailable_[direction] < tokens) {
						tokenDebt_[direction] += tokens - object->bytesAvailable_[direction];
					}
				}
			}
			objects_[i] = objects_[objects_.size() - 1];
			objects_.pop_back();
			scratchBuf_.pop_back();

			if (objects_.empty()) {
				stop_timer(timer_);
				timer_ = 0;
			}
			break;
		}
	}

	for (int direction = 0; direction < 2; ++direction) {
		for (size_t i = 0; i < wakeupList_[direction].size(); ++i) {
			auto * const object = wakeupList_[direction][i];
			if (object == pObject) {
				wakeupList_[direction][i] = wakeupList_[direction][wakeupList_[direction].size() - 1];
				wakeupList_[direction].pop_back();
				break;
			}
		}
	}
}

void CRateLimiter::OnTimer(fz::timer_id)
{
	fz::scoped_lock lock(sync_);

	if (objects_.empty()) {
		return;
	}

	for (int i = 0; i < 2; ++i) {
		tokenDebt_[i] = 0;

		if (limits_[i] == 0) {
			for (auto * object : objects_) {
				object->bytesAvailable_[i] = -1;
				if (object->waiting_[i]) {
					wakeupList_[i].push_back(object);
				}
			}
			continue;
		}

		int64_t tokens = (limits_[i] * tickDelay) / 1000;
		int64_t maxTokens = tokens * bucketSize_;

		// Get amount of tokens for each object
		int64_t tokensPerObject = tokens / objects_.size();

		if (tokensPerObject == 0) {
			tokensPerObject = 1;
		}
		tokens = 0;

		// This list will hold all objects which didn't reach maxTokens
		size_t unsaturated{};

		for (auto * object : objects_) {
			if (object->bytesAvailable_[i] == -1) {
				assert(!object->waiting_[i]);
				object->bytesAvailable_[i] = tokensPerObject;
				scratchBuf_[unsaturated++] = object;
			}
			else {
				object->bytesAvailable_[i] += tokensPerObject;
				if (object->bytesAvailable_[i] > maxTokens) {
					tokens += object->bytesAvailable_[i] - maxTokens;
					object->bytesAvailable_[i] = maxTokens;
				}
				else {
					scratchBuf_[unsaturated++] = object;
				}

				if (object->waiting_[i]) {
					wakeupList_[i].push_back(object);
				}
			}
		}

		// If there are any left-over tokens (in case of objects with a rate below the limit)
		// assign to the unsaturated sources
		while (tokens != 0 && unsaturated) {
			tokensPerObject = tokens / unsaturated;
			if (tokensPerObject == 0) {
				break;
			}
			tokens = 0;

			for (size_t j = 0; j < unsaturated; ) {
				auto * object = scratchBuf_[j];
				object->bytesAvailable_[i] += tokensPerObject;
				if (object->bytesAvailable_[i] > maxTokens) {
					tokens += object->bytesAvailable_[i] - maxTokens;
					object->bytesAvailable_[i] = maxTokens;

					scratchBuf_[j] = scratchBuf_[--unsaturated];
				}
				else {
					++j;
				}
			}
		}
	}

	WakeupWaitingObjects(lock);
}

void CRateLimiter::WakeupWaitingObjects(fz::scoped_lock & l)
{
	for (int i = 0; i < 2; ++i) {
		while (!wakeupList_[i].empty()) {
			CRateLimiterObject* pObject = wakeupList_[i].back();
			wakeupList_[i].pop_back();
			if (!pObject->waiting_[i]) {
				continue;
			}

			assert(pObject->bytesAvailable_[i] != 0);
			pObject->waiting_[i] = false;

			l.unlock(); // Do not hold while executing callback
			pObject->OnRateAvailable((rate_direction)i);
			l.lock();
		}
	}
}

void CRateLimiter::operator()(fz::event_base const& ev)
{
	fz::dispatch<fz::timer_event, CRateLimitChangedEvent>(ev, this,
		&CRateLimiter::OnTimer,
		&CRateLimiter::OnRateChanged);
}

void CRateLimiter::OnRateChanged()
{
	fz::scoped_lock lock(sync_);

	UpdateLimits();

	if (limits_[inbound] > 0 || limits_[outbound] > 0) {
		if (!timer_ && !objects_.empty()) {
			timer_ = add_timer(fz::duration::from_milliseconds(tickDelay), false);
		}
	}
	else {
		stop_timer(timer_);
		timer_ = 0;

		tokenDebt_[inbound] = 0;
		tokenDebt_[outbound] = 0;

		for (int i = 0; i < 2; ++i) {
			for (auto * object : objects_) {
				object->bytesAvailable_[i] = -1;
				if (object->waiting_[i]) {
					wakeupList_[i].push_back(object);
				}
			}
		}

		WakeupWaitingObjects(lock);
	}
}

void CRateLimiter::OnOptionsChanged(changed_options_t const&)
{
	send_event<CRateLimitChangedEvent>();
}

void CRateLimiterObject::UpdateUsage(CRateLimiter::rate_direction direction, int usedBytes)
{
	assert(0 <= direction && direction <= 1);
	assert(usedBytes <= bytesAvailable_[direction]);
	if (usedBytes > bytesAvailable_[direction]) {
		bytesAvailable_[direction] = 0;
	}
	else {
		bytesAvailable_[direction] -= usedBytes;
	}
}

void CRateLimiterObject::Wait(CRateLimiter::rate_direction direction)
{
	assert(0 <= direction && direction <= 1);
	assert(bytesAvailable_[direction] == 0);
	waiting_[direction] = true;
}

bool CRateLimiterObject::IsWaiting(CRateLimiter::rate_direction direction) const
{
	assert(0 <= direction && direction <= 1);
	return waiting_[direction];
}
