#ifndef FILEZILLA_ENGINE_BACKEND_HEADER
#define FILEZILLA_ENGINE_BACKEND_HEADER

#include <libfilezilla/socket.hpp>

#include "ratelimiter.h"

class CRatelimitLayer final : public fz::socket_layer, public CRateLimiterObject
{
public:
	CRatelimitLayer(fz::event_handler* pEvtHandler, fz::socket_interface& next_layer, CRateLimiter& rateLimiter);
	virtual ~CRatelimitLayer();

	virtual int read(void *buffer, unsigned int size, int& error) override;
	virtual int write(void const* buffer, unsigned int size, int& error) override;

	virtual fz::socket_state get_state() const override {
		return next_layer_.get_state();
	}

	virtual int connect(fz::native_string const& host, unsigned int port, fz::address_type family = fz::address_type::unknown) override{
		return next_layer_.connect(host, port, family);
	}

	virtual int shutdown() override {
		return next_layer_.shutdown();
	}

protected:
	virtual void OnRateAvailable(CRateLimiter::rate_direction direction) override;

	CRateLimiter& m_rateLimiter;
};

#endif
