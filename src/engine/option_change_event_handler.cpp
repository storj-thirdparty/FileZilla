#include <filezilla.h>
#include "option_change_event_handler.h"

#include <libfilezilla/util.hpp>

#include <algorithm>
#include <assert.h>

std::vector<COptionChangeEventHandler*> COptionChangeEventHandler::m_handlers;
std::size_t COptionChangeEventHandler::notify_index_{npos};
fz::mutex COptionChangeEventHandler::m_{false};
COptionChangeEventHandler* COptionChangeEventHandler::active_handler_{};
fz::thread::id COptionChangeEventHandler::thread_id_;

COptionChangeEventHandler::~COptionChangeEventHandler()
{
	UnregisterAllOptions();
}

void COptionChangeEventHandler::UnregisterAllOptions()
{
	fz::scoped_lock l(m_);

	if (index_ != npos) {
		m_handled_options.reset();
		RemoveHandler();
	}

	if (active_handler_ == this) {
		if (fz::thread::own_id() != thread_id_) {
			while (active_handler_ == this) {
				l.unlock();
				fz::sleep(fz::duration::from_milliseconds(1));
				l.lock();
			}
		}
	}
}

void COptionChangeEventHandler::RegisterOption(int option)
{
	if (option < 0) {
		return;
	}

	fz::scoped_lock l(m_);
	if (index_ == npos) {
		index_ = m_handlers.size();
		m_handlers.push_back(this);
	}
	m_handled_options.set(option);
}

void COptionChangeEventHandler::UnregisterOption(int option)
{
	fz::scoped_lock l(m_);
	m_handled_options.set(option, false);
	if (m_handled_options.none()) {
		RemoveHandler();
	}
}

void COptionChangeEventHandler::RemoveHandler()
{
	if (index_ != npos) {
		if (notify_index_ < m_handlers.size() && index_ < notify_index_) {
			--notify_index_;
			m_handlers[index_] = m_handlers[notify_index_];
			m_handlers[index_]->index_ = index_;
			m_handlers[notify_index_] = m_handlers.back();
			m_handlers[notify_index_]->index_ = notify_index_;
		}
		else {
			m_handlers[index_] = m_handlers.back();
			m_handlers[index_]->index_ = index_;
		}
		m_handlers.pop_back();
		index_ = npos;
	}
}

void COptionChangeEventHandler::UnregisterAllHandlers()
{
	fz::scoped_lock l(m_);
	for (auto & handler : m_handlers) {
		handler->m_handled_options.reset();
		handler->index_ = npos;
	}
	m_handlers.clear();
}

void COptionChangeEventHandler::DoNotify(changed_options_t const& options)
{
	fz::scoped_lock l(m_);

	assert(!active_handler_);

	// Going over notify_index_ which may be changed by UnregisterOption
	// Bit ugly but otherwise has reentrancy issues.
	for (notify_index_ = 0; notify_index_ < m_handlers.size();) {
		auto & handler = m_handlers[notify_index_++];
		auto hoptions = options & handler->m_handled_options;
		if (hoptions.any()) {
			active_handler_ = handler;
			thread_id_ = fz::thread::own_id();

			l.unlock();
			handler->OnOptionsChanged(hoptions);
			l.lock();

			active_handler_ = nullptr;
		}
	}
	notify_index_ = npos;
}
