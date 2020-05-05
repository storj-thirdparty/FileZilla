#ifndef FILEZILLA_INTERFACE_SERVER_HEADER
#define FILEZILLA_INTERFACE_SERVER_HEADER

#include <server.h>

#include <libfilezilla/encryption.hpp>

#include <optional>
#include <string>

class ProtectedCredentials final : public Credentials
{
public:
	ProtectedCredentials() = default;
	ProtectedCredentials(ProtectedCredentials const& c) = default;
	ProtectedCredentials& operator=(ProtectedCredentials const& c) = default;

	explicit ProtectedCredentials(Credentials const& c)
		: Credentials(c)
	{
	}

	void Protect();
	void Protect(fz::public_key const& key);
	bool Unprotect(fz::private_key const& key, bool on_failure_set_to_ask = false);

	fz::public_key encrypted_;

private:
	bool DoUnprotect(fz::private_key const& key);
};

class Bookmark final
{
public:
	bool operator==(Bookmark const& b) const;
	bool operator!=(Bookmark const& b) const { return !(*this == b); }

	std::wstring m_localDir;
	CServerPath m_remoteDir;

	bool m_sync{};
	bool m_comparison{};

	std::wstring m_name;
};

struct SiteHandleData final : public ServerHandleData
{
public:
	std::wstring name_;
	std::wstring sitePath_;

	bool operator==(SiteHandleData& rhs) const {
		return name_ == rhs.name_ && sitePath_ == rhs.sitePath_;
	}

	bool operator!=(SiteHandleData& rhs) const {
		return !(*this == rhs);
	}
};

SiteHandleData toSiteHandle(ServerHandle const& handle);

class Site final
{
public:
	Site() = default;

	explicit Site(CServer const& s, ServerHandle const& handle, Credentials const& c)
		: server(s)
		, credentials(c)
		, data_(std::dynamic_pointer_cast<SiteHandleData>(handle.lock()))
	{}

	explicit Site(CServer const& s, ServerHandle const& handle, ProtectedCredentials const& c)
		: server(s)
		, credentials(c)
		, data_(std::dynamic_pointer_cast<SiteHandleData>(handle.lock()))
	{}

	Site(Site const& s);
	Site& operator=(Site const& s);

	explicit operator bool() const { return server.operator bool(); }

	bool empty() const { return !*this; }
	bool operator==(Site const& s) const;
	bool operator!=(Site const& s) const { return !(*this == s); }

	// Return true if URL could be parsed correctly, false otherwise.
	// If parsing fails, pError is filled with the reason and the CServer instance may be left an undefined state.
	bool ParseUrl(std::wstring host, unsigned int port, std::wstring user, std::wstring pass, std::wstring &error, CServerPath &path, ServerProtocol const hint = UNKNOWN);
	bool ParseUrl(std::wstring const& host, std::wstring const& port, std::wstring const& user, std::wstring const& pass, std::wstring &error, CServerPath &path, ServerProtocol const hint = UNKNOWN);

	std::wstring Format(ServerFormat formatType) const {
		return server.Format(formatType, credentials);
	}

	void SetLogonType(LogonType logonType);

	void SetUser(std::wstring const& user);

	void SetName(std::wstring const& name);
	std::wstring const& GetName() const;

	CServer server;
	std::optional<CServer> originalServer;
	ProtectedCredentials credentials;

	std::wstring comments_;

	Bookmark m_default_bookmark;

	std::vector<Bookmark> m_bookmarks;

	wxColour m_colour;

	void SetSitePath(std::wstring const& sitePath);
	std::wstring const& SitePath() const;

	ServerHandle Handle() const;

	// Almost like operator= but does not invalidate exiting handles.
	void Update(Site const& rhs);

	bool SameResource(Site const& other) const {
		return server.SameResource(other.server);
	}

	CServer const& GetOriginalServer() const {
		return originalServer ? *originalServer : server;
	}
private:
	std::shared_ptr<SiteHandleData> data_;
};

#endif
