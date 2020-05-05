#ifndef FILEZILLA_INTERFACE_SITEMANAGER_HEADER
#define FILEZILLA_INTERFACE_SITEMANAGER_HEADER

#include <wx/treectrl.h>

#include "xmlfunctions.h"

class CSiteManagerXmlHandler
{
public:
	virtual ~CSiteManagerXmlHandler() {};

	// Adds a folder and descents
	virtual bool AddFolder(std::wstring const& name, bool expanded) = 0;
	virtual bool AddSite(std::unique_ptr<Site> data) = 0;

	// Go up a level
	virtual bool LevelUp() { return true; } // *Ding*
};

class CLoginManager;
class CSiteManagerXmlHandler;
class CSiteManagerDialog;
class CSiteManager
{
	friend class CSiteManagerDialog;
public:
	// This function also clears the Id map
	static std::unique_ptr<wxMenu> GetSitesMenu();
	static void ClearIdMap();
	static std::unique_ptr<Site> GetSiteById(int id);

	static std::pair<std::unique_ptr<Site>, Bookmark> GetSiteByPath(std::wstring const& sitePath, bool printErrors = true);

	static std::wstring AddServer(Site site);
	static bool AddBookmark(std::wstring sitePath, wxString const& name, wxString const& local_dir, CServerPath const& remote_dir, bool sync, bool comparison);
	static bool ClearBookmarks(std::wstring sitePath);

	static void Rewrite(CLoginManager & loginManager, bool on_failure_set_to_ask);

	static bool UnescapeSitePath(std::wstring path, std::vector<std::wstring>& result);
	static std::wstring EscapeSegment(std::wstring segment);

	static bool HasSites();

	static bool ReadBookmarkElement(Bookmark & bookmark, pugi::xml_node element);

	static wxColour GetColourFromIndex(int i);
	static int GetColourIndex(wxColour const& c);
	static wxString GetColourName(int i);

	static bool ImportSites(pugi::xml_node sites);

protected:
	static bool ImportSites(pugi::xml_node sitesToImport, pugi::xml_node existingSites);

	static void Rewrite(CLoginManager & loginManager, pugi::xml_node element, bool on_failure_set_to_ask);
	static void Save(pugi::xml_node element, Site const& site);

	static std::pair<std::unique_ptr<Site>, Bookmark> DoGetSiteByPath(std::wstring sitePath, wxString& error);

	static bool Load(CSiteManagerXmlHandler& pHandler);
	static bool Load(pugi::xml_node element, CSiteManagerXmlHandler& pHandler);
	static std::unique_ptr<Site> ReadServerElement(pugi::xml_node element);

	static pugi::xml_node GetElementByPath(pugi::xml_node node, std::vector<std::wstring> const& segments);
	static std::wstring BuildPath(wxChar root, std::vector<std::wstring> const& segments);

	static std::map<int, std::unique_ptr<Site>> m_idMap;

	// The map maps event id's to sites
	static std::unique_ptr<wxMenu> GetSitesMenu_Predefined(std::map<int, std::unique_ptr<Site>> &idMap);

	static bool LoadPredefined(CSiteManagerXmlHandler& handler);
};

#endif
