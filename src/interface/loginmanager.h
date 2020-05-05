#ifndef FILEZILLA_INTERFACE_LOGINMANAGER_HEADER
#define FILEZILLA_INTERFACE_LOGINMANAGER_HEADER

#include "serverdata.h"

#include <list>

// The purpose of this class is to manage some aspects of the login
// behaviour. These are:
// - Password dialog for servers with ASK or INTERACTIVE logontype
// - Storage of passwords for ASK servers for duration of current session

class CLoginManager
{
public:
	static CLoginManager& Get() { return m_theLoginManager; }

	bool GetPassword(Site & site, bool silent);
	bool GetPassword(Site & site, bool silent, std::wstring const& challenge, bool canRemember);

	void CachedPasswordFailed(CServer const& server, std::wstring const& challenge = std::wstring());

	void RememberPassword(Site & site, std::wstring const& challenge = std::wstring());

	bool AskDecryptor(fz::public_key const& pub, bool allowForgotten, bool allowCancel);
	fz::private_key GetDecryptor(fz::public_key const& pub);
	void Remember(fz::private_key const& key);

protected:
	bool DisplayDialogForEncrypted(Site & site);
	bool DisplayDialog(Site & site, std::wstring const& challenge, bool canRemember);

	static CLoginManager m_theLoginManager;

	// Session password cache for Ask-type servers
	struct t_passwordcache
	{
		std::wstring host;
		unsigned int port{};
		std::wstring user;
		std::wstring password;
		std::wstring challenge;
	};

	std::list<t_passwordcache>::iterator FindItem(CServer const& server, std::wstring const& challenge);

	std::list<t_passwordcache> m_passwordCache;

	std::map<fz::public_key, fz::private_key> decryptors_;
};

#endif
