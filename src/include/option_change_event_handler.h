#ifndef FILEZILLA_ENGINE_OPTION_CHANGE_EVENT_HANDLER_HEADER
#define FILEZILLA_ENGINE_OPTION_CHANGE_EVENT_HANDLER_HEADER

#include <libfilezilla/mutex.hpp>
#include <libfilezilla/thread.hpp>

#include <bitset>
#include <vector>

class COptions;

typedef std::bitset<64*3> changed_options_t;

class COptionChangeEventHandler
{
	friend class COptions;

public:
	COptionChangeEventHandler() = default;
	virtual ~COptionChangeEventHandler();

	void RegisterOption(int option);
	void UnregisterOption(int option);
	void UnregisterAllOptions();

protected:
	virtual void OnOptionsChanged(changed_options_t const& options) = 0;

private:
	void RemoveHandler();

	changed_options_t m_handled_options;

	static constexpr auto npos{static_cast<size_t>(-1)};
	size_t index_{npos};

	// Very important: Never ever call this if there's OnOptionsChanged on the stack.
	static void DoNotify(changed_options_t const& options);
	static std::size_t notify_index_;

	static void UnregisterAllHandlers();

	static std::vector<COptionChangeEventHandler*> m_handlers;

	static fz::mutex m_;

	static COptionChangeEventHandler* active_handler_;
	static fz::thread::id thread_id_;
};

#endif
