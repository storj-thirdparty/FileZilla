#ifndef FILEZILLA_INTERFACE_FILEZILLAAPP_HEADER
#define FILEZILLA_INTERFACE_FILEZILLAAPP_HEADER

#include "local_path.h"

#include <vector>

class CWrapEngine;
class CCommandLine;
class CThemeProvider;

class CFileZillaApp : public wxApp
{
public:
	CFileZillaApp();
	virtual ~CFileZillaApp();

	virtual bool OnInit();
	virtual int OnExit();

	// Always (back)slash-terminated
	CLocalPath GetResourceDir() const { return m_resourceDir; }
	CLocalPath GetDefaultsDir() const { return m_defaultsDir; }
	CLocalPath GetLocalesDir() const { return m_localesDir; }

	std::wstring GetSettingsFile(std::wstring const& name) const;

	void CheckExistsFzsftp();
#if ENABLE_STORJ
	void CheckExistsFzstorj();
#endif

	void InitLocale();
	bool SetLocale(int language);
	int GetCurrentLanguage() const;
	wxString GetCurrentLanguageCode() const;

	void DisplayEncodingWarning();

	CWrapEngine* GetWrapEngine();

	const CCommandLine* GetCommandLine() const { return m_pCommandLine.get(); }

	void ShowStartupProfile();
	void AddStartupProfileRecord(std::string const& msg);

protected:
	void CheckExistsTool(std::wstring const& tool, std::wstring const& buildRelPath, char const* env, int setting, std::wstring const& description);

	bool InitDefaultsDir();
	bool LoadResourceFiles();
	bool LoadLocales();
	int ProcessCommandLine();

	std::unique_ptr<wxLocale> m_pLocale;

	CLocalPath m_resourceDir;
	CLocalPath m_defaultsDir;
	CLocalPath m_localesDir;

#if wxUSE_DEBUGREPORT && wxUSE_ON_FATAL_EXCEPTION
	virtual void OnFatalException();
#endif

	CLocalPath GetDataDir(std::vector<std::wstring> const& fileToFind, std::wstring const& prefixSub, bool searchSelfDir = true) const;

	bool FileExists(std::wstring const& file) const;

	std::unique_ptr<CWrapEngine> m_pWrapEngine;

	std::unique_ptr<CCommandLine> m_pCommandLine;

	fz::monotonic_clock m_profile_start;
	std::vector<std::pair<fz::monotonic_clock, std::string>> m_startupProfile;

	std::unique_ptr<CThemeProvider> themeProvider_;
};

DECLARE_APP(CFileZillaApp)

#endif
