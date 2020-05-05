#include <filezilla.h>

#include "loginmanager.h"
#include "serverdata.h"
#include "Options.h"

#include <libfilezilla/translate.hpp>

bool Bookmark::operator==(Bookmark const& b) const
{
	if (m_localDir != b.m_localDir) {
		return false;
	}

	if (m_remoteDir != b.m_remoteDir) {
		return false;
	}

	if (m_sync != b.m_sync) {
		return false;
	}

	if (m_comparison != b.m_comparison) {
		return false;
	}

	if (m_name != b.m_name) {
		return false;
	}

	return true;
}

Site::Site(Site const& s)
	: server(s.server)
	, originalServer(s.originalServer)
	, credentials(s.credentials)
	, comments_(s.comments_)
	, m_default_bookmark(s.m_default_bookmark)
	, m_bookmarks(s.m_bookmarks)
	, m_colour(s.m_colour)
{
	if (s.data_) {
		data_ = std::make_shared<SiteHandleData>(*s.data_);
	}
}

Site& Site::operator=(Site const& s)
{
	if (this != &s) {
		server = s.server;
		originalServer = s.originalServer;
		credentials = s.credentials;
		comments_ = s.comments_;
		m_default_bookmark = s.m_default_bookmark;
		m_bookmarks = s.m_bookmarks;
		m_colour = s.m_colour;
		data_.reset();

		if (s.data_) {
			data_ = std::make_shared<SiteHandleData>(*s.data_);
		}
	}

	return *this;
}

bool Site::operator==(Site const& s) const
{
	if (server != s.server) {
		return false;
	}

	if (comments_ != s.comments_) {
		return false;
	}

	if (m_default_bookmark != s.m_default_bookmark) {
		return false;
	}

	if (m_bookmarks != s.m_bookmarks) {
		return false;
	}

	if (static_cast<bool>(data_) != static_cast<bool>(s.data_)) {
		return false;
	}

	if (data_ && *data_ != *s.data_) {
		return false;
	}

	if (m_colour != s.m_colour) {
		return false;
	}

	return true;
}

void Site::SetName(std::wstring const& name) {
	if (!data_) {
		data_ = std::make_shared<SiteHandleData>();
	}
	data_->name_ = name;
}

std::wstring const& Site::GetName() const
{
	if (data_) {
		return data_->name_;
	}
	else {
		static std::wstring empty;
		return empty;
	}
}

void Site::SetSitePath(std::wstring const& sitePath) {
	if (!data_) {
		data_ = std::make_shared<SiteHandleData>();
	}
	data_->sitePath_ = sitePath;
}

std::wstring const& Site::SitePath() const
{
	if (data_) {
		return data_->sitePath_;
	}
	else {
		static std::wstring empty;
		return empty;
	}
}

ServerHandle Site::Handle() const
{
	return data_;
}

void Site::Update(Site const& rhs)
{
	CServer newServer;
	std::optional<CServer> newOriginalServer;
	if (originalServer && originalServer->SameResource(rhs.GetOriginalServer())) {
		newOriginalServer = rhs.GetOriginalServer();
	}
	else {
		newOriginalServer = originalServer;
	}

	if (server.SameResource(rhs.server)) {
		newServer = rhs.server;
	}
	else {
		newServer = server;
	}

	std::shared_ptr<SiteHandleData> data = data_;
	*this = rhs;
	server = newServer;
	originalServer = newOriginalServer;
	if (data && rhs.data_) {
		*data = *rhs.data_;
		data_ = data;
	}
}

bool Site::ParseUrl(std::wstring const& host, std::wstring const& port, std::wstring const& user, std::wstring const& pass, std::wstring &error, CServerPath &path, ServerProtocol const hint)
{
	unsigned int nPort = 0;
	if (!port.empty()) {
		nPort = fz::to_integral<unsigned int>(fz::trimmed(port));
		if (port.size() > 5 || !nPort || nPort > 65535) {
			error = fztranslate("Invalid port given. The port has to be a value from 1 to 65535.");
			error += L"\n";
			error += fztranslate("You can leave the port field empty to use the default port.");
			return false;
		}
	}
	return ParseUrl(host, nPort, user, pass, error, path, hint);
}

bool Site::ParseUrl(std::wstring host, unsigned int port, std::wstring user, std::wstring pass, std::wstring &error, CServerPath &path, ServerProtocol const hint)
{
	server.SetType(DEFAULT);

	if (host.empty()) {
		error = fztranslate("No host given, please enter a host.");
		return false;
	}

	size_t pos = host.find(L"://");
	if (pos != std::wstring::npos) {
		std::wstring protocol = fz::str_tolower_ascii(host.substr(0, pos));
		host = host.substr(pos + 3);
		if (protocol.substr(0, 3) == L"fz_") {
			protocol = protocol.substr(3);
		}
		auto p = CServer::GetProtocolFromPrefix(protocol, hint);
		if (p == UNKNOWN) {
			error = fztranslate("Invalid protocol specified. Valid protocols are:\nftp:// for normal FTP with optional encryption,\nsftp:// for SSH file transfer protocol,\nftps:// for FTP over TLS (implicit) and\nftpes:// for FTP over TLS (explicit).");
			return false;
		}
		server.SetProtocol(p);
	}
	else {
		if (hint != UNKNOWN) {
			server.SetProtocol(hint);
		}
	}

	pos = host.find('@');
	if (pos != std::wstring::npos) {
		// Check if it's something like
		//   user@name:password@host:port/path
		// => If there are multiple at signs, username/port ends at last at before
		// the first slash. (Since host and port never contain any at sign)

		size_t slash = host.find('/', pos + 1);

		size_t next_at = host.find('@', pos + 1);
		while (next_at != std::wstring::npos) {
			if (slash != std::wstring::npos && next_at > slash) {
				break;
			}

			pos = next_at;
			next_at = host.find('@', next_at + 1);
		}

		user = host.substr(0, pos);
		host = host.substr(pos + 1);

		// Extract password (if any) from username
		pos = user.find(':');
		if (pos != std::wstring::npos) {
			pass = user.substr(pos + 1);
			user = user.substr(0, pos);
		}

		// Remove leading and trailing whitespace
		fz::trim(user);

		if (user.empty()) {
			error = fztranslate("Invalid username given.");
			return false;
		}
	}
	else {
		// Remove leading and trailing whitespace
		fz::trim(user);
	}

	pos = host.find('/');
	if (pos != std::wstring::npos) {
		path = CServerPath(host.substr(pos));
		host = host.substr(0, pos);
	}

	if (!host.empty() && host[0] == '[') {
		// Probably IPv6 address
		pos = host.find(']');
		if (pos == std::wstring::npos) {
			error = fztranslate("Host starts with '[' but no closing bracket found.");
			return false;
		}
		if (pos < host.size() - 1) {
			if (host[pos + 1] != ':') {
				error = fztranslate("Invalid host, after closing bracket only colon and port may follow.");
				return false;
			}
			++pos;
		}
		else {
			pos = std::wstring::npos;
		}
	}
	else {
		pos = host.find(':');
	}
	if (pos != std::wstring::npos) {
		if (!pos) {
			error = fztranslate("No host given, please enter a host.");
			return false;
		}

		port = fz::to_integral<unsigned int>(host.substr(pos + 1));
		host = host.substr(0, pos);
	}
	else {
		if (!port) {
			port = CServer::GetDefaultPort(server.GetProtocol());
		}
	}

	if (port < 1 || port > 65535) {
		error = fztranslate("Invalid port given. The port has to be a value from 1 to 65535.");
		return false;
	}

	fz::trim(host);

	if (host.empty()) {
		error = fztranslate("No host given, please enter a host.");
		return false;
	}

	if (host[0] == '[') {
		host = host.substr(1, host.size() - 2);
	}

	server.SetHost(host, port);

	credentials.account_.clear();

	if (credentials.logonType_ != LogonType::ask && credentials.logonType_ != LogonType::interactive) {
		if (user.empty()) {
			credentials.logonType_ = LogonType::anonymous;
		}
		else if (user == L"anonymous") {
			if (pass.empty() || pass == L"anonymous@example.com") {
				credentials.logonType_ = LogonType::anonymous;
			}
			else {
				credentials.logonType_ = LogonType::normal;
			}
		}
		else {
			credentials.logonType_ = LogonType::normal;
		}
	}
	if (credentials.logonType_ == LogonType::anonymous) {
		user.clear();
		pass.clear();
	}
	server.SetUser(user);
	credentials.SetPass(pass);

	if (server.GetProtocol() == UNKNOWN) {
		server.SetProtocol(CServer::GetProtocolFromPort(port));
	}

	return true;
}

void Site::SetLogonType(LogonType logonType)
{
	credentials.logonType_ = logonType;
	if (logonType == LogonType::anonymous) {
		server.SetUser(L"");
	}
}

void Site::SetUser(std::wstring const& user)
{
	if (credentials.logonType_ == LogonType::anonymous) {
		server.SetUser(L"");
	}
	else {
		server.SetUser(user);
	}
}

void ProtectedCredentials::Protect()
{
	if (logonType_ != LogonType::normal && logonType_ != LogonType::account) {
		password_.clear();
		return;
	}

	bool kiosk_mode = COptions::Get()->GetOptionVal(OPTION_DEFAULT_KIOSKMODE) != 0;
	if (kiosk_mode) {
		if (logonType_ == LogonType::normal || logonType_ == LogonType::account) {
			logonType_ = LogonType::ask;
			password_.clear();
		}
	}
	else {
		auto key = fz::public_key::from_base64(fz::to_utf8(COptions::Get()->GetOption(OPTION_MASTERPASSWORDENCRYPTOR)));
		Protect(key);
	}
}

void ProtectedCredentials::Protect(fz::public_key const& key)
{
	if (logonType_ != LogonType::normal && logonType_ != LogonType::account) {
		password_.clear();
		encrypted_ = fz::public_key();
		return;
	}

	if (!key) {
		return;
	}

	if (encrypted_ == key) {
		return;
	}
	else {
		// Different key used. Try decrypting it
		auto priv = CLoginManager::Get().GetDecryptor(key);
		if (priv) {
			if (!Unprotect(priv, true)) {
				return;
			}
		}
	}
	
	auto plain = fz::to_utf8(password_);
	if (plain.size() < 16) {
		// Primitive length hiding
		plain.append(16 - plain.size(), 0);
	}
	auto encrypted = encrypt(plain, key);

	if (encrypted.empty()) {
		// Something went wrong
		logonType_ = LogonType::ask;
		password_.clear();
	}
	else {
		password_ = fz::to_wstring_from_utf8(fz::base64_encode(std::string(encrypted.begin(), encrypted.end()), fz::base64_type::standard, false));
		encrypted_ = key;
	}
}

bool ProtectedCredentials::DoUnprotect(fz::private_key const& key)
{
	if (!key || key.pubkey() != encrypted_) {
		return false;
	}

	auto const cipher = fz::base64_decode(fz::to_utf8(password_));

	auto plain = fz::decrypt(cipher, key);
	if (plain.empty()) {
		// Compatibility with unauthenticated encryption, remove eventually.
		plain = decrypt(cipher, key, false);
	}

	if (plain.size() < 16) {
		return false;
	}

	// This undoes the length-hiding
	auto pw = std::string(plain.begin(), plain.end());
	char const c = 0;
	auto pos = pw.find(c);
	if (pos != std::string::npos) {
		if (pw.find_first_not_of(c, pos + 1) != std::string::npos) {
			return false;
		}
		pw = pw.substr(0, pos);
	}
	password_ = fz::to_wstring_from_utf8(pw);
	if (password_.empty() && !pw.empty()) {
		return false;
	}
	encrypted_ = fz::public_key();
	return true;

}

bool ProtectedCredentials::Unprotect(fz::private_key const& key, bool on_failure_set_to_ask)
{
	if (!encrypted_) {
		return true;
	}

	bool const ret = DoUnprotect(key);
	if (!ret && on_failure_set_to_ask) {
		encrypted_ = fz::public_key();
		password_.clear();
		logonType_ = LogonType::ask;
	}

	return ret;
}

SiteHandleData toSiteHandle(ServerHandle const& handle)
{
	auto l = handle.lock();
	if (l) {
		auto d = dynamic_cast<SiteHandleData const*>(l.get());
		if (d) {
			return *d;
		}
	}

	return SiteHandleData();
}
