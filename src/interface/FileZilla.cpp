#include <filezilla.h>
#ifdef _MSC_VER
#pragma hdrstop
#endif
#include "filezillaapp.h"
#include "Mainfrm.h"
#include "Options.h"
#include "wrapengine.h"
#include "buildinfo.h"
#include "cmdline.h"
#include "welcome_dialog.h"
#include "msgbox.h"
#include "themeprovider.h"
#include "wxfilesystem_blob_handler.h"
#include <libfilezilla/local_filesys.hpp>
#include <libfilezilla/translate.hpp>

#include <wx/evtloop.h>

#ifdef WITH_LIBDBUS
#include <../dbus/session_manager.h>
#endif

#if defined(__WXMAC__) || defined(__UNIX__)
#include <wx/stdpaths.h>
#elif defined(__WXMSW__)
#include <shobjidl.h>
#endif

#include "locale_initializer.h"

#ifdef USE_MAC_SANDBOX
#include "osx_sandbox_userdirs.h"
#endif

#if !defined(__WXGTK__) && !defined(__MINGW32__)
IMPLEMENT_APP(CFileZillaApp)
#else
IMPLEMENT_APP_NO_MAIN(CFileZillaApp)
#endif //__WXGTK__

#if !wxUSE_PRINTF_POS_PARAMS
  #error Please build wxWidgets with support for positional arguments.
#endif

namespace {
#if FZ_WINDOWS
std::wstring const PATH_SEP = L";";
#else
std::wstring const PATH_SEP = L":";
#endif
}

// If non-empty, always terminated by a separator
std::wstring GetOwnExecutableDir()
{
#ifdef FZ_WINDOWS
	// Add executable path
	std::wstring path;
	path.resize(4095);
	DWORD res;
	while (true) {
		res = GetModuleFileNameW(0, &path[0], path.size() - 1);
		if (!res) {
			// Failure
			return std::wstring();
		}

		if (res < path.size() - 1) {
			path.resize(res);
			break;
		}

		path.resize(path.size() * 2 + 1);
	}
	size_t pos = path.find_last_of(L"\\/");
	if (pos != std::wstring::npos) {
		return path.substr(0, pos + 1);
	}
#elif defined(FZ_MAC)
	// Fixme: Remove wxDependency and move entire function to libfilezilla
	std::wstring executable = wxStandardPaths::Get().GetExecutablePath().ToStdWstring();
	size_t pos = executable.rfind('/');
	if (pos != std::wstring::npos) {
		return executable.substr(0, pos + 1);
	}
#else
	std::string path;
	path.resize(4095);
	while (true) {
		int res = readlink("/proc/self/exe", &path[0], path.size());
		if (res < 0) {
			return std::wstring();
		}
		if (static_cast<size_t>(res) < path.size()) {
			path.resize(res);
			break;
		}
		path.resize(path.size() * 2 + 1);
	}
	size_t pos = path.rfind('/');
	if (pos != std::wstring::npos) {
		return fz::to_wstring(path.substr(0, pos + 1));
	}
#endif
	return std::wstring();
}

CFileZillaApp::CFileZillaApp()
{
	m_profile_start = fz::monotonic_clock::now();
	AddStartupProfileRecord("CFileZillaApp::CFileZillaApp()");
}

CFileZillaApp::~CFileZillaApp()
{
	COptions::Destroy();
}

void CFileZillaApp::InitLocale()
{
	wxString language = COptions::Get()->GetOption(OPTION_LANGUAGE);
	const wxLanguageInfo* pInfo = wxLocale::FindLanguageInfo(language);
	if (!language.empty()) {
#ifdef __WXGTK__
		if (CInitializer::error) {
			wxString error;

			wxLocale *loc = wxGetLocale();
			const wxLanguageInfo* currentInfo = loc ? loc->GetLanguageInfo(loc->GetLanguage()) : 0;
			if (!loc || !currentInfo) {
				if (!pInfo) {
					error.Printf(_("Failed to set language to %s, using default system language."),
						language);
				}
				else {
					error.Printf(_("Failed to set language to %s (%s), using default system language."),
						pInfo->Description, language);
				}
			}
			else {
				wxString currentName = currentInfo->CanonicalName;

				if (!pInfo) {
					error.Printf(_("Failed to set language to %s, using default system language (%s, %s)."),
						language, loc->GetLocale(),
						currentName);
				}
				else {
					error.Printf(_("Failed to set language to %s (%s), using default system language (%s, %s)."),
						pInfo->Description, language, loc->GetLocale(),
						currentName);
				}
			}

			error += _T("\n");
			error += _("Please make sure the requested locale is installed on your system.");
			wxMessageBoxEx(error, _("Failed to change language"), wxICON_EXCLAMATION);

			COptions::Get()->SetOption(OPTION_LANGUAGE, _T(""));
		}
#else
		if (!pInfo || !SetLocale(pInfo->Language)) {
			for (language = GetFallbackLocale(language); !language.empty(); language = GetFallbackLocale(language)) {
				const wxLanguageInfo* fallbackInfo = wxLocale::FindLanguageInfo(language);
				if (fallbackInfo && SetLocale(fallbackInfo->Language)) {
					COptions::Get()->SetOption(OPTION_LANGUAGE, language.ToStdWstring());
					return;
				}
			}
			COptions::Get()->SetOption(OPTION_LANGUAGE, std::wstring());
			if (pInfo && !pInfo->Description.empty()) {
				wxMessageBoxEx(wxString::Format(_("Failed to set language to %s (%s), using default system language"), pInfo->Description, language), _("Failed to change language"), wxICON_EXCLAMATION);
			}
			else {
				wxMessageBoxEx(wxString::Format(_("Failed to set language to %s, using default system language"), language), _("Failed to change language"), wxICON_EXCLAMATION);
			}
		}
#endif
	}
}

namespace {
std::wstring translator(char const* const t)
{
	return wxGetTranslation(t).ToStdWstring();
}

std::wstring translator_pf(char const* const singular, char const* const plural, int64_t n)
{
	// wxGetTranslation does not support 64bit ints on 32bit systems.
	if (n < 0) {
		n = -n;
	}
	return wxGetTranslation(singular, plural, (sizeof(unsigned int) < 8 && n > 1000000000) ? (1000000000 + n % 1000000000) : n).ToStdWstring();
}
}

bool CFileZillaApp::OnInit()
{
	AddStartupProfileRecord("CFileZillaApp::OnInit()");

	// Turn off idle events, we don't need them
	wxIdleEvent::SetMode(wxIDLE_PROCESS_SPECIFIED);

	wxUpdateUIEvent::SetMode(wxUPDATE_UI_PROCESS_SPECIFIED);

	fz::set_translators(translator, translator_pf);

#ifdef __WXMSW__
	SetCurrentProcessExplicitAppUserModelID(L"FileZilla.Client.AppID");
#endif

	//wxSystemOptions is slow, if a value is not set, it keeps querying the environment
	//each and every time...
	wxSystemOptions::SetOption(_T("filesys.no-mimetypesmanager"), 0);
	wxSystemOptions::SetOption(_T("window-default-variant"), _T(""));
#ifdef __WXMSW__
	wxSystemOptions::SetOption(_T("no-maskblt"), 0);
	wxSystemOptions::SetOption(_T("msw.window.no-clip-children"), 0);
	wxSystemOptions::SetOption(_T("msw.font.no-proof-quality"), 0);
	wxSystemOptions::SetOption(_T("msw.remap"), 0);
	wxSystemOptions::SetOption(_T("msw.staticbox.optimized-paint"), 0);
#endif
#ifdef __WXMAC__
	wxSystemOptions::SetOption(_T("mac.listctrl.always_use_generic"), 1);
	wxSystemOptions::SetOption(_T("mac.textcontrol-use-spell-checker"), 0);
#endif

	int cmdline_result = ProcessCommandLine();
	if (!cmdline_result) {
		return false;
	}

	LoadLocales();

	if (cmdline_result < 0) {
		if (m_pCommandLine) {
			m_pCommandLine->DisplayUsage();
		}
		return false;
	}

#if USE_MAC_SANDBOX
	// Set PUTTYDIR so that fzsftp uses the sandboxed home to put settings.
	std::wstring home = GetEnv("HOME");
	if (!home.empty()) {
		if (home.back() != '/') {
			home += '/';
		}
		wxSetEnv("PUTTYDIR", home + L".config/putty");
	}
#endif
	InitDefaultsDir();

	COptions::Init();

	InitLocale();

#ifndef _DEBUG
	const wxString& buildType = CBuildInfo::GetBuildType();
	if (buildType == _T("nightly")) {
		wxMessageBoxEx(_T("You are using a nightly development version of FileZilla 3, do not expect anything to work.\r\nPlease use the official releases instead.\r\n\r\n\
Unless explicitly instructed otherwise,\r\n\
DO NOT post bugreports,\r\n\
DO NOT use it in production environments,\r\n\
DO NOT distribute the binaries,\r\n\
DO NOT complain about it\r\n\
USE AT OWN RISK"), _T("Important Information"));
	}
	else {
		std::wstring v = GetEnv("FZDEBUG");
		if (v != L"1") {
			COptions::Get()->SetOption(OPTION_LOGGING_DEBUGLEVEL, 0);
			COptions::Get()->SetOption(OPTION_LOGGING_RAWLISTING, 0);
		}
	}
#endif

	if (!LoadResourceFiles()) {
		COptions::Destroy();
		return false;
	}

	themeProvider_ = std::make_unique<CThemeProvider>();
	CheckExistsFzsftp();
#if ENABLE_STORJ
	CheckExistsFzstorj();
#endif

#ifdef WITH_LIBDBUS
	CSessionManager::Init();
#endif

	// Load the text wrapping engine
	m_pWrapEngine = std::make_unique<CWrapEngine>();
	m_pWrapEngine->LoadCache();

	bool welcome_skip = false;
#ifdef USE_MAC_SANDBOX
	OSXSandboxUserdirs::Get().Load();

	if (OSXSandboxUserdirs::Get().GetDirs().empty()) {
		CWelcomeDialog welcome;
		welcome.Run(nullptr, false);
		welcome_skip = true;

		OSXSandboxUserdirsDialog dlg;
		dlg.Run(nullptr, true);
		if (OSXSandboxUserdirs::Get().GetDirs().empty()) {
			return false;
		}
    }
#endif

	CMainFrame *frame = new CMainFrame();
	frame->Show(true);
	SetTopWindow(frame);

	if (!welcome_skip) {
		CWelcomeDialog::RunDelayed(frame);
	}

	frame->ProcessCommandLine();
	frame->PostInitialize();

	ShowStartupProfile();

	return true;
}

int CFileZillaApp::OnExit()
{
	CContextManager::Get()->NotifyGlobalHandlers(STATECHANGE_QUITNOW);

	COptions::Get()->SaveIfNeeded();

#ifdef WITH_LIBDBUS
	CSessionManager::Uninit();
#endif

	if (GetMainLoop() && wxEventLoopBase::GetActive() != GetMainLoop()) {
		// There's open dialogs and such and we just have to quit _NOW_.
		// We cannot continue as wx proceeds to destroy all windows, which interacts
		// badly with nested event loops, virtually always causing a crash.
		// I very much prefer clean shutdown but it's impossible in this situation. Sad.
		_exit(0);
	}

	return wxApp::OnExit();
}

bool CFileZillaApp::FileExists(std::wstring const& file) const
{
	return fz::local_filesys::get_file_type(fz::to_native(file), true) == fz::local_filesys::file;
}

CLocalPath CFileZillaApp::GetDataDir(std::vector<std::wstring> const& fileToFind, std::wstring const& prefixSub, bool searchSelfDir) const
{
	/*
	 * Finding the resources in all cases is a difficult task,
	 * due to the huge variety of diffent systems and their filesystem
	 * structure.
	 * Basically we just check a couple of paths for presence of the resources,
	 * and hope we find them. If not, the user can still specify on the cmdline
	 * and using environment variables where the resources are.
	 *
	 * At least on OS X it's simple: All inside application bundle.
	 */

	CLocalPath ret;

	auto testPath = [&](std::wstring const& path) {
		ret = CLocalPath(path);
		if (ret.empty()) {
			return false;
		}

		for (auto const& file : fileToFind) {
			if (FileExists(ret.GetPath() + file)) {
				return true;
			}
		}
		return false;
	};

#ifdef __WXMAC__
	(void)prefixSub;

	if (searchSelfDir && testPath(wxStandardPaths::Get().GetDataDir().ToStdWstring())) {
		return ret;
	}

	return CLocalPath();
#else

	// First try the user specified data dir.
	if (searchSelfDir) {
		if (testPath(GetEnv("FZ_DATADIR"))) {
			return ret;
		}
	}

	std::wstring selfDir = GetOwnExecutableDir();
	if (!selfDir.empty()) {
		if (searchSelfDir && testPath(selfDir)) {
			return ret;
		}

		if (!prefixSub.empty() && selfDir.size() > 5 && fz::ends_with(selfDir, std::wstring(L"/bin/"))) {
			std::wstring path = selfDir.substr(0, selfDir.size() - 4) + prefixSub + L"/";
			if (testPath(path)) {
				return ret;
			}
		}

		// Development paths
		if (searchSelfDir && selfDir.size() > 7 && fz::ends_with(selfDir, std::wstring(L"/.libs/"))) {
			std::wstring path = selfDir.substr(0, selfDir.size() - 6);
			if (FileExists(path + L"Makefile")) {
				if (testPath(path)) {
					return ret;
				}
			}
		}
	}

	// Now scan through the path
	if (!prefixSub.empty()) {
		std::wstring path = GetEnv("PATH");
		auto const segments = fz::strtok(path, PATH_SEP);

		for (auto const& segment : segments) {
			auto const cur = CLocalPath(segment).GetPath();
			if (cur.size() > 5 && fz::ends_with(cur, std::wstring(L"/bin/"))) {
				std::wstring path = cur.substr(0, cur.size() - 4) + prefixSub + L"/";
				if (testPath(path)) {
					return ret;
				}
			}
		}
	}

	ret.clear();
	return ret;
#endif
}

bool CFileZillaApp::LoadResourceFiles()
{
	AddStartupProfileRecord("CFileZillaApp::LoadResourceFiles");
	m_resourceDir = GetDataDir({L"resources/defaultfilters.xml"}, L"share/filezilla");

	wxImage::AddHandler(new wxPNGHandler());

	if (m_resourceDir.empty()) {
		wxString msg = _("Could not find the resource files for FileZilla, closing FileZilla.\nYou can specify the data directory of FileZilla by setting the FZ_DATADIR environment variable.");
		wxMessageBoxEx(msg, _("FileZilla Error"), wxOK | wxICON_ERROR);
		return false;
	}
	else {
		m_resourceDir.AddSegment(_T("resources"));
	}

	// Useful for XRC files with embedded image data.
	wxFileSystem::AddHandler(new wxFileSystemBlobHandler);

	return true;
}

bool CFileZillaApp::InitDefaultsDir()
{
	AddStartupProfileRecord("InitDefaultsDir");
#ifdef __WXGTK__
	m_defaultsDir = COptions::GetUnadjustedSettingsDir();
	if (m_defaultsDir.empty() || !FileExists(m_defaultsDir.GetPath() + L"fzdefaults.xml")) {
		if (FileExists(L"/etc/filezilla/fzdefaults.xml")) {
			m_defaultsDir.SetPath(L"/etc/filezilla");
		}
		else {
			m_defaultsDir.clear();
		}
	}

#endif
	if (m_defaultsDir.empty()) {
		m_defaultsDir = GetDataDir({L"fzdefaults.xml"}, L"share/filezilla");
	}

	return !m_defaultsDir.empty();
}

bool CFileZillaApp::LoadLocales()
{
	AddStartupProfileRecord("CFileZillaApp::LoadLocales");
	m_localesDir = GetDataDir({L"locales/de/filezilla.mo"}, std::wstring());
	if (!m_localesDir.empty()) {
		m_localesDir.AddSegment(_T("locales"));
	}
#ifndef __WXMAC__
	else {
		m_localesDir = GetDataDir({L"de/filezilla.mo", L"de/LC_MESSAGES/filezilla.mo"}, L"share/locale", false);
	}
#endif
	if (!m_localesDir.empty()) {
		wxLocale::AddCatalogLookupPathPrefix(m_localesDir.GetPath());
	}

	SetLocale(wxLANGUAGE_DEFAULT);

	return true;
}

bool CFileZillaApp::SetLocale(int language)
{
	// First check if we can load the new locale
	auto pLocale = std::make_unique<wxLocale>();
	wxLogNull log;
	pLocale->Init(language);
	if (!pLocale->IsOk()) {
		return false;
	}

	// Load catalog. Only fail if a non-default language has been selected
	if (!pLocale->AddCatalog(_T("filezilla")) && language != wxLANGUAGE_DEFAULT) {
		return false;
	}
	pLocale->AddCatalog(_T("libfilezilla"));

	if (m_pLocale) {
		// Now unload old locale
		// We unload new locale as well, else the internal locale chain in wxWidgets get's broken.
		pLocale.reset();
		m_pLocale.reset();

		// Finally load new one
		pLocale = std::make_unique<wxLocale>();
		pLocale->Init(language);
		if (!pLocale->IsOk()) {
			return false;
		}
		else if (!pLocale->AddCatalog(_T("filezilla")) && language != wxLANGUAGE_DEFAULT) {
			return false;
		}
		pLocale->AddCatalog(_T("libfilezilla"));
	}
	m_pLocale = std::move(pLocale);

	return true;
}

int CFileZillaApp::GetCurrentLanguage() const
{
	if (!m_pLocale) {
		return wxLANGUAGE_ENGLISH;
	}

	return m_pLocale->GetLanguage();
}

wxString CFileZillaApp::GetCurrentLanguageCode() const
{
	if (!m_pLocale) {
		return wxString();
	}

	return m_pLocale->GetCanonicalName();
}

#if wxUSE_DEBUGREPORT && wxUSE_ON_FATAL_EXCEPTION
void CFileZillaApp::OnFatalException()
{
}
#endif

void CFileZillaApp::DisplayEncodingWarning()
{
	static bool displayedEncodingWarning = false;
	if (displayedEncodingWarning) {
		return;
	}

	displayedEncodingWarning = true;

	wxMessageBoxEx(_("A local filename could not be decoded.\nPlease make sure the LC_CTYPE (or LC_ALL) environment variable is set correctly.\nUnless you fix this problem, files might be missing in the file listings.\nNo further warning will be displayed this session."), _("Character encoding issue"), wxICON_EXCLAMATION);
}

CWrapEngine* CFileZillaApp::GetWrapEngine()
{
	return m_pWrapEngine.get();
}

void CFileZillaApp::CheckExistsFzsftp()
{
	AddStartupProfileRecord("FileZillaApp::CheckExistsFzsftp");
	CheckExistsTool(L"fzsftp", L"../putty/", "FZ_FZSFTP", OPTION_FZSFTP_EXECUTABLE, fztranslate("SFTP support"));
}

#if ENABLE_STORJ
void CFileZillaApp::CheckExistsFzstorj()
{
	AddStartupProfileRecord("FileZillaApp::CheckExistsFzstorj");
	CheckExistsTool(L"fzstorj", L"../storj/", "FZ_FZSTORJ", OPTION_FZSTORJ_EXECUTABLE, fztranslate("Storj support"));
}
#endif

void CFileZillaApp::CheckExistsTool(std::wstring const& tool, std::wstring const& buildRelPath, char const* env, int setting, std::wstring const& description)
{
	// Get the correct path to the specified tool

	bool found = false;
	std::wstring executable;

	std::wstring program = tool;
#ifdef __WXMAC__
	(void)buildRelPath;

	// On Mac we only look inside the bundle
	std::wstring path = GetOwnExecutableDir();
	if (!path.empty()) {
		executable = path + '/' + tool;
		if (FileExists(executable)) {
			found = true;
		}
	}
#else

#ifdef __WXMSW__
	program += L".exe";
#endif

	// First check the given environment variable
	executable = GetEnv(env);
	if (!executable.empty()) {
		if (FileExists(executable)) {
			found = true;
		}
	}

	if (!found) {
		std::wstring path = GetOwnExecutableDir();
		if (!path.empty()) {
			executable = path + program;
			if (FileExists(executable)) {
				found = true;
			}
			else {
				// Check if running from build dir
				if (path.size() > 7 && fz::ends_with(path, std::wstring(L"/.libs/"))) {
					if (FileExists(path.substr(0, path.size() - 6) + L"Makefile")) {
						executable = path + L"../" + buildRelPath + program;
						if (FileExists(executable)) {
							found = true;
						}
					}
				}
				else if (FileExists(path + L"Makefile")) {
					executable = path + buildRelPath + program;
					if (FileExists(executable)) {
						found = true;
					}
				}
			}
		}
	}

	if (!found) {
		// Check PATH
		std::wstring path = GetEnv("PATH");
		auto const segments = fz::strtok(path, PATH_SEP);
		for (auto const& segment : segments) {
			auto const cur = CLocalPath(segment).GetPath();
			executable = cur + program;
			if (!cur.empty() && FileExists(executable)) {
				found = true;
				break;
			}
		}
	}
#endif

	if (!found) {
		// Quote path if it contains spaces
		if (executable.find(' ') != std::wstring::npos && executable.front() != '"' && executable.front() != '\'') {
			executable = L"\"" + executable + L"\"";
		}

		wxMessageBoxEx(fz::sprintf(fztranslate("%s could not be found. Without this component of FileZilla, %s will not work.\n\nPossible solutions:\n- Make sure %s is in a directory listed in your PATH environment variable.\n- Set the full path to %s in the %s environment variable."), program, description, program, program, env),
			_("File not found"), wxICON_ERROR | wxOK);
		executable.clear();
	}
	COptions::Get()->SetOption(setting, executable);
}

#ifdef __WXMSW__
extern "C" BOOL CALLBACK EnumWindowCallback(HWND hwnd, LPARAM)
{
	HWND child = FindWindowEx(hwnd, 0, 0, _T("FileZilla process identificator 3919DB0A-082D-4560-8E2F-381A35969FB4"));
	if (child) {
		::PostMessage(hwnd, WM_ENDSESSION, (WPARAM)TRUE, (LPARAM)ENDSESSION_LOGOFF);
	}

	return TRUE;
}
#endif

int CFileZillaApp::ProcessCommandLine()
{
	AddStartupProfileRecord("CFileZillaApp::ProcessCommandLine");
	m_pCommandLine = std::make_unique<CCommandLine>(argc, argv);
	int res = m_pCommandLine->Parse() ? 1 : -1;

	if (res > 0) {
		if (m_pCommandLine->HasSwitch(CCommandLine::close)) {
#ifdef __WXMSW__
			EnumWindows((WNDENUMPROC)EnumWindowCallback, 0);
#endif
			return 0;
		}

		if (m_pCommandLine->HasSwitch(CCommandLine::version)) {
			wxString out = wxString::Format(_T("FileZilla %s"), CBuildInfo::GetVersion());
			if (!CBuildInfo::GetBuildType().empty()) {
				out += _T(" ") + CBuildInfo::GetBuildType() + _T(" build");
			}
			out += _T(", compiled on ") + CBuildInfo::GetBuildDateString();

			printf("%s\n", (const char*)out.mb_str());
			return 0;
		}
	}

	return res;
}

void CFileZillaApp::AddStartupProfileRecord(std::string const& msg)
{
	if (!m_profile_start) {
		return;
	}

	m_startupProfile.emplace_back(fz::monotonic_clock::now(), msg);
}

void CFileZillaApp::ShowStartupProfile()
{
	if (m_profile_start && m_pCommandLine && m_pCommandLine->HasSwitch(CCommandLine::debug_startup)) {
		AddStartupProfileRecord("CFileZillaApp::ShowStartupProfile");
		wxString msg = _T("Profile:\n");

		size_t const max_digits = fz::to_string((m_startupProfile.back().first - m_profile_start).get_milliseconds()).size();
		
		int64_t prev{};
		for (auto const& p : m_startupProfile) {
			auto const diff = p.first - m_profile_start;
			auto absolute = std::to_wstring(diff.get_milliseconds());
			if (absolute.size() < max_digits) {
				msg.append(max_digits - absolute.size(), wchar_t(0x2007)); // FIGURE SPACE
			}
			msg += absolute;
			msg += L" ";

			auto relative = std::to_wstring(diff.get_milliseconds() - prev);
			if (relative.size() < max_digits) {
				msg.append(max_digits - relative.size(), wchar_t(0x2007)); // FIGURE SPACE
			}
			msg += relative;
			msg += L" ";

			msg += fz::to_wstring(p.second);
			msg += L"\n";

			prev = diff.get_milliseconds();
		}
		wxMessageBoxEx(msg);
	}

	m_profile_start = fz::monotonic_clock();
	m_startupProfile.clear();
}

std::wstring CFileZillaApp::GetSettingsFile(std::wstring const& name) const
{
	return COptions::Get()->GetOption(OPTION_DEFAULT_SETTINGSDIR) + name + _T(".xml");
}

#if defined(__MINGW32__)
extern "C" {
__declspec(dllexport) // This forces ld to not strip relocations so that ASLR can work on MSW.
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR, int nCmdShow)
{
	wxDISABLE_DEBUG_SUPPORT();
	return wxEntry(hInstance, hPrevInstance, NULL, nCmdShow);
}
}
#endif

