#include <filezilla.h>
#include "Options.h"
#include "filezillaapp.h"
#include "file_utils.h"
#include "ipcmutex.h"
#include "locale_initializer.h"
#include <option_change_event_handler.h>
#include "sizeformatting.h"

#include <algorithm>
#include <string>

#include <stdlib.h>

#ifdef FZ_WINDOWS
	#include <shlobj.h>

	// Needed for MinGW:
	#ifndef SHGFP_TYPE_CURRENT
		#define SHGFP_TYPE_CURRENT 0
	#endif
#endif

COptions* COptions::m_theOptions = 0;

enum Type
{
	string,
	number,
	xml
};

enum Flags
{
	normal = 0,
	internal = 1,
	default_only = 2,
	default_priority = 4, // If that option is given in fzdefaults.xml, it overrides any user option
	platform = 8 // A non-portable platform specific option, nodes have platform attribute
};

struct t_Option
{
	const char name[40];
	const Type type;
	const std::wstring defaultValue; // Default values are stored as string even for numerical options
	const Flags flags; // internal items won't get written to settings file nor loaded from there
};

#ifdef FZ_WINDOWS
//case insensitive
#define DEFAULT_FILENAME_SORT   L"0"
#else
//case sensitive
#define DEFAULT_FILENAME_SORT   L"1"
#endif

namespace {
#ifdef FZ_WINDOWS
	auto const platform_name = "win";
#elif defined(FZ_MAC)
	auto const platform_name = "mac";
#else
	auto const platform_name = "unix";
#endif
}

static_assert(OPTIONS_NUM <= changed_options_t().size());

static const t_Option options[OPTIONS_NUM] =
{
	// Note: A few options are versioned due to a changed
	// option syntax or past, unhealthy defaults

	// Engine settings
	{ "Use Pasv mode", number, L"1", normal },
	{ "Limit local ports", number, L"0", normal },
	{ "Limit ports low", number, L"6000", normal },
	{ "Limit ports high", number, L"7000", normal },
	{ "Limit ports offset", number, L"0", normal },
	{ "External IP mode", number, L"0", normal },
	{ "External IP", string, L"", normal },
	{ "External address resolver", string, L"http://ip.filezilla-project.org/ip.php", normal },
	{ "Last resolved IP", string, L"", normal },
	{ "No external ip on local conn", number, L"1", normal },
	{ "Pasv reply fallback mode", number, L"0", normal },
	{ "Timeout", number, L"20", normal },
	{ "Logging Debug Level", number, L"0", normal },
	{ "Logging Raw Listing", number, L"0", normal },
	{ "fzsftp executable", string, L"", internal },
	{ "fzstorj executable", string, L"", internal },
	{ "Allow transfermode fallback", number, L"1", normal },
	{ "Reconnect count", number, L"2", normal },
	{ "Reconnect delay", number, L"5", normal },
	{ "Enable speed limits", number, L"0", normal },
	{ "Speedlimit inbound", number, L"1000", normal },
	{ "Speedlimit outbound", number, L"100", normal },
	{ "Speedlimit burst tolerance", number, L"0", normal },
	{ "Preallocate space", number, L"0", normal },
	{ "View hidden files", number, L"0", normal },
	{ "Preserve timestamps", number, L"0", normal },
	{ "Socket recv buffer size (v2)", number, L"4194304", normal }, // Make it large enough by default
														 // to enable a large TCP window scale
	{ "Socket send buffer size (v2)", number, L"262144", normal },
	{ "FTP Keep-alive commands", number, L"0", normal },
	{ "FTP Proxy type", number, L"0", normal },
	{ "FTP Proxy host", string, L"", normal },
	{ "FTP Proxy user", string, L"", normal },
	{ "FTP Proxy password", string, L"", normal },
	{ "FTP Proxy login sequence", string, L"", normal },
	{ "SFTP keyfiles", string, L"", platform },
	{ "SFTP compression", number, L"", normal },
	{ "Proxy type", number, L"0", normal },
	{ "Proxy host", string, L"", normal },
	{ "Proxy port", number, L"0", normal },
	{ "Proxy user", string, L"", normal },
	{ "Proxy password", string, L"", normal },
	{ "Logging file", string, L"", platform },
	{ "Logging filesize limit", number, L"10", normal },
	{ "Logging show detailed logs", number, L"0", internal },
	{ "Size format", number, L"0", normal },
	{ "Size thousands separator", number, L"1", normal },
	{ "Size decimal places", number, L"1", normal },
	{ "TCP Keepalive Interval", number, L"15", normal },
	{ "Cache TTL", number, L"600", normal },

	// Interface settings
	{ "Number of Transfers", number, L"2", normal },
	{ "Ascii Binary mode", number, L"0", normal },
	{ "Auto Ascii files", string, L"ac|am|asp|bat|c|cfm|cgi|conf|cpp|css|dhtml|diff|diz|h|hpp|htm|html|in|inc|java|js|jsp|lua|m4|mak|md5|nfo|nsh|nsi|pas|patch|pem|php|phtml|pl|po|pot|py|qmail|sh|sha1|sha256|sha512|shtml|sql|svg|tcl|tpl|txt|vbs|xhtml|xml|xrc", normal },
	{ "Auto Ascii no extension", number, L"1", normal },
	{ "Auto Ascii dotfiles", number, L"1", normal },
	{ "Language Code", string, L"", normal },
	{ "Concurrent download limit", number, L"0", normal },
	{ "Concurrent upload limit", number, L"0", normal },
	{ "Update Check", number, L"1", normal },
	{ "Update Check Interval", number, L"7", normal },
	{ "Last automatic update check", string, L"", normal },
	{ "Last automatic update version", string, L"", normal },
	{ "Update Check New Version", string, L"", platform },
	{ "Update Check Check Beta", number, L"0", normal },
	{ "Show debug menu", number, L"0", normal },
	{ "File exists action download", number, L"0", normal },
	{ "File exists action upload", number, L"0", normal },
	{ "Allow ascii resume", number, L"0", normal },
	{ "Greeting version", string, L"", normal },
	{ "Greeting resources", string, L"", normal },
	{ "Onetime Dialogs", string, L"", normal },
	{ "Show Tree Local", number, L"1", normal },
	{ "Show Tree Remote", number, L"1", normal },
	{ "File Pane Layout", number, L"0", normal },
	{ "File Pane Swap", number, L"0", normal },
	{ "Filelist directory sort", number, L"0", normal },
	{ "Filelist name sort", number, DEFAULT_FILENAME_SORT, normal },
	{ "Queue successful autoclear", number, L"0", normal },
	{ "Queue column widths", string, L"", normal },
	{ "Local filelist colwidths", string, L"", normal },
	{ "Remote filelist colwidths", string, L"", normal },
	{ "Window position and size", string, L"", normal },
	{ "Splitter positions (v2)", string, L"", normal },
	{ "Local filelist sortorder", string, L"", normal },
	{ "Remote filelist sortorder", string, L"", normal },
	{ "Time Format", string, L"", normal },
	{ "Date Format", string, L"", normal },
	{ "Show message log", number, L"1", normal },
	{ "Show queue", number, L"1", normal },
	{ "Default editor", string, L"", platform },
	{ "Always use default editor", number, L"0", normal },
	{ "File associations", string, L"", platform },
	{ "Comparison mode", number, L"1", normal },
	{ "Comparison threshold", number, L"1", normal },
	{ "Site Manager position", string, L"", normal },
	{ "Icon theme", string, L"default", normal },
	{ "Icon scale", number, L"125", normal },
	{ "Timestamp in message log", number, L"0", normal },
	{ "Sitemanager last selected", string, L"", normal },
	{ "Local filelist shown columns", string, L"", normal },
	{ "Remote filelist shown columns", string, L"", normal },
	{ "Local filelist column order", string, L"", normal },
	{ "Remote filelist column order", string, L"", normal },
	{ "Filelist status bar", number, L"1", normal },
	{ "Filter toggle state", number, L"0", normal },
	{ "Show quickconnect bar", number, L"1", normal },
	{ "Messagelog position", number, L"0", normal },
	{ "File doubleclick action", number, L"0", normal },
	{ "Dir doubleclick action", number, L"0", normal },
	{ "Minimize to tray", number, L"0", normal },
	{ "Search column widths", string, L"", normal },
	{ "Search column shown", string, L"", normal },
	{ "Search column order", string, L"", normal },
	{ "Search window size", string, L"", normal },
	{ "Comparison hide identical", number, L"0", normal },
	{ "Search sort order", string, L"", normal },
	{ "Edit track local", number, L"1", normal },
	{ "Prevent idle sleep", number, L"1", normal },
	{ "Filteredit window size", string, L"", normal },
	{ "Enable invalid char filter", number, L"1", normal },
	{ "Invalid char replace", string, L"_", normal },
	{ "Already connected choice", number, L"0", normal },
	{ "Edit status dialog size", string, L"", normal },
	{ "Display current speed", number, L"0", normal },
	{ "Toolbar hidden", number, L"0", normal },
	{ "Strip VMS revisions", number, L"0", normal },
	{ "Startup action", number, L"0", normal },
	{ "Prompt password save", number, L"0", normal },
	{ "Persistent Choices", number, L"0", normal },
	{ "Queue completion action", number, L"1", normal },
	{ "Queue completion command", string, L"", normal },
	{ "Drag and Drop disabled", number, L"0", normal },
	{ "Disable update footer", number, L"0", normal },
	{ "Master password encryptor", string, L"", normal },
	{ "Tab data", xml, std::wstring(), normal },

	// Default/internal options
	{ "Config Location", string, L"", static_cast<Flags>(default_only|platform) },
	{ "Kiosk mode", number, L"0", default_priority },
	{ "Disable update check", number, L"0", default_only },
	{ "Cache directory", string, L"", static_cast<Flags>(default_priority|platform) },
};

std::wstring GetEnv(char const* name)
{
	std::wstring ret;
	if (name) {
		auto* v = getenv(name);
		if (v) {
			ret = fz::to_wstring(v);
		}
	}
	return ret;
}

BEGIN_EVENT_TABLE(COptions, wxEvtHandler)
EVT_TIMER(wxID_ANY, COptions::OnTimer)
END_EVENT_TABLE()

t_OptionsCache& t_OptionsCache::operator=(std::wstring_view const& v)
{
	strValue = v;
	numValue = fz::to_integral<int>(v);
	return *this;
}

t_OptionsCache& t_OptionsCache::operator=(std::wstring && v)
{
	numValue = fz::to_integral<int>(v);
	strValue = std::move(v);
	return *this;
}

t_OptionsCache& t_OptionsCache::operator=(int v)
{
	numValue = v;
	strValue = fz::to_wstring(v);
	return *this;
}

t_OptionsCache& t_OptionsCache::operator=(std::unique_ptr<pugi::xml_document> const& v)
{
	xmlValue = std::make_unique<pugi::xml_document>();
	for (auto c = v->first_child(); c; c = c.next_sibling()) {
		xmlValue->append_copy(c);
	}

	return *this;
}

COptions::COptions()
{
	m_theOptions = this;

	SetDefaultValues();

	m_save_timer.SetOwner(this);

	auto const nameOptionMap = GetNameOptionMap();
	LoadGlobalDefaultOptions(nameOptionMap);

	CLocalPath const dir = InitSettingsDir();

	CInterProcessMutex mutex(MUTEX_OPTIONS);
	xmlFile_ = std::make_unique<CXmlFile>(dir.GetPath() + L"filezilla.xml");
	if (!xmlFile_->Load()) {
		wxString msg = xmlFile_->GetError() + L"\n\n" + _("For this session the default settings will be used. Any changes to the settings will not be saved.");
		wxMessageBoxEx(msg, _("Error loading xml file"), wxICON_ERROR);
		xmlFile_.reset();
	}
	else {
		CreateSettingsXmlElement();
	}

	LoadOptions(nameOptionMap);
}

std::map<std::string, unsigned int> COptions::GetNameOptionMap() const
{
	std::map<std::string, unsigned int> ret;
	for (unsigned int i = 0; i < OPTIONS_NUM; ++i) {
		if (!(options[i].flags & internal)) {
			ret.insert(std::make_pair(std::string(options[i].name), i));
		}
	}
	return ret;
}

COptions::~COptions()
{
	COptionChangeEventHandler::UnregisterAllHandlers();
}

int COptions::GetOptionVal(unsigned int nID)
{
	if (nID >= OPTIONS_NUM) {
		return 0;
	}

	fz::scoped_lock l(m_sync_);
	return m_optionsCache[nID].numValue;
}

std::wstring COptions::GetOption(unsigned int nID)
{
	if (nID >= OPTIONS_NUM) {
		return std::wstring();
	}

	fz::scoped_lock l(m_sync_);
	return m_optionsCache[nID].strValue;
}

std::unique_ptr<pugi::xml_document> COptions::GetOptionXml(unsigned int nID)
{
	if (nID >= OPTIONS_NUM) {
		return nullptr;
	}

	auto value = std::make_unique<pugi::xml_document>();
	for (auto c = m_optionsCache[nID].xmlValue->first_child(); c; c = c.next_sibling()) {
		value->append_copy(c);
	}

	return value;
}

bool COptions::SetOption(unsigned int nID, int value)
{
	if (nID >= OPTIONS_NUM) {
		return false;
	}

	if (options[nID].type != number) {
		return false;
	}

	ContinueSetOption(nID, value);
	return true;
}

bool COptions::SetOption(unsigned int nID, std::wstring_view const& value)
{
	if (nID >= OPTIONS_NUM) {
		return false;
	}

	if (options[nID].type != string) {
		return SetOption(nID, fz::to_integral<int>(value));
	}

	ContinueSetOption(nID, value);
	return true;
}

bool COptions::SetOptionXml(unsigned int nID, pugi::xml_node const& value)
{
	if (nID >= OPTIONS_NUM) {
		return false;
	}

	if (options[nID].type != xml) {
		return false;
	}

	auto doc = std::make_unique<pugi::xml_document>();
	if (value) {
		if (value.type() == pugi::node_document) {
			for (auto c = value.first_child(); c; c = c.next_sibling()) {
				if (c.type() == pugi::node_element) {
					doc->append_copy(c);
				}
			}
		}
		else {
			doc->append_copy(value);
		}
	}

	ContinueSetOption(nID, doc);

	return true;
}

template<typename T>
void COptions::ContinueSetOption(unsigned int nID, T const& value)
{
	auto validated = Validate(nID, value);

	{
		fz::scoped_lock l(m_sync_);
		if (m_optionsCache[nID] == validated) {
			// Nothing to do
			return;
		}
		m_optionsCache[nID] = validated;
	}

	// Fixme: Setting options from other threads
	if (!wxIsMainThread()) {
		return;
	}

	if (!(options[nID].flags & (internal | default_only))) {
		SetXmlValue(nID, validated);

		if (!m_save_timer.IsRunning()) {
			m_save_timer.Start(15000, true);
		}
	}

	if (changedOptions_.none()) {
		CallAfter(&COptions::NotifyChangedOptions);
	}
	changedOptions_.set(nID);
}

void COptions::NotifyChangedOptions()
{
	// Reset prior to notifying to correctly handle the case of an option being set while notifying
	auto changedOptions = changedOptions_;
	changedOptions_.reset();
	COptionChangeEventHandler::DoNotify(changedOptions);
}

bool COptions::OptionFromFzDefaultsXml(unsigned int nID)
{
	if (nID >= OPTIONS_NUM) {
		return false;
	}

	fz::scoped_lock l(m_sync_);
	return m_optionsCache[nID].from_default;
}

pugi::xml_node COptions::CreateSettingsXmlElement()
{
	if (!xmlFile_) {
		return pugi::xml_node();
	}

	auto element = xmlFile_->GetElement();
	if (!element) {
		return element;
	}

	auto settings = element.child("Settings");
	if (settings) {
		return settings;
	}

	settings = element.append_child("Settings");
	for (int i = 0; i < OPTIONS_NUM; ++i) {
		if (options[i].type == string) {
			SetXmlValue(i, GetOption(i));
		}
		else if (options[i].type == xml) {
			SetXmlValue(i, GetOptionXml(i));
		}
		else {
			SetXmlValue(i, GetOptionVal(i));
		}
	}

	return settings;
}

void COptions::SetXmlValue(unsigned int nID, int value)
{
	SetXmlValue(nID, fz::to_wstring(value));
}

void COptions::SetXmlValue(unsigned int nID, std::wstring_view const& value)
{
	if (!xmlFile_) {
		return;
	}

	// No checks are made about the validity of the value, that's done in SetOption
	std::string utf8 = fz::to_utf8(value);

	auto settings = CreateSettingsXmlElement();
	if (settings) {
		pugi::xml_node setting;
		for (pugi::xml_node it = settings.child("Setting"); it;) {
			auto cur = it;
			it = it.next_sibling("Setting");

			char const *attribute = cur.attribute("name").value();
			if (strcmp(attribute, options[nID].name)) {
				continue;
			}

			if (options[nID].flags & platform) {
				// Ignore items from the wrong platform
				char const* p = cur.attribute("platform").value();
				if (*p && strcmp(p, platform_name)) {
					continue;
				}
			}

			if (setting) {
				// Remove duplicates
				settings.remove_child(cur);
			}
			else {
				setting = cur;
			}
			break;
		}
		if (!setting) {
			setting = settings.append_child("Setting");
			SetTextAttribute(setting, "name", options[nID].name);
		}
		if (options[nID].flags & platform) {
			SetTextAttribute(setting, "platform", platform_name);
		}
		setting.text() = utf8.c_str();
	}
}

void COptions::SetXmlValue(unsigned int nID, std::unique_ptr<pugi::xml_document> const& value)
{
	if (!xmlFile_) {
		return;
	}

	auto settings = CreateSettingsXmlElement();
	if (settings) {
		pugi::xml_node setting;
		for (pugi::xml_node it = settings.child("Setting"); it;) {
			auto cur = it;
			it = it.next_sibling("Setting");

			const char *attribute = cur.attribute("name").value();
			if (strcmp(attribute, options[nID].name)) {
				continue;
			}
			if (options[nID].flags & platform) {
				// Ignore items from the wrong platform
				char const* p = cur.attribute("platform").value();
				if (*p && strcmp(p, platform_name)) {
					continue;
				}
			}
			break;
		}
		if (setting) {
			settings.remove_child(setting);
		}
		if (value && value->first_child()) {
			setting = settings.append_child("Setting");
			SetTextAttribute(setting, "name", options[nID].name);
			if (options[nID].flags & platform) {
				SetTextAttribute(setting, "platform", platform_name);
			}

			for (auto c = value->first_child(); c; c = c.next_sibling()) {
				setting.append_copy(c);
			}
		}
	}
}

int COptions::Validate(unsigned int nID, int value)
{
	switch (nID)
	{
	case OPTION_UPDATECHECK_INTERVAL:
		if (value < 1 || value > 7) {
			value = 7;
		}
		break;
	case OPTION_LOGGING_DEBUGLEVEL:
		if (value < 0 || value > 4) {
			value = 0;
		}
		break;
	case OPTION_RECONNECTCOUNT:
		if (value < 0 || value > 99) {
			value = 5;
		}
		break;
	case OPTION_RECONNECTDELAY:
		if (value < 0 || value > 999) {
			value = 5;
		}
		break;
	case OPTION_FILEPANE_LAYOUT:
		if (value < 0 || value > 3) {
			value = 0;
		}
		break;
	case OPTION_SPEEDLIMIT_INBOUND:
	case OPTION_SPEEDLIMIT_OUTBOUND:
		if (value < 0) {
			value = 0;
		}
		break;
	case OPTION_SPEEDLIMIT_BURSTTOLERANCE:
		if (value < 0 || value > 2) {
			value = 0;
		}
		break;
	case OPTION_FILELIST_DIRSORT:
	case OPTION_FILELIST_NAMESORT:
		if (value < 0 || value > 2) {
			value = 0;
		}
		break;
	case OPTION_SOCKET_BUFFERSIZE_RECV:
		if (value != -1 && (value < 4096 || value > 4096 * 1024)) {
			value = -1;
		}
		break;
	case OPTION_SOCKET_BUFFERSIZE_SEND:
		if (value != -1 && (value < 4096 || value > 4096 * 1024)) {
			value = 131072;
		}
		break;
	case OPTION_COMPARISONMODE:
		if (value < 0 || value > 0) {
			value = 1;
		}
		break;
	case OPTION_COMPARISON_THRESHOLD:
		if (value < 0 || value > 1440) {
			value = 1;
		}
		break;
	case OPTION_SIZE_DECIMALPLACES:
		if (value < 0 || value > 3) {
			value = 0;
		}
		break;
	case OPTION_MESSAGELOG_POSITION:
		if (value < 0 || value > 2) {
			value = 0;
		}
		break;
	case OPTION_DOUBLECLICK_ACTION_FILE:
	case OPTION_DOUBLECLICK_ACTION_DIRECTORY:
		if (value < 0 || value > 3) {
			value = 0;
		}
		break;
	case OPTION_SIZE_FORMAT:
		if (value < 0 || value >= CSizeFormat::formats_count) {
			value = 0;
		}
		break;
	case OPTION_TIMEOUT:
		if (value <= 0) {
			value = 0;
		}
		else if (value < 10) {
			value = 10;
		}
		else if (value > 9999) {
			value = 9999;
		}
		break;
	case OPTION_CACHE_TTL:
		if (value < 30) {
			value = 30;
		}
		else if (value > 60*60*24) {
			value = 60 * 60 * 24;
		}
		break;
	case OPTION_ICONS_SCALE:
		if (value < 25) {
			value = 25;
		}
		else if (value > 400) {
			value = 400;
		}
		break;
	}
	return value;
}

std::wstring COptions::Validate(unsigned int nID, std::wstring_view const& value)
{
	if (nID == OPTION_INVALID_CHAR_REPLACE) {
		if (value.size() != 1) {
			return L"_";
		}
		if (IsInvalidChar(value[0], true)) {
			return L"_";
		}
	}
	return std::wstring(value);
}

std::unique_ptr<pugi::xml_document> COptions::Validate(unsigned int, std::unique_ptr<pugi::xml_document> const& value)
{
	auto res = std::make_unique<pugi::xml_document>();
	for (auto c = value->first_child(); c; c = c.next_sibling()) {
		if (c.type() == pugi::node_element) {
			res->append_copy(c);
		}
	}

	return res;
}

void COptions::Init()
{
	if (!m_theOptions) {
		new COptions(); // It sets m_theOptions internally itself
	}
}

void COptions::Destroy()
{
	if (!m_theOptions) {
		return;
	}

	delete m_theOptions;
	m_theOptions = 0;
}

COptions* COptions::Get()
{
	return m_theOptions;
}

void COptions::Import(pugi::xml_node element)
{
	LoadOptions(GetNameOptionMap(), element);
	if (!m_save_timer.IsRunning()) {
		m_save_timer.Start(15000, true);
	}
}

void COptions::LoadOptions(std::map<std::string, unsigned int> const& nameOptionMap, pugi::xml_node settings)
{
	if (!settings) {
		settings = CreateSettingsXmlElement();
		if (!settings) {
			return;
		}
	}

	for (auto setting = settings.child("Setting"); setting; setting = setting.next_sibling("Setting")) {
		LoadOptionFromElement(setting, nameOptionMap, false);
	}
}

void COptions::LoadOptionFromElement(pugi::xml_node option, std::map<std::string, unsigned int> const& nameOptionMap, bool allowDefault)
{
	const char* name = option.attribute("name").value();
	if (!name) {
		return;
	}

	auto const iter = nameOptionMap.find(name);
	if (iter != nameOptionMap.end()) {
		if (!allowDefault && options[iter->second].flags & default_only) {
			return;
		}

		if (options[iter->second].flags & platform) {
			char const* p = option.attribute("platform").value();
			if (*p && strcmp(p, platform_name)) {
				return;
			}
		}

		std::wstring value = GetTextElement(option);
		if (options[iter->second].flags & default_priority) {
			if (allowDefault) {
				fz::scoped_lock l(m_sync_);
				m_optionsCache[iter->second].from_default = true;
			}
			else {
				fz::scoped_lock l(m_sync_);
				if (m_optionsCache[iter->second].from_default) {
					return;
				}
			}
		}

		if (options[iter->second].type == number) {
			int numValue = fz::to_integral<int>(value);
			numValue = Validate(iter->second, numValue);
			fz::scoped_lock l(m_sync_);
			m_optionsCache[iter->second] = numValue;
		}
		else if (options[iter->second].type == string) {
			value = Validate(iter->second, value);
			fz::scoped_lock l(m_sync_);
			m_optionsCache[iter->second] = value;
		}
		else {
			fz::scoped_lock l(m_sync_);
			if (!option.first_child().empty()) {
				m_optionsCache[iter->second].xmlValue = std::make_unique<pugi::xml_document>();
				for (auto c = option.first_child(); c; c = c.next_sibling()) {
					m_optionsCache[iter->second].xmlValue->append_copy(c);
				}
			}
		}
	}
}

void COptions::LoadGlobalDefaultOptions(std::map<std::string, unsigned int> const& nameOptionMap)
{
	CLocalPath const defaultsDir = wxGetApp().GetDefaultsDir();
	if (defaultsDir.empty()) {
		return;
	}
	CXmlFile file(defaultsDir.GetPath() + L"fzdefaults.xml");
	if (!file.Load()) {
		return;
	}

	auto element = file.GetElement();
	if (!element) {
		return;
	}

	element = element.child("Settings");
	if (!element) {
		return;
	}

	for (auto setting = element.child("Setting"); setting; setting = setting.next_sibling("Setting")) {
		LoadOptionFromElement(setting, nameOptionMap, true);
	}
}

void COptions::OnTimer(wxTimerEvent&)
{
	Save();
}

void COptions::Save()
{
	if (GetOptionVal(OPTION_DEFAULT_KIOSKMODE) == 2) {
		return;
	}

	if (!xmlFile_) {
		return;
	}

	CInterProcessMutex mutex(MUTEX_OPTIONS);
	xmlFile_->Save(true);
}

bool COptions::Cleanup()
{
	bool ret = false;

	needsCleanup_ = false;
	auto element = xmlFile_->GetElement();
	auto child = element.first_child();

	// Remove all but the one settings element
	while (child) {
		auto next = child.next_sibling();

		if (child.name() == std::string("Settings")) {
			break;
		}
		element.remove_child(child);
		child = next;
		ret = true;
	}

	pugi::xml_node next;
	while ((next = child.next_sibling())) {
		element.remove_child(next);
	}

	auto settings = child;
	child = settings.first_child();

	auto const nameOptionMap = GetNameOptionMap();

	// Remove unknown settings
	while (child) {
		auto next = child.next_sibling();

		if (child.name() == std::string("Setting")) {
			if (nameOptionMap.find(child.attribute("name").value()) == nameOptionMap.cend()) {
				settings.remove_child(child);
				ret = true;
			}
		}
		else {
			settings.remove_child(child);
			ret = true;
		}
		child = next;
	}

	return ret;
}

void COptions::SaveIfNeeded()
{
	bool save = m_save_timer.IsRunning();

	if (needsCleanup_) {
		save |= Cleanup();
	}

	m_save_timer.Stop();
	if (save) {
		Save();
	}
}

namespace {
#ifndef FZ_WINDOWS
std::wstring TryDirectory(wxString path, wxString const& suffix, bool check_exists)
{
	if (!path.empty() && path[0] == '/') {
		if (path[path.size() - 1] != '/') {
			path += '/';
		}

		path += suffix;

		if (check_exists) {
			if (!wxFileName::DirExists(path)) {
				path.clear();
			}
		}
	}
	else {
		path.clear();
	}
	return path.ToStdWstring();
}

#endif
}

CLocalPath COptions::GetUnadjustedSettingsDir()
{
	CLocalPath ret;

#ifdef FZ_WINDOWS
	wchar_t buffer[MAX_PATH * 2 + 1];

	if (SUCCEEDED(SHGetFolderPath(0, CSIDL_APPDATA, 0, SHGFP_TYPE_CURRENT, buffer))) {
		CLocalPath tmp(buffer);
		if (!tmp.empty()) {
			tmp.AddSegment(L"FileZilla");
		}
		ret = tmp;
	}

	if (ret.empty()) {
		// Fall back to directory where the executable is
		DWORD c = GetModuleFileName(0, buffer, MAX_PATH * 2);
		if (c && c < MAX_PATH * 2) {
			std::wstring tmp;
			ret.SetPath(buffer, &tmp);
		}
	}
#else
	std::wstring cfg = TryDirectory(GetEnv("XDG_CONFIG_HOME"), L"filezilla/", true);
	if (cfg.empty()) {
		cfg = TryDirectory(wxGetHomeDir(), L".config/filezilla/", true);
	}
	if (cfg.empty()) {
		cfg = TryDirectory(wxGetHomeDir(), L".filezilla/", true);
	}
	if (cfg.empty()) {
		cfg = TryDirectory(GetEnv("XDG_CONFIG_HOME"), L"filezilla/", false);
	}
	if (cfg.empty()) {
		cfg = TryDirectory(wxGetHomeDir(), L".config/filezilla/", false);
	}
	if (cfg.empty()) {
		cfg = TryDirectory(wxGetHomeDir(), L".filezilla/", false);
	}
	ret.SetPath(cfg);
#endif
	return ret;
}

CLocalPath COptions::GetCacheDirectory()
{
	CLocalPath ret;

	std::wstring dir(GetOption(OPTION_DEFAULT_CACHE_DIR));
	if (!dir.empty()) {
		dir = ExpandPath(dir);
		ret.SetPath(wxGetApp().GetDefaultsDir().GetPath());
		if (!ret.ChangePath(dir)) {
			ret.clear();
		}
	}
	else {
#ifdef FZ_WINDOWS
		wchar_t buffer[MAX_PATH * 2 + 1];

		if (SUCCEEDED(SHGetFolderPath(0, CSIDL_LOCAL_APPDATA, 0, SHGFP_TYPE_CURRENT, buffer))) {
			ret.SetPath(buffer);
			if (!ret.empty()) {
				ret.AddSegment(L"FileZilla");
			}
		}
#else
		std::wstring cfg = TryDirectory(GetEnv("XDG_CACHE_HOME"), L"filezilla/", false);
		if (cfg.empty()) {
			cfg = TryDirectory(wxGetHomeDir(), L".cache/filezilla/", false);
		}
		ret.SetPath(cfg);
#endif
	}

	return ret;
}

CLocalPath COptions::InitSettingsDir()
{
	CLocalPath p;

	std::wstring dir = GetOption(OPTION_DEFAULT_SETTINGSDIR);
	if (!dir.empty()) {
		dir = ExpandPath(dir);
		p.SetPath(wxGetApp().GetDefaultsDir().GetPath());
		p.ChangePath(dir);
	}
	else {
		p = GetUnadjustedSettingsDir();
	}

	if (!p.empty() && !p.Exists()) {
		wxFileName::Mkdir(p.GetPath(), 0700, wxPATH_MKDIR_FULL);
	}

	SetOption(OPTION_DEFAULT_SETTINGSDIR, p.GetPath());

	return p;
}

void COptions::SetDefaultValues()
{
	fz::scoped_lock l(m_sync_);
	for (int i = 0; i < OPTIONS_NUM; ++i) {
		m_optionsCache[i].from_default = false;
		if (options[i].type == xml) {
			m_optionsCache[i].xmlValue = std::make_unique<pugi::xml_document>();
			m_optionsCache[i].xmlValue->load_string(fz::to_string(options[i].defaultValue).c_str());
		}
		else {
			m_optionsCache[i] = options[i].defaultValue;
		}
	}
}


void COptions::RequireCleanup()
{
	needsCleanup_ = true;
}
