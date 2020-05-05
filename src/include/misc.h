#ifndef FILEZILLA_ENGINE_MISC_HEADER
#define FILEZILLA_ENGINE_MISC_HEADER

#include <libfilezilla/event_handler.hpp>

enum class lib_dependency
{
	gnutls,
	count
};

std::wstring GetDependencyName(lib_dependency d);
std::wstring GetDependencyVersion(lib_dependency d);

std::string ListTlsCiphers(std::string const& priority);

template<typename Derived, typename Base>
std::unique_ptr<Derived>
unique_static_cast(std::unique_ptr<Base>&& p)
{
	auto d = static_cast<Derived *>(p.release());
	return std::unique_ptr<Derived>(d);
}

#if FZ_WINDOWS
DWORD GetSystemErrorCode();
fz::native_string GetSystemErrorDescription(DWORD err);
#else
int GetSystemErrorCode();
fz::native_string GetSystemErrorDescription(int err);
#endif

namespace fz {

// Poor-man's tolower. Consider to eventually use libicu or similar
std::wstring str_tolower(std::wstring const& source);
}

#endif
