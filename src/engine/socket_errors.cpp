#include <libfilezilla/libfilezilla.hpp>
#ifdef FZ_WINDOWS
  #include <libfilezilla/private/windows.hpp>
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <mstcpip.h>
#endif
#include <filezilla.h>
#include <libfilezilla/format.hpp>
#include <libfilezilla/socket.hpp>
#ifndef FZ_WINDOWS
#include <netdb.h>
#endif

// Fixups needed on FreeBSD
#if !defined(EAI_ADDRFAMILY) && defined(EAI_FAMILY)
  #define EAI_ADDRFAMILY EAI_FAMILY
#endif

namespace fz {

#define ERRORDECL(c, desc) { c, #c, desc },

namespace {
struct Error_table
{
	int code;
	char const* const name;
	char const* const description;
};

static Error_table const error_table[] =
{
	ERRORDECL(EACCES, fztranslate_mark("Permission denied"))
	ERRORDECL(EADDRINUSE, fztranslate_mark("Local address in use"))
	ERRORDECL(EAFNOSUPPORT, fztranslate_mark("The specified address family is not supported"))
	ERRORDECL(EINPROGRESS, fztranslate_mark("Operation in progress"))
	ERRORDECL(EINVAL, fztranslate_mark("Invalid argument passed"))
	ERRORDECL(EMFILE, fztranslate_mark("Process file table overflow"))
	ERRORDECL(ENFILE, fztranslate_mark("System limit of open files exceeded"))
	ERRORDECL(ENOBUFS, fztranslate_mark("Out of memory"))
	ERRORDECL(ENOMEM, fztranslate_mark("Out of memory"))
	ERRORDECL(EPERM, fztranslate_mark("Permission denied"))
	ERRORDECL(EPROTONOSUPPORT, fztranslate_mark("Protocol not supported"))
	ERRORDECL(EAGAIN, fztranslate_mark("Resource temporarily unavailable"))
	ERRORDECL(EALREADY, fztranslate_mark("Operation already in progress"))
	ERRORDECL(EBADF, fztranslate_mark("Bad file descriptor"))
	ERRORDECL(ECONNREFUSED, fztranslate_mark("Connection refused by server"))
	ERRORDECL(EFAULT, fztranslate_mark("Socket address outside address space"))
	ERRORDECL(EINTR, fztranslate_mark("Interrupted by signal"))
	ERRORDECL(EISCONN, fztranslate_mark("Socket already connected"))
	ERRORDECL(ENETUNREACH, fztranslate_mark("Network unreachable"))
	ERRORDECL(ENOTSOCK, fztranslate_mark("File descriptor not a socket"))
	ERRORDECL(ETIMEDOUT, fztranslate_mark("Connection attempt timed out"))
	ERRORDECL(EHOSTUNREACH, fztranslate_mark("No route to host"))
	ERRORDECL(ENOTCONN, fztranslate_mark("Socket not connected"))
	ERRORDECL(ENETRESET, fztranslate_mark("Connection reset by network"))
	ERRORDECL(EOPNOTSUPP, fztranslate_mark("Operation not supported"))
	ERRORDECL(ESHUTDOWN, fztranslate_mark("Socket has been shut down"))
	ERRORDECL(EMSGSIZE, fztranslate_mark("Message too large"))
	ERRORDECL(ECONNABORTED, fztranslate_mark("Connection aborted"))
	ERRORDECL(ECONNRESET, fztranslate_mark("Connection reset by peer"))
	ERRORDECL(EPIPE, fztranslate_mark("Local endpoint has been closed"))
	ERRORDECL(EHOSTDOWN, fztranslate_mark("Host is down"))

	// Getaddrinfo related
#ifdef EAI_ADDRFAMILY
	ERRORDECL(EAI_ADDRFAMILY, fztranslate_mark("Network host does not have any network addresses in the requested address family"))
#endif
	ERRORDECL(EAI_AGAIN, fztranslate_mark("Temporary failure in name resolution"))
	ERRORDECL(EAI_BADFLAGS, fztranslate_mark("Invalid value for ai_flags"))
#ifdef EAI_BADHINTS
	ERRORDECL(EAI_BADHINTS, fztranslate_mark("Invalid value for hints"))
#endif
	ERRORDECL(EAI_FAIL, fztranslate_mark("Nonrecoverable failure in name resolution"))
	ERRORDECL(EAI_FAMILY, fztranslate_mark("The ai_family member is not supported"))
	ERRORDECL(EAI_MEMORY, fztranslate_mark("Memory allocation failure"))
#ifdef EAI_NODATA
	ERRORDECL(EAI_NODATA, fztranslate_mark("No address associated with nodename"))
#endif
	ERRORDECL(EAI_NONAME, fztranslate_mark("Neither nodename nor servname provided, or not known"))
#ifdef EAI_OVERFLOW
	ERRORDECL(EAI_OVERFLOW, fztranslate_mark("Argument buffer overflow"))
#endif
#ifdef EAI_PROTOCOL
	ERRORDECL(EAI_PROTOCOL, fztranslate_mark("Resolved protocol is unknown"))
#endif
	ERRORDECL(EAI_SERVICE, fztranslate_mark("The servname parameter is not supported for ai_socktype"))
	ERRORDECL(EAI_SOCKTYPE, fztranslate_mark("The ai_socktype member is not supported"))
#ifdef EAI_SYSTEM
	ERRORDECL(EAI_SYSTEM, fztranslate_mark("Other system error"))
#endif
#ifdef EAI_IDN_ENCODE
	ERRORDECL(EAI_IDN_ENCODE, fztranslate_mark("Invalid characters in hostname"))
#endif

	// Codes that have no POSIX equivalence
#ifdef FZ_WINDOWS
	ERRORDECL(WSANOTINITIALISED, fztranslate_mark("Not initialized, need to call WSAStartup"))
	ERRORDECL(WSAENETDOWN, fztranslate_mark("System's network subsystem has failed"))
	ERRORDECL(WSAEPROTOTYPE, fztranslate_mark("Protocol not supported on given socket type"))
	ERRORDECL(WSAESOCKTNOSUPPORT, fztranslate_mark("Socket type not supported for address family"))
	ERRORDECL(WSAEADDRNOTAVAIL, fztranslate_mark("Cannot assign requested address"))
	ERRORDECL(ERROR_NETNAME_DELETED, fztranslate_mark("The specified network name is no longer available"))
#endif
	{ 0, nullptr, nullptr }
};
}

std::string socket_error_string(int error)
{
	for (int i = 0; error_table[i].code; ++i) {
		if (error != error_table[i].code) {
			continue;
		}

		return error_table[i].name;
	}

	return fz::to_string(error);
}

native_string socket_error_description(int error)
{
	for (int i = 0; error_table[i].code; ++i) {
		if (error != error_table[i].code) {
			continue;
		}

		return to_native(to_native(std::string(error_table[i].name)) + fzT(" - ") + to_native(translate(error_table[i].description)));
	}

	return sprintf(fzT("%d"), error);
}

}
