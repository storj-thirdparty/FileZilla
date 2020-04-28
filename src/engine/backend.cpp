#include <filezilla.h>

#include "backend.h"

CRatelimitLayer::CRatelimitLayer(fz::event_handler* pEvtHandler, fz::socket_interface& next_layer, CRateLimiter& rateLimiter)
	: fz::socket_layer(pEvtHandler, next_layer, true)
	, m_rateLimiter(rateLimiter)
{
	next_layer_.set_event_handler(pEvtHandler);
	m_rateLimiter.AddObject(this);
}

CRatelimitLayer::~CRatelimitLayer()
{
	next_layer_.set_event_handler(nullptr);
	m_rateLimiter.RemoveObject(this);
}

int CRatelimitLayer::write(const void *buffer, unsigned int len, int& error)
{
	int64_t max = GetAvailableBytes(CRateLimiter::outbound);
	if (max == 0) {
		Wait(CRateLimiter::outbound);
		error = EAGAIN;
		return -1;
	}
	else if (max > 0 && max < len) {
		len = static_cast<unsigned int>(max);
	}

	int written = next_layer_.write(buffer, len, error);

	if (written > 0 && max != -1) {
		UpdateUsage(CRateLimiter::outbound, written);
	}

	return written;
}

int CRatelimitLayer::read(void *buffer, unsigned int len, int& error)
{
	int64_t max = GetAvailableBytes(CRateLimiter::inbound);
	if (max == 0) {
		Wait(CRateLimiter::inbound);
		error = EAGAIN;
		return -1;
	}
	else if (max > 0 && max < len) {
		len = static_cast<unsigned int>(max);
	}

	int read = next_layer_.read(buffer, len, error);

	if (read > 0 && max != -1) {
		UpdateUsage(CRateLimiter::inbound, read);
	}

	return read;
}

void CRatelimitLayer::OnRateAvailable(CRateLimiter::rate_direction direction)
{
	if (!event_handler_) {
		return;
	}

	if (direction == CRateLimiter::outbound) {
		event_handler_->send_event<fz::socket_event>(this, fz::socket_event_flag::write, 0);
	}
	else {
		event_handler_->send_event<fz::socket_event>(this, fz::socket_event_flag::read, 0);
	}
}
