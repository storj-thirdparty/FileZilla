#include <filezilla.h>
#include "Mainfrm.h"

#include "aboutdialog.h"
#include "asyncrequestqueue.h"
#include "auto_ascii_files.h"
#include "bookmarks_dialog.h"
#include "buildinfo.h"
#include "clearprivatedata.h"
#include "cmdline.h"
#include "commandqueue.h"
#include "conditionaldialog.h"
#include "context_control.h"
#include "defaultfileexistsdlg.h"
#include "edithandler.h"
#include "encoding_converter.h"
#include "export.h"
#include "filelist_statusbar.h"
#include "filezillaapp.h"
#include "filter.h"
#include "import.h"
#include "inputdialog.h"
#include "led.h"
#include "list_search_panel.h"
#include "local_recursive_operation.h"
#include "LocalListView.h"
#include "LocalTreeView.h"
#include "loginmanager.h"
#include "manual_transfer.h"
#include "menu_bar.h"
#include "netconfwizard.h"
#include "Options.h"
#include "power_management.h"
#include "queue.h"
#include "quickconnectbar.h"
#include "remote_recursive_operation.h"
#include "RemoteListView.h"
#include "RemoteTreeView.h"
#include "search.h"
#include "settings/settingsdialog.h"
#include "sitemanager_dialog.h"
#include "speedlimits_dialog.h"
#include "splitter.h"
#include "StatusView.h"
#include "state.h"
#include "themeprovider.h"
#include "toolbar.h"
#include "update_dialog.h"
#include "updater.h"
#include "view.h"
#include "viewheader.h"
#include "welcome_dialog.h"
#include "window_state_manager.h"

#ifdef __WXMSW__
#include <wx/module.h>
#endif
#ifndef __WXMAC__
#include <wx/taskbar.h>
#else
#include <wx/combobox.h>
#endif

#include <functional>
#include <limits>
#include <map>

#ifdef __WXGTK__
DECLARE_EVENT_TYPE(fzEVT_TASKBAR_CLICK_DELAYED, -1)
DEFINE_EVENT_TYPE(fzEVT_TASKBAR_CLICK_DELAYED)
#endif

static int tab_hotkey_ids[10];

#if FZ_MANUALUPDATECHECK
static int GetAvailableUpdateMenuId()
{
	static int updateAvailableMenuId = wxNewId();
	return updateAvailableMenuId;
}
#endif

std::map<int, std::pair<std::function<void(wxTextEntry*)>, wxChar>> keyboardCommands;

#ifdef __WXMAC__
wxTextEntry* GetSpecialTextEntry(wxWindow* w, wxChar cmd)
{
	if (cmd == 'A' || cmd == 'V') {
		wxTextCtrl* text = dynamic_cast<wxTextCtrl*>(w);
		if (text && text->GetWindowStyle() & wxTE_PASSWORD) {
			return text;
		}
	}
	return dynamic_cast<wxComboBox*>(w);
}
#else
wxTextEntry* GetSpecialTextEntry(wxWindow*, wxChar)
{
	return 0;
}
#endif

bool HandleKeyboardCommand(wxCommandEvent& event, wxWindow& parent)
{
	auto const& it = keyboardCommands.find(event.GetId());
	if (it == keyboardCommands.end()) {
		return false;
	}

	wxTextEntry* e = GetSpecialTextEntry(parent.FindFocus(), it->second.second);
	if (e) {
		it->second.first(e);
	}
	else {
		event.Skip();
	}
	return true;
}

BEGIN_EVENT_TABLE(CMainFrame, wxNavigationEnabled<wxFrame>)
	EVT_SIZE(CMainFrame::OnSize)
	EVT_MENU(wxID_ANY, CMainFrame::OnMenuHandler)
	EVT_COMMAND(wxID_ANY, fzEVT_UPDATE_LED_TOOLTIP, CMainFrame::OnUpdateLedTooltip)
	EVT_TOOL(XRCID("ID_TOOLBAR_DISCONNECT"), CMainFrame::OnDisconnect)
	EVT_MENU(XRCID("ID_MENU_SERVER_DISCONNECT"), CMainFrame::OnDisconnect)
	EVT_TOOL(XRCID("ID_TOOLBAR_CANCEL"), CMainFrame::OnCancel)
	EVT_MENU(XRCID("ID_CANCEL"), CMainFrame::OnCancel)
	EVT_TOOL(XRCID("ID_TOOLBAR_RECONNECT"), CMainFrame::OnReconnect)
	EVT_TOOL(XRCID("ID_MENU_SERVER_RECONNECT"), CMainFrame::OnReconnect)
	EVT_TOOL(XRCID("ID_TOOLBAR_REFRESH"), CMainFrame::OnRefresh)
	EVT_MENU(XRCID("ID_REFRESH"), CMainFrame::OnRefresh)
	EVT_TOOL(XRCID("ID_TOOLBAR_SITEMANAGER"), CMainFrame::OnSiteManager)
	EVT_CLOSE(CMainFrame::OnClose)
#ifdef WITH_LIBDBUS
	EVT_END_SESSION(CMainFrame::OnClose)
#endif
	EVT_TIMER(wxID_ANY, CMainFrame::OnTimer)
	EVT_TOOL(XRCID("ID_TOOLBAR_PROCESSQUEUE"), CMainFrame::OnProcessQueue)
	EVT_TOOL(XRCID("ID_TOOLBAR_LOGVIEW"), CMainFrame::OnToggleLogView)
	EVT_TOOL(XRCID("ID_TOOLBAR_LOCALTREEVIEW"), CMainFrame::OnToggleDirectoryTreeView)
	EVT_TOOL(XRCID("ID_TOOLBAR_REMOTETREEVIEW"), CMainFrame::OnToggleDirectoryTreeView)
	EVT_TOOL(XRCID("ID_TOOLBAR_QUEUEVIEW"), CMainFrame::OnToggleQueueView)
	EVT_MENU(XRCID("ID_VIEW_TOOLBAR"), CMainFrame::OnToggleToolBar)
	EVT_MENU(XRCID("ID_VIEW_MESSAGELOG"), CMainFrame::OnToggleLogView)
	EVT_MENU(XRCID("ID_VIEW_LOCALTREE"), CMainFrame::OnToggleDirectoryTreeView)
	EVT_MENU(XRCID("ID_VIEW_REMOTETREE"), CMainFrame::OnToggleDirectoryTreeView)
	EVT_MENU(XRCID("ID_VIEW_QUEUE"), CMainFrame::OnToggleQueueView)
	EVT_MENU(wxID_ABOUT, CMainFrame::OnMenuHelpAbout)
	EVT_TOOL(XRCID("ID_TOOLBAR_FILTER"), CMainFrame::OnFilter)
	EVT_TOOL_RCLICKED(XRCID("ID_TOOLBAR_FILTER"), CMainFrame::OnFilterRightclicked)
#if FZ_MANUALUPDATECHECK
	EVT_MENU(XRCID("ID_CHECKFORUPDATES"), CMainFrame::OnCheckForUpdates)
	EVT_MENU(GetAvailableUpdateMenuId(), CMainFrame::OnCheckForUpdates)
#endif //FZ_MANUALUPDATECHECK
	EVT_TOOL_RCLICKED(XRCID("ID_TOOLBAR_SITEMANAGER"), CMainFrame::OnSitemanagerDropdown)
#ifdef EVT_TOOL_DROPDOWN
	EVT_TOOL_DROPDOWN(XRCID("ID_TOOLBAR_SITEMANAGER"), CMainFrame::OnSitemanagerDropdown)
#endif
	EVT_NAVIGATION_KEY(CMainFrame::OnNavigationKeyEvent)
	EVT_CHAR_HOOK(CMainFrame::OnChar)
	EVT_MENU(XRCID("ID_MENU_VIEW_FILTERS"), CMainFrame::OnFilter)
	EVT_ACTIVATE(CMainFrame::OnActivate)
	EVT_TOOL(XRCID("ID_TOOLBAR_COMPARISON"), CMainFrame::OnToolbarComparison)
	EVT_TOOL_RCLICKED(XRCID("ID_TOOLBAR_COMPARISON"), CMainFrame::OnToolbarComparisonDropdown)
#ifdef EVT_TOOL_DROPDOWN
	EVT_TOOL_DROPDOWN(XRCID("ID_TOOLBAR_COMPARISON"), CMainFrame::OnToolbarComparisonDropdown)
#endif
	EVT_MENU(XRCID("ID_COMPARE_SIZE"), CMainFrame::OnDropdownComparisonMode)
	EVT_MENU(XRCID("ID_COMPARE_DATE"), CMainFrame::OnDropdownComparisonMode)
	EVT_MENU(XRCID("ID_COMPARE_HIDEIDENTICAL"), CMainFrame::OnDropdownComparisonHide)
	EVT_TOOL(XRCID("ID_TOOLBAR_SYNCHRONIZED_BROWSING"), CMainFrame::OnSyncBrowse)
#ifdef __WXMAC__
	EVT_CHILD_FOCUS(CMainFrame::OnChildFocused)
#else
	EVT_ICONIZE(CMainFrame::OnIconize)
#endif
#ifdef __WXGTK__
	EVT_COMMAND(wxID_ANY, fzEVT_TASKBAR_CLICK_DELAYED, CMainFrame::OnTaskBarClick_Delayed)
#endif
	EVT_TOOL(XRCID("ID_TOOLBAR_FIND"), CMainFrame::OnSearch)
	EVT_MENU(XRCID("ID_MENU_SERVER_SEARCH"), CMainFrame::OnSearch)
	EVT_MENU(XRCID("ID_MENU_FILE_NEWTAB"), CMainFrame::OnMenuNewTab)
	EVT_MENU(XRCID("ID_MENU_FILE_CLOSETAB"), CMainFrame::OnMenuCloseTab)
END_EVENT_TABLE()

class CMainFrameStateEventHandler final : public CGlobalStateEventHandler
{
public:
	CMainFrameStateEventHandler(CMainFrame* pMainFrame)
	{
		m_pMainFrame = pMainFrame;

		CContextManager::Get()->RegisterHandler(this, STATECHANGE_REMOTE_IDLE, false);
		CContextManager::Get()->RegisterHandler(this, STATECHANGE_SERVER, false);

		CContextManager::Get()->RegisterHandler(this, STATECHANGE_CHANGEDCONTEXT, false);
	}

protected:
	virtual void OnStateChange(CState* pState, t_statechange_notifications notification, std::wstring const&, const void*) override
	{
		if (notification == STATECHANGE_CHANGEDCONTEXT) {
			// Update window title
			if (!pState || !pState->GetSite()) {
				m_pMainFrame->SetTitle(_T("FileZilla"));
			}
			else {
				m_pMainFrame->SetTitle(pState->GetTitle() + _T(" - FileZilla"));
			}

			return;
		}

		if (!pState) {
			return;
		}

		if (!m_pMainFrame->m_pContextControl) {
			return;
		}

		CContextControl::_context_controls* controls = m_pMainFrame->m_pContextControl->GetControlsFromState(pState);
		if (!controls) {
			return;
		}

		if (!controls->used()) {
			if (notification == STATECHANGE_REMOTE_IDLE || notification == STATECHANGE_SERVER) {
				pState->Disconnect();
			}

			return;
		}

		if (notification == STATECHANGE_SERVER) {
			if (pState == CContextManager::Get()->GetCurrentContext()) {
				Site const& site = pState->GetSite();
				if (!site) {
					m_pMainFrame->SetTitle(_T("FileZilla"));
				}
				else {
					m_pMainFrame->SetTitle(pState->GetTitle() + _T(" - FileZilla"));
				}
			}

			return;
		}
	}

	CMainFrame* m_pMainFrame;
};


/*#include "overlay.h"
#include <wx/hyperlink.h>

namespace {
void ShowStorjOverlay(wxWindow* parent, wxWindow* anchor, wxPoint const& offset)
{
	auto p = new OverlayWindow(parent);

	auto box = new wxBoxSizer(wxVERTICAL);
	auto sizer = layout::createFlex(1);
	box->Add(sizer, 0, wxALL, 7);

	p->SetSizer(box);

	auto title = new wxStaticText(p, -1, _("Did you know about this new FileZilla feature?"));

	wxFont f = title->GetFont();
	f.SetWeight(wxBOLD);
	title->SetFont(f);
	sizer->Add(title);

	sizer->AddSpacer(0);

	sizer->Add(new wxStaticText(p, -1, _("You can use FileZilla to store your files securely in the Storj decentralized cloud.")));
	sizer->Add(new wxHyperlinkCtrl(p, -1, _("Click to learn more"), L"https://wiki.filezilla-project.org/Storj"));

	sizer->AddSpacer(10);

	//sizer->Add(new wxCheckBox(p, -1, L"Don't show this again"), 0, wxALIGN_RIGHT);

	auto dismiss = new wxButton(p, -1, L"Dismiss");
	dismiss->Bind(wxEVT_BUTTON, [p](wxCommandEvent&) { p->Destroy(); });
	sizer->Add(dismiss, 0, wxALIGN_RIGHT);

	wxGetApp().GetWrapEngine()->WrapRecursive(p, 3);

	box->Fit(p);

	p->SetAnchor(anchor, offset);
}
}*/

CMainFrame::CMainFrame()
	: m_engineContext(*COptions::Get(), CustomEncodingConverter::Get())
	, m_comparisonToggleAcceleratorId(wxNewId())
{
	m_pActivityLed[0] = m_pActivityLed[1] = 0;

	wxGetApp().AddStartupProfileRecord("CMainFrame::CMainFrame");
	wxRect screen_size = CWindowStateManager::GetScreenDimensions();

	wxSize initial_size;
	initial_size.x = wxMin(1200, screen_size.GetWidth() - 10);
	initial_size.y = wxMin(950, screen_size.GetHeight() - 50);

	Create(NULL, -1, _T("FileZilla"), wxDefaultPosition, initial_size);
	SetSizeHints(700, 500);

#ifdef __WXMSW__
	// In order for the --close commandline argument to work,
	// there has to be a way to find other instances.
	// Create a hidden window with a title no other program uses
	wxWindow* pChild = new wxWindow();
	pChild->Hide();
	pChild->Create(this, wxID_ANY);
	::SetWindowText((HWND)pChild->GetHandle(), _T("FileZilla process identificator 3919DB0A-082D-4560-8E2F-381A35969FB4"));
#endif

#ifdef __WXMSW__
	SetIcon(wxICON(appicon));
#else
	SetIcons(CThemeProvider::GetIconBundle(_T("ART_FILEZILLA")));
#endif

	CPowerManagement::Create(this);

	// It's important that the context control gets created before our own state handler
	// so that contextchange events can be processed in the right order.
	m_pContextControl = new CContextControl(*this);

	m_pStatusBar = new CStatusBar(this);
	if (m_pStatusBar) {
		m_pActivityLed[0] = new CLed(m_pStatusBar, 0);
		m_pActivityLed[1] = new CLed(m_pStatusBar, 1);

		m_pStatusBar->AddField(-1, widget_led_recv, m_pActivityLed[1]);
		m_pStatusBar->AddField(-1, widget_led_send, m_pActivityLed[0]);

		SetStatusBar(m_pStatusBar);
	}

	m_closeEventTimer.SetOwner(this);

	if (CFilterManager::HasActiveFilters(true)) {
		if (COptions::Get()->GetOptionVal(OPTION_FILTERTOGGLESTATE)) {
			CFilterManager::ToggleFilters();
		}
	}

	CreateMenus();
	CreateMainToolBar();
	if (COptions::Get()->GetOptionVal(OPTION_SHOW_QUICKCONNECT)) {
		CreateQuickconnectBar();
	}

	m_pAsyncRequestQueue = new CAsyncRequestQueue(this);

#ifdef __WXMSW__
	long style = wxSP_NOBORDER | wxSP_LIVE_UPDATE;
#elif !defined(__WXMAC__)
	long style = wxSP_3DBORDER | wxSP_LIVE_UPDATE;
#else
	long style = wxSP_LIVE_UPDATE;
#endif

	wxSize clientSize = GetClientSize();

	m_pTopSplitter = new CSplitterWindowEx(this, -1, wxDefaultPosition, clientSize, style);
	m_pTopSplitter->SetMinimumPaneSize(50);

	m_pBottomSplitter = new CSplitterWindowEx(m_pTopSplitter, -1, wxDefaultPosition, wxDefaultSize, wxSP_NOBORDER | wxSP_LIVE_UPDATE);
	m_pBottomSplitter->SetMinimumPaneSize(20, 70);
	m_pBottomSplitter->SetSashGravity(1.0);

	const int message_log_position = COptions::Get()->GetOptionVal(OPTION_MESSAGELOG_POSITION);
	m_pQueueLogSplitter = new CSplitterWindowEx(m_pBottomSplitter, -1, wxDefaultPosition, wxDefaultSize, wxSP_NOBORDER | wxSP_LIVE_UPDATE);
	m_pQueueLogSplitter->SetMinimumPaneSize(50, 250);
	m_pQueueLogSplitter->SetSashGravity(0.5);
	m_pQueuePane = new CQueue(m_pQueueLogSplitter, this, m_pAsyncRequestQueue);

	if (message_log_position == 1) {
		m_pStatusView = new CStatusView(m_pQueueLogSplitter, -1);
	}
	else {
		m_pStatusView = new CStatusView(m_pTopSplitter, -1);
	}

	m_pQueueView = m_pQueuePane->GetQueueView();

	m_pContextControl->Create(m_pBottomSplitter);

	m_pStateEventHandler = new CMainFrameStateEventHandler(this);

	m_pContextControl->RestoreTabs();

	switch (message_log_position) {
	case 1:
		m_pTopSplitter->Initialize(m_pBottomSplitter);
		if (COptions::Get()->GetOptionVal(OPTION_SHOW_MESSAGELOG)) {
			if (COptions::Get()->GetOptionVal(OPTION_SHOW_QUEUE)) {
				m_pQueueLogSplitter->SplitVertically(m_pQueuePane, m_pStatusView);
			}
			else {
				m_pQueueLogSplitter->Initialize(m_pStatusView);
				m_pQueuePane->Hide();
			}
		}
		else {
			if (COptions::Get()->GetOptionVal(OPTION_SHOW_QUEUE)) {
				m_pStatusView->Hide();
				m_pQueueLogSplitter->Initialize(m_pQueuePane);
			}
			else {
				m_pQueuePane->Hide();
				m_pStatusView->Hide();
				m_pQueueLogSplitter->Hide();
			}
		}
		break;
	case 2:
		m_pTopSplitter->Initialize(m_pBottomSplitter);
		if (COptions::Get()->GetOptionVal(OPTION_SHOW_QUEUE)) {
			m_pQueueLogSplitter->Initialize(m_pQueuePane);
		}
		else {
			m_pQueueLogSplitter->Hide();
			m_pQueuePane->Hide();
		}
		m_pQueuePane->AddPage(m_pStatusView, _("Message log"));
		break;
	default:
		if (COptions::Get()->GetOptionVal(OPTION_SHOW_QUEUE)) {
			m_pQueueLogSplitter->Initialize(m_pQueuePane);
		}
		else {
			m_pQueuePane->Hide();
			m_pQueueLogSplitter->Hide();
		}
		if (COptions::Get()->GetOptionVal(OPTION_SHOW_MESSAGELOG)) {
			m_pTopSplitter->SplitHorizontally(m_pStatusView, m_pBottomSplitter);
		}
		else {
			m_pStatusView->Hide();
			m_pTopSplitter->Initialize(m_pBottomSplitter);
		}
		break;
	}

	if (m_pQueueLogSplitter->IsShown()) {
		m_pBottomSplitter->SplitHorizontally(m_pContextControl, m_pQueueLogSplitter);
	}
	else {
		m_pQueueLogSplitter->Hide();
		m_pBottomSplitter->Initialize(m_pContextControl);
	}

	wxGetApp().AddStartupProfileRecord("CMainFrame::CMainFrame pre layout");
	m_pWindowStateManager = new CWindowStateManager(this);
	m_pWindowStateManager->Restore(OPTION_MAINWINDOW_POSITION);

	Layout();
	HandleResize();

	if (!RestoreSplitterPositions()) {
		SetDefaultSplitterPositions();
	}

	SetupKeyboardAccelerators();

	ConnectNavigationHandler(m_pStatusView);
	ConnectNavigationHandler(m_pQueuePane);

	CEditHandler::Create()->SetQueue(m_pQueueView);

	CAutoAsciiFiles::SettingsChanged();

	FixTabOrder();

	RegisterOption(OPTION_ICONS_THEME);
	RegisterOption(OPTION_ICONS_SCALE);
	RegisterOption(OPTION_MESSAGELOG_POSITION);
	RegisterOption(OPTION_FILEPANE_LAYOUT);
	RegisterOption(OPTION_FILEPANE_SWAP);
}

CMainFrame::~CMainFrame()
{
	UnregisterAllOptions();

	CPowerManagement::Destroy();

	delete m_pStateEventHandler;

	delete m_pContextControl;
	m_pContextControl = 0;

	CContextManager::Get()->DestroyAllStates();
	delete m_pAsyncRequestQueue;
#if FZ_MANUALUPDATECHECK
	delete m_pUpdater;
#endif

	CEditHandler* pEditHandler = CEditHandler::Get();
	if (pEditHandler) {
		// This might leave temporary files behind,
		// edit handler should clean them on next startup
		pEditHandler->Release();
	}

#ifndef __WXMAC__
	delete m_taskBarIcon;
#endif
}

void CMainFrame::HandleResize()
{
	wxSize clientSize = GetClientSize();
	if (clientSize.y <= 0) { // Can happen if restoring from tray on XP if using ugly XP themes
		return;
	}

	if (m_pQuickconnectBar) {
		m_pQuickconnectBar->SetSize(0, 0, clientSize.GetWidth(), -1, wxSIZE_USE_EXISTING);
	}
	if (m_pTopSplitter) {
		if (!m_pQuickconnectBar) {
			m_pTopSplitter->SetSize(0, 0, clientSize.GetWidth(), clientSize.GetHeight());
		}
		else {
			wxSize panelSize = m_pQuickconnectBar->GetSize();
			m_pTopSplitter->SetSize(0, panelSize.GetHeight(), clientSize.GetWidth(), clientSize.GetHeight() - panelSize.GetHeight());
		}
	}
}

void CMainFrame::OnSize(wxSizeEvent &event)
{
	wxFrame::OnSize(event);

	if (!m_pBottomSplitter) {
		return;
	}

	HandleResize();

#ifdef __WXGTK__
	if (m_pWindowStateManager && m_pWindowStateManager->m_maximize_requested && IsMaximized()) {
		m_pWindowStateManager->m_maximize_requested = 0;
		if (!RestoreSplitterPositions()) {
			SetDefaultSplitterPositions();
		}
	}
#endif
}

bool CMainFrame::CreateMenus()
{
	wxGetApp().AddStartupProfileRecord("CMainFrame::CreateMenus");
	CMenuBar* old = m_pMenuBar;

	m_pMenuBar = CMenuBar::Load(this);

	if (!m_pMenuBar) {
		m_pMenuBar = old;
		return false;
	}

	SetMenuBar(m_pMenuBar);
	delete old;

	return true;
}

bool CMainFrame::CreateQuickconnectBar()
{
	wxGetApp().AddStartupProfileRecord("CMainFrame::CreateQuickconnectBar");
	delete m_pQuickconnectBar;

	m_pQuickconnectBar = new CQuickconnectBar();
	if (!m_pQuickconnectBar->Create(this)) {
		delete m_pQuickconnectBar;
		m_pQuickconnectBar = 0;
	}
	else {
		wxSize clientSize = GetClientSize();
		if (m_pTopSplitter) {
			wxSize panelSize = m_pQuickconnectBar->GetSize();
			m_pTopSplitter->SetSize(-1, panelSize.GetHeight(), -1, clientSize.GetHeight() - panelSize.GetHeight(), wxSIZE_USE_EXISTING);
		}
		m_pQuickconnectBar->SetSize(0, 0, clientSize.GetWidth(), -1);
	}

	return true;
}

void CMainFrame::OnMenuHandler(wxCommandEvent &event)
{
	if (event.GetId() == XRCID("wxID_EXIT")) {
		Close();
	}
	else if (event.GetId() == XRCID("ID_MENU_FILE_SITEMANAGER")) {
		OpenSiteManager();
	}
	else if (event.GetId() == XRCID("ID_MENU_FILE_COPYSITEMANAGER")) {
		Site site;
		CState* pState = CContextManager::Get()->GetCurrentContext();
		if (pState) {
			site = pState->GetSite();
		}
		if (!site) {
			wxMessageBoxEx(_("Not connected to any server."), _("Cannot add server to Site Manager"), wxICON_EXCLAMATION);
			return;
		}
		OpenSiteManager(&site);
	}
	else if (event.GetId() == XRCID("ID_MENU_SERVER_CMD")) {
		CState* pState = CContextManager::Get()->GetCurrentContext();
		if (!pState || !pState->m_pCommandQueue || !pState->IsRemoteConnected() || !pState->IsRemoteIdle()) {
			return;
		}

		CInputDialog dlg;
		dlg.Create(this, _("Enter custom command"), _("Please enter raw FTP command.\nUsing raw ftp commands will clear the directory cache."));
		if (dlg.ShowModal() != wxID_OK) {
			return;
		}

		pState = CContextManager::Get()->GetCurrentContext();
		if (!pState || !pState->m_pCommandQueue || !pState->IsRemoteConnected() || !pState->IsRemoteIdle()) {
			wxBell();
			return;
		}

		const wxString &command = dlg.GetValue();

		if (!command.Left(5).CmpNoCase(_T("quote")) || !command.Left(6).CmpNoCase(_T("quote "))) {
			CConditionalDialog condDlg(this, CConditionalDialog::rawcommand_quote, CConditionalDialog::yesno);
			condDlg.SetTitle(_("Raw FTP command"));

			condDlg.AddText(_("'quote' is usually a local command used by commandline clients to send the arguments following 'quote' to the server. You might want to enter the raw command without the leading 'quote'."));
			condDlg.AddText(wxString::Format(_("Do you really want to send '%s' to the server?"), command));

			if (!condDlg.Run()) {
				return;
			}
		}

		pState = CContextManager::Get()->GetCurrentContext();
		if (!pState || !pState->m_pCommandQueue || !pState->IsRemoteConnected() || !pState->IsRemoteIdle()) {
			wxBell();
			return;
		}
		pState->m_pCommandQueue->ProcessCommand(new CRawCommand(dlg.GetValue().ToStdWstring()));
	}
	else if (event.GetId() == XRCID("wxID_PREFERENCES")) {
		OnMenuEditSettings(event);
	}
	else if (event.GetId() == XRCID("ID_MENU_EDIT_NETCONFWIZARD")) {
		CNetConfWizard wizard(this, COptions::Get(), m_engineContext);
		wizard.Load();
		wizard.Run();
	}
	// Debug menu
	else if (event.GetId() == XRCID("ID_CIPHERS")) {
		CInputDialog dlg;
		dlg.Create(this, _T("Ciphers"), _T("Priority string:"));
		dlg.AllowEmpty(true);
		if (dlg.ShowModal() == wxID_OK) {
			std::string ciphers = ListTlsCiphers(fz::to_string(dlg.GetValue().ToStdWstring()));
			wxMessageBoxEx(fz::to_wstring(ciphers), _T("Ciphers"));
		}
	}
	else if (event.GetId() == XRCID("ID_CLEARCACHE_LAYOUT")) {
		CWrapEngine::ClearCache();
	}
	else if (event.GetId() == XRCID("ID_CLEAR_UPDATER")) {
#if FZ_MANUALUPDATECHECK
		if (m_pUpdater) {
			COptions::Get()->SetOption(OPTION_UPDATECHECK_LASTDATE, std::wstring());
			COptions::Get()->SetOption(OPTION_UPDATECHECK_NEWVERSION, std::wstring());
			m_pUpdater->Init();
		}
#endif
	}
	else if (event.GetId() == XRCID("ID_MENU_TRANSFER_FILEEXISTS")) {
		CDefaultFileExistsDlg dlg;
		if (!dlg.Load(this, false)) {
			return;
		}

		dlg.Run();
	}
	else if (event.GetId() == XRCID("ID_MENU_EDIT_CLEARPRIVATEDATA")) {
		CClearPrivateDataDialog* pDlg = CClearPrivateDataDialog::Create(this);
		if (!pDlg) {
			return;
		}

		pDlg->Run();
		pDlg->Delete();

		if (m_pMenuBar) {
			m_pMenuBar->UpdateMenubarState();
		}
		if (m_pToolBar) {
			m_pToolBar->UpdateToolbarState();
		}
	}
	else if (event.GetId() == XRCID("ID_MENU_SERVER_VIEWHIDDEN")) {
		bool showHidden = COptions::Get()->GetOptionVal(OPTION_VIEW_HIDDEN_FILES) ? 0 : 1;
		if (showHidden) {
			CConditionalDialog dlg(this, CConditionalDialog::viewhidden, CConditionalDialog::ok, false);
			dlg.SetTitle(_("Force showing hidden files"));

			dlg.AddText(_("Note that this feature is only supported using the FTP protocol."));
			dlg.AddText(_("A proper server always shows all files, but some broken servers hide files from the user. Use this option to force the server to show all files."));
			dlg.AddText(_("Keep in mind that not all servers support this feature and may return incorrect listings if this option is enabled. Although FileZilla performs some tests to check if the server supports this feature, the test may fail."));
			dlg.AddText(_("Disable this option again if you will not be able to see the correct directory contents anymore."));
			(void)dlg.Run();
		}

		COptions::Get()->SetOption(OPTION_VIEW_HIDDEN_FILES, showHidden ? 1 : 0);
		const std::vector<CState*> *pStates = CContextManager::Get()->GetAllStates();
		for (auto & pState : *pStates) {
			CServerPath path = pState->GetRemotePath();
			if (!path.empty() && pState->m_pCommandQueue) {
				pState->ChangeRemoteDir(path, std::wstring(), LIST_FLAG_REFRESH);
			}
		}
	}
	else if (event.GetId() == XRCID("ID_EXPORT")) {
		CExportDialog dlg(this, m_pQueueView);
		dlg.Run();
	}
	else if (event.GetId() == XRCID("ID_IMPORT")) {
		CImportDialog dlg(this, m_pQueueView);
		dlg.Run();
	}
	else if (event.GetId() == XRCID("ID_MENU_FILE_EDITED")) {
		CEditHandlerStatusDialog dlg(this);
		dlg.ShowModal();
	}
	else if (event.GetId() == XRCID("ID_MENU_TRANSFER_TYPE_AUTO")) {
		COptions::Get()->SetOption(OPTION_ASCIIBINARY, 0);
	}
	else if (event.GetId() == XRCID("ID_MENU_TRANSFER_TYPE_ASCII")) {
		COptions::Get()->SetOption(OPTION_ASCIIBINARY, 1);
	}
	else if (event.GetId() == XRCID("ID_MENU_TRANSFER_TYPE_BINARY")) {
		COptions::Get()->SetOption(OPTION_ASCIIBINARY, 2);
	}
	else if (event.GetId() == XRCID("ID_MENU_TRANSFER_PRESERVETIMES")) {
		if (event.IsChecked()) {
			CConditionalDialog dlg(this, CConditionalDialog::confirm_preserve_timestamps, CConditionalDialog::ok, true);
			dlg.SetTitle(_("Preserving file timestamps"));
			dlg.AddText(_("Please note that preserving timestamps on uploads on FTP, FTPS and FTPES servers only works if they support the MFMT command."));
			dlg.Run();
		}
		COptions::Get()->SetOption(OPTION_PRESERVE_TIMESTAMPS, event.IsChecked() ? 1 : 0);
	}
	else if (event.GetId() == XRCID("ID_MENU_TRANSFER_PROCESSQUEUE")) {
		if (m_pQueueView) {
			m_pQueueView->SetActive(event.IsChecked());
		}
	}
	else if (event.GetId() == XRCID("ID_MENU_HELP_GETTINGHELP") ||
			 event.GetId() == XRCID("ID_MENU_HELP_BUGREPORT"))
	{
		wxString url(_T("https://filezilla-project.org/support.php?type=client&mode="));
		if (event.GetId() == XRCID("ID_MENU_HELP_GETTINGHELP")) {
			url += _T("help");
		}
		else {
			url += _T("bugreport");
		}
		wxString version = CBuildInfo::GetVersion();
		if (version != _T("custom build")) {
			url += _T("&version=");
			// We need to urlencode version number

			// Unbelievable, but wxWidgets does not have any method
			// to urlencode strings.
			// Do a crude approach: Drop everything unexpected...
			for (unsigned int i = 0; i < version.Len(); i++) {
				wxChar c = version.GetChar(i);
				if ((c >= '0' && c <= '9') ||
					(c >= 'a' && c <= 'z') ||
					(c >= 'A' && c <= 'Z') ||
					c == '-' || c == '.' ||
					c == '_')
				{
					url.Append(c);
				}
			}
		}
		wxLaunchDefaultBrowser(url);
	}
	else if (event.GetId() == XRCID("ID_MENU_VIEW_FILELISTSTATUSBAR")) {
		bool show = COptions::Get()->GetOptionVal(OPTION_FILELIST_STATUSBAR) == 0;
		COptions::Get()->SetOption(OPTION_FILELIST_STATUSBAR, show ? 1 : 0);
		CContextControl::_context_controls* controls = m_pContextControl ? m_pContextControl->GetCurrentControls() : 0;
		if (controls && controls->pLocalListViewPanel) {
			wxStatusBar* pStatusBar = controls->pLocalListViewPanel->GetStatusBar();
			if (pStatusBar) {
				pStatusBar->Show(show);
				wxSizeEvent evt;
				controls->pLocalListViewPanel->ProcessWindowEvent(evt);
			}
		}
		if (controls && controls->pRemoteListViewPanel) {
			wxStatusBar* pStatusBar = controls->pRemoteListViewPanel->GetStatusBar();
			if (pStatusBar) {
				pStatusBar->Show(show);
				wxSizeEvent evt;
				controls->pRemoteListViewPanel->ProcessWindowEvent(evt);
			}
		}
	}
	else if (event.GetId() == XRCID("ID_VIEW_QUICKCONNECT")) {
		if (!m_pQuickconnectBar) {
			CreateQuickconnectBar();
		}
		else {
			m_pQuickconnectBar->Destroy();
			m_pQuickconnectBar = 0;
			wxSize clientSize = GetClientSize();
			m_pTopSplitter->SetSize(0, 0, clientSize.GetWidth(), clientSize.GetHeight());
		}
		COptions::Get()->SetOption(OPTION_SHOW_QUICKCONNECT, m_pQuickconnectBar != 0);
	}
	else if (event.GetId() == XRCID("ID_MENU_TRANSFER_MANUAL")) {
		CState* pState = CContextManager::Get()->GetCurrentContext();
		if (!pState || !m_pQueueView) {
			wxBell();
			return;
		}
		CManualTransfer dlg(m_pQueueView);
		dlg.Run(this, pState);
	}
	else if (event.GetId() == XRCID("ID_BOOKMARK_ADD") || event.GetId() == XRCID("ID_BOOKMARK_MANAGE")) {
		CState* pState = CContextManager::Get()->GetCurrentContext();
		if (!pState) {
			return;
		}

		Site const old_site = pState->GetSite();
		std::wstring sitePath = old_site.SitePath();

		CContextControl::_context_controls* controls = m_pContextControl->GetCurrentControls();
		if (!controls) {
			return;
		}

		// controls->last_bookmark_path can get modified if it's empty now
		int res;
		if (event.GetId() == XRCID("ID_BOOKMARK_ADD")) {
			CNewBookmarkDialog dlg(this, sitePath, old_site ? &old_site : 0);
			res = dlg.Run(pState->GetLocalDir().GetPath(), pState->GetRemotePath());
		}
		else {
			CBookmarksDialog dlg(this, sitePath, old_site ? &old_site : 0);
			res = dlg.Run();
		}
		if (res == wxID_OK) {
			if (!sitePath.empty()) {
				std::unique_ptr<Site> site = CSiteManager::GetSiteByPath(sitePath, false).first;
				if (site) {
					for (int i = 0; i < m_pContextControl->GetTabCount(); ++i) {
						CContextControl::_context_controls *tab_controls = m_pContextControl->GetControlsFromTabIndex(i);
						if (tab_controls) {
							tab_controls->pState->UpdateSite(old_site.SitePath(), *site);
						}
					}
				}
			}
		}
	}
	else if (event.GetId() == XRCID("ID_MENU_HELP_WELCOME")) {
		CWelcomeDialog dlg;
		dlg.Run(this, true);
	}
	else if (event.GetId() == XRCID("ID_MENU_TRANSFER_SPEEDLIMITS_ENABLE")) {
		bool enable = COptions::Get()->GetOptionVal(OPTION_SPEEDLIMIT_ENABLE) == 0;

		const int downloadLimit = COptions::Get()->GetOptionVal(OPTION_SPEEDLIMIT_INBOUND);
		const int uploadLimit = COptions::Get()->GetOptionVal(OPTION_SPEEDLIMIT_OUTBOUND);
		if (enable && !downloadLimit && !uploadLimit) {
			CSpeedLimitsDialog dlg;
			dlg.Run(this);
		}
		else {
			COptions::Get()->SetOption(OPTION_SPEEDLIMIT_ENABLE, enable ? 1 : 0);
		}
	}
	else if (event.GetId() == XRCID("ID_MENU_TRANSFER_SPEEDLIMITS_CONFIGURE")) {
		CSpeedLimitsDialog dlg;
		dlg.Run(this);
	}
	else if (event.GetId() == m_comparisonToggleAcceleratorId) {
		CState* pState = CContextManager::Get()->GetCurrentContext();
		if (!pState) {
			return;
		}

		int old_mode = COptions::Get()->GetOptionVal(OPTION_COMPARISONMODE);
		COptions::Get()->SetOption(OPTION_COMPARISONMODE, old_mode ? 0 : 1);

		CComparisonManager* pComparisonManager = pState->GetComparisonManager();
		if (pComparisonManager && pComparisonManager->IsComparing()) {
			pComparisonManager->CompareListings();
		}
	}
	else if (HandleKeyboardCommand(event, *this)) {
		return;
	}
	else {
		for (int i = 0; i < 10; ++i) {
			if (event.GetId() != tab_hotkey_ids[i]) {
				continue;
			}

			if (!m_pContextControl) {
				return;
			}

			int sel = i - 1;
			if (sel < 0) {
				sel = 9;
			}
			m_pContextControl->SelectTab(sel);

			return;
		}

		std::unique_ptr<Site> pData = CSiteManager::GetSiteById(event.GetId());

		if (!pData) {
			event.Skip();
		}
		else {
			ConnectToSite(*pData, pData->m_default_bookmark);
		}
	}
}

void CMainFrame::OnEngineEvent(CFileZillaEngine* engine)
{
	CallAfter(&CMainFrame::DoOnEngineEvent, engine);
}

void CMainFrame::DoOnEngineEvent(CFileZillaEngine* engine)
{
	const std::vector<CState*> *pStates = CContextManager::Get()->GetAllStates();
	CState* pState = 0;
	for (std::vector<CState*>::const_iterator iter = pStates->begin(); iter != pStates->end(); ++iter) {
		if ((*iter)->m_pEngine != engine) {
			continue;
		}

		pState = *iter;
		break;
	}
	if (!pState) {
		return;
	}

	std::unique_ptr<CNotification> pNotification = pState->m_pEngine->GetNextNotification();
	while (pNotification) {
		switch (pNotification->GetID())
		{
		case nId_logmsg:
			if (m_pStatusView) {
				m_pStatusView->AddToLog(std::move(static_cast<CLogmsgNotification&>(*pNotification.get())));
			}
			if (COptions::Get()->GetOptionVal(OPTION_MESSAGELOG_POSITION) == 2 && m_pQueuePane) {
				m_pQueuePane->Highlight(3);
			}
			break;
		case nId_operation:
			if (pState->m_pCommandQueue) {
				pState->m_pCommandQueue->Finish(unique_static_cast<COperationNotification>(std::move(pNotification)));
			}
			if (m_bQuit) {
				Close();
				return;
			}
			break;
		case nId_listing:
			{
				auto const& listingNotification = static_cast<CDirectoryListingNotification const&>(*pNotification.get());
				if (pState->m_pCommandQueue) {
					pState->m_pCommandQueue->ProcessDirectoryListing(listingNotification);
				}
			}
			break;
		case nId_asyncrequest:
			{
				auto pAsyncRequest = unique_static_cast<CAsyncRequestNotification>(std::move(pNotification));
				if (pAsyncRequest->GetRequestID() == reqId_fileexists) {
					if (m_pQueueView) {
						m_pQueueView->ProcessNotification(pState->m_pEngine, std::move(pAsyncRequest));
					}
				}
				else {
					if (pAsyncRequest->GetRequestID() == reqId_certificate) {
						pState->SetSecurityInfo(static_cast<CCertificateNotification&>(*pAsyncRequest));
					}
					if (m_pAsyncRequestQueue) {
						m_pAsyncRequestQueue->AddRequest(pState->m_pEngine, std::move(pAsyncRequest));
					}
				}
			}
			break;
		case nId_active:
			{
				CActiveNotification const& activeNotification = static_cast<CActiveNotification const&>(*pNotification.get());
				UpdateActivityLed(activeNotification.GetDirection());
			}
			break;
		case nId_transferstatus:
			if (m_pQueueView) {
				m_pQueueView->ProcessNotification(pState->m_pEngine, std::move(pNotification));
			}
			break;
		case nId_sftp_encryption:
			{
				pState->SetSecurityInfo(static_cast<CSftpEncryptionNotification&>(*pNotification));
			}
			break;
		case nId_local_dir_created:
			if (pState) {
				auto const& localDirCreatedNotification = static_cast<CLocalDirCreatedNotification const&>(*pNotification.get());
				pState->LocalDirCreated(localDirCreatedNotification.dir);
			}
			break;
		case nId_serverchange:
			if (pState) {
				auto const& notification = static_cast<ServerChangeNotification const&>(*pNotification.get());
				pState->ChangeServer(notification.newServer_);
			}
			break;
		default:
			break;
		}

		pNotification = pState->m_pEngine->GetNextNotification();
	}
}

void CMainFrame::OnUpdateLedTooltip(wxCommandEvent&)
{
	wxString tooltipText;

	wxFileOffset downloadSpeed = m_pQueueView ? m_pQueueView->GetCurrentDownloadSpeed() : 0;
	wxFileOffset uploadSpeed = m_pQueueView ? m_pQueueView->GetCurrentUploadSpeed() : 0;

	CSizeFormat::_format format = static_cast<CSizeFormat::_format>(COptions::Get()->GetOptionVal(OPTION_SIZE_FORMAT));
	if (format == CSizeFormat::bytes) {
		format = CSizeFormat::iec;
	}

	const wxString downloadSpeedStr = CSizeFormat::Format(downloadSpeed, true, format,
														  COptions::Get()->GetOptionVal(OPTION_SIZE_USETHOUSANDSEP) != 0,
														  COptions::Get()->GetOptionVal(OPTION_SIZE_DECIMALPLACES));
	const wxString uploadSpeedStr = CSizeFormat::Format(uploadSpeed, true, format,
														COptions::Get()->GetOptionVal(OPTION_SIZE_USETHOUSANDSEP) != 0,
														COptions::Get()->GetOptionVal(OPTION_SIZE_DECIMALPLACES));
	tooltipText.Printf(_("Download speed: %s/s\nUpload speed: %s/s"), downloadSpeedStr, uploadSpeedStr);

	m_pActivityLed[0]->SetToolTip(tooltipText);
	m_pActivityLed[1]->SetToolTip(tooltipText);
}

bool CMainFrame::CreateMainToolBar()
{
	wxGetApp().AddStartupProfileRecord("CMainFrame::CreateMainToolBar");
	if (m_pToolBar) {
#ifdef __WXMAC__
		if (m_pToolBar) {
			COptions::Get()->SetOption(OPTION_TOOLBAR_HIDDEN, m_pToolBar->IsShown() ? 0 : 1);
		}
#endif
		SetToolBar(0);
		delete m_pToolBar;
		m_pToolBar = 0;
	}

#ifndef __WXMAC__
	if (COptions::Get()->GetOptionVal(OPTION_TOOLBAR_HIDDEN) != 0) {
		return true;
	}
#endif

	m_pToolBar = CToolBar::Load(this);
	if (!m_pToolBar) {
		wxLogError(_("Cannot load toolbar from resource file"));
		return false;
	}
	SetToolBar(m_pToolBar);

#ifdef __WXMAC__
	if (COptions::Get()->GetOptionVal(OPTION_TOOLBAR_HIDDEN) != 0) {
		m_pToolBar->Show(false);
	}
#endif


	if (m_pQuickconnectBar) {
		m_pQuickconnectBar->Refresh();
	}

	return true;
}

void CMainFrame::OnDisconnect(wxCommandEvent&)
{
	CState* pState = CContextManager::Get()->GetCurrentContext();
	if (!pState || !pState->IsRemoteConnected()) {
		return;
	}

	if (!pState->IsRemoteIdle()) {
		return;
	}

	pState->Disconnect();
}

void CMainFrame::OnCancel(wxCommandEvent&)
{
	CState* pState = CContextManager::Get()->GetCurrentContext();
	if (!pState || pState->m_pCommandQueue->Idle()) {
		return;
	}

	if (wxMessageBoxEx(_("Really cancel current operation?"), _T("FileZilla"), wxYES_NO | wxICON_QUESTION) == wxYES) {
		pState->m_pCommandQueue->Cancel();
		pState->GetRemoteRecursiveOperation()->StopRecursiveOperation();
	}
}

#ifdef __WXMSW__

BOOL CALLBACK FzEnumThreadWndProc(HWND hwnd, LPARAM lParam)
{
	// This function enumerates all dialogs and calls EndDialog for them
	TCHAR buffer[10];
	int c = GetClassName(hwnd, buffer, 9);
	// #32770 is the dialog window class.
	if (c && !_tcscmp(buffer, _T("#32770")))
	{
		*((bool*)lParam) = true;
		EndDialog(hwnd, IDCANCEL);
		return FALSE;
	}

	return TRUE;
}
#endif //__WXMSW__


bool CMainFrame::CloseDialogsAndQuit(wxCloseEvent &event)
{
#ifndef __WXMAC__
	if (m_taskBarIcon) {
		delete m_taskBarIcon;
		m_taskBarIcon = 0;
		m_closeEvent = event.GetEventType();
		m_closeEventTimer.Start(1, true);
		return false;
	}
#endif

	// We need to close all other top level windows on the stack before closing the main frame.
	// In other words, all open dialogs need to be closed.
	static int prev_size = 0;

	int size = wxTopLevelWindows.size();
	static wxTopLevelWindow* pLast = 0;
	if (wxTopLevelWindows.size()) {
		wxWindowList::reverse_iterator iter = wxTopLevelWindows.rbegin();
		wxTopLevelWindow* pTop = (wxTopLevelWindow*)(*iter);
		while (pTop != this && (size != prev_size || pLast != pTop)) {
			if (!pTop) {
				++iter;
				if (iter == wxTopLevelWindows.rend()) {
					break;
				}
				pTop = (wxTopLevelWindow*)(*iter);
				continue;
			}

			wxDialog* pDialog = dynamic_cast<wxDialog*>(pTop);
			if (pDialog && pDialog->IsModal()) {
				pDialog->EndModal(wxID_CANCEL);
			}
			else {
				wxWindow* pParent = pTop->GetParent();
				if (m_pQueuePane && pParent == m_pQueuePane) {
					// It's the AUI frame manager hint window. Ignore it
					++iter;
					if (iter == wxTopLevelWindows.rend()) {
						break;
					}
					pTop = (wxTopLevelWindow*)(*iter);
					continue;
				}
				wxString title = pTop->GetTitle();
				pTop->Destroy();
			}

			prev_size = size;
			pLast = pTop;

			m_closeEvent = event.GetEventType();
			m_closeEventTimer.Start(1, true);

			return false;
		}
	}

#ifdef __WXMSW__
	// wxMessageBoxEx does not use wxTopLevelWindow, close it too
	bool dialog = false;
	EnumThreadWindows(GetCurrentThreadId(), FzEnumThreadWndProc, (LPARAM)&dialog);
	if (dialog) {
		m_closeEvent = event.GetEventType();
		m_closeEventTimer.Start(1, true);

		return false;
	}
#endif //__WXMSW__

	// At this point all other top level windows should be closed.
	return true;
}


void CMainFrame::OnClose(wxCloseEvent &event)
{
	if (!m_bQuit) {
		static bool quit_confirmation_displayed = false;
		if (quit_confirmation_displayed && event.CanVeto()) {
			event.Veto();
			return;
		}
		if (event.CanVeto()) {
			quit_confirmation_displayed = true;

			if (m_pQueueView && m_pQueueView->IsActive()) {
				CConditionalDialog dlg(this, CConditionalDialog::confirmexit, CConditionalDialog::yesno);
				dlg.SetTitle(_("Close FileZilla"));

				dlg.AddText(_("File transfers still in progress."));
				dlg.AddText(_("Do you really want to close FileZilla?"));

				if (!dlg.Run()) {
					event.Veto();
					quit_confirmation_displayed = false;
					return;
				}
				if (m_bQuit) {
					return;
				}
			}

			CEditHandler* pEditHandler = CEditHandler::Get();
			if (pEditHandler) {
				if (pEditHandler->GetFileCount(CEditHandler::remote, CEditHandler::edit) || pEditHandler->GetFileCount(CEditHandler::none, CEditHandler::upload) ||
					pEditHandler->GetFileCount(CEditHandler::none, CEditHandler::upload_and_remove) ||
					pEditHandler->GetFileCount(CEditHandler::none, CEditHandler::upload_and_remove_failed))
				{
					CConditionalDialog dlg(this, CConditionalDialog::confirmexit_edit, CConditionalDialog::yesno);
					dlg.SetTitle(_("Close FileZilla"));

					dlg.AddText(_("Some files are still being edited or need to be uploaded."));
					dlg.AddText(_("If you close FileZilla, your changes will be lost."));
					dlg.AddText(_("Do you really want to close FileZilla?"));

					if (!dlg.Run()) {
						event.Veto();
						quit_confirmation_displayed = false;
						return;
					}
					if (m_bQuit) {
						return;
					}
				}
			}
			quit_confirmation_displayed = false;
		}

		if (m_pWindowStateManager) {
			m_pWindowStateManager->Remember(OPTION_MAINWINDOW_POSITION);
			delete m_pWindowStateManager;
			m_pWindowStateManager = 0;
		}

		RememberSplitterPositions();

#ifdef __WXMAC__
		if (m_pToolBar) {
			COptions::Get()->SetOption(OPTION_TOOLBAR_HIDDEN, m_pToolBar->IsShown() ? 0 : 1);
		}
#endif
		m_bQuit = true;
	}

	Show(false);
	if (!CloseDialogsAndQuit(event)) {
		return;
	}

	// Getting deleted by wxWidgets
	for (int i = 0; i < 2; ++i) {
		m_pActivityLed[i] = 0;
	}
	m_pStatusBar = 0;
	m_pMenuBar = 0;
	m_pToolBar = 0;

	// We're no longer interested in these events
	delete m_pStateEventHandler;
	m_pStateEventHandler = 0;

	if (m_pQueueView && !m_pQueueView->Quit()) {
		if (event.CanVeto()) {
			event.Veto();
		}
		return;
	}

	CEditHandler* pEditHandler = CEditHandler::Get();
	if (pEditHandler) {
		pEditHandler->RemoveAll(true);
		pEditHandler->Release();
	}

	bool res = true;
	const std::vector<CState*> *pStates = CContextManager::Get()->GetAllStates();
	for (CState* pState : *pStates) {
		if (pState->GetLocalRecursiveOperation()) {
			pState->GetLocalRecursiveOperation()->StopRecursiveOperation();
		}
		if (pState->GetRemoteRecursiveOperation()) {
			pState->GetRemoteRecursiveOperation()->StopRecursiveOperation();
		}

		if (pState->m_pCommandQueue) {
			if (!pState->m_pCommandQueue->Quit()) {
				res = false;
			}
		}
	}

	if (!res) {
		if (event.CanVeto()) {
			event.Veto();
		}
		return;
	}

	if (m_pContextControl) {
		CContextControl::_context_controls* controls = m_pContextControl->GetCurrentControls();
		if (controls) {
			if (controls->pLocalListView) {
				controls->pLocalListView->SaveColumnSettings(OPTION_LOCALFILELIST_COLUMN_WIDTHS, OPTION_LOCALFILELIST_COLUMN_SHOWN, OPTION_LOCALFILELIST_COLUMN_ORDER);
			}
			if (controls->pRemoteListView) {
				controls->pRemoteListView->SaveColumnSettings(OPTION_REMOTEFILELIST_COLUMN_WIDTHS, OPTION_REMOTEFILELIST_COLUMN_SHOWN, OPTION_REMOTEFILELIST_COLUMN_ORDER);
			}
		}

		m_pContextControl->SaveTabs();
	}

	for (std::vector<CState*>::const_iterator iter = pStates->begin(); iter != pStates->end(); ++iter) {
		CState *pState = *iter;
		pState->DestroyEngine();
	}

	CSiteManager::ClearIdMap();

	bool filters_toggled = CFilterManager::HasActiveFilters(true) && !CFilterManager::HasActiveFilters(false);
	COptions::Get()->SetOption(OPTION_FILTERTOGGLESTATE, filters_toggled ? 1 : 0);

	Destroy();
}

void CMainFrame::OnReconnect(wxCommandEvent &)
{
	CState* pState = CContextManager::Get()->GetCurrentContext();
	if (!pState) {
		return;
	}

	if (pState->IsRemoteConnected() || !pState->IsRemoteIdle()) {
		return;
	}

	Site site = pState->GetLastSite();
	CServerPath path = pState->GetLastServerPath();
	Bookmark bm;
	bm.m_remoteDir = path;
	ConnectToSite(site, bm);
}

void CMainFrame::OnRefresh(wxCommandEvent &)
{
	CState* pState = CContextManager::Get()->GetCurrentContext();
	if (!pState) {
		return;
	}

	bool clear_cache = false;
	if (wxGetKeyState(WXK_CONTROL)) {
		clear_cache = true;
	}
	pState->RefreshRemote(clear_cache);
	pState->RefreshLocal();
}

void CMainFrame::OnTimer(wxTimerEvent& event)
{
	if (event.GetId() == m_closeEventTimer.GetId()) {
		if (m_closeEvent == 0) {
			return;
		}

		// When we get idle event, a dialog's event loop has been left.
		// Now we can close the top level window on the stack.
		wxCloseEvent *evt = new wxCloseEvent(m_closeEvent);
		evt->SetCanVeto(false);
		QueueEvent(evt);
	}
#if FZ_MANUALUPDATECHECK
	else if( event.GetId() == update_dialog_timer_.GetId() ) {
		TriggerUpdateDialog();
	}
#endif
}

void CMainFrame::OpenSiteManager(Site const* site)
{
	CState* pState = CContextManager::Get()->GetCurrentContext();
	if (!pState) {
		return;
	}

	CSiteManagerDialog dlg;

	std::vector<CSiteManagerDialog::_connected_site> connected_sites;

	if (site) {
		CSiteManagerDialog::_connected_site connected_site;
		connected_site.site = *site;
		connected_sites.push_back(connected_site);
	}

	for (int i = 0; i < m_pContextControl->GetTabCount(); ++i) {
		CContextControl::_context_controls *controls =  m_pContextControl->GetControlsFromTabIndex(i);
		if (!controls || !controls->pState) {
			continue;
		}

		Site site = controls->pState->GetSite();
		if (!site) {
			site = controls->pState->GetLastSite();
		}

		std::wstring const& path = site.SitePath();
		if (path.empty()) {
			continue;
		}

		auto it = std::find_if(connected_sites.cbegin(), connected_sites.cend(), [&](auto const& v) { return v.old_path == path; });
		if (it != connected_sites.cend()) {
			continue;
		}

		CSiteManagerDialog::_connected_site connected_site;
		connected_site.old_path = path;
		connected_site.site = site;
		connected_sites.push_back(connected_site);
	}

	if (!dlg.Create(this, &connected_sites, site)) {
		return;
	}

	int res = dlg.ShowModal();
	if (res == wxID_YES || res == wxID_OK) {
		// Update bookmark paths
		for (int i = 0; i < m_pContextControl->GetTabCount(); ++i) {
			CContextControl::_context_controls *controls = m_pContextControl->GetControlsFromTabIndex(i);
			if (!controls || !controls->pState) {
				continue;
			}

			controls->pState->UpdateKnownSites(connected_sites);
		}
	}

	if (res == wxID_YES) {
		Site data;
		Bookmark bookmark;
		if (!dlg.GetServer(data, bookmark)) {
			return;
		}

		ConnectToSite(data, bookmark);
	}
}

void CMainFrame::OnSiteManager(wxCommandEvent& e)
{
#ifdef __WXMAC__
	if (wxGetKeyState(WXK_SHIFT) ||
		wxGetKeyState(WXK_ALT) ||
		wxGetKeyState(WXK_CONTROL))
	{
		OnSitemanagerDropdown(e);
		return;
	}
#endif
	(void)e;
	OpenSiteManager();
}

void CMainFrame::UpdateActivityLed(int direction)
{
	if (m_pActivityLed[direction]) {
		m_pActivityLed[direction]->Ping();
	}
}

void CMainFrame::OnProcessQueue(wxCommandEvent& event)
{
	if (m_pQueueView) {
		m_pQueueView->SetActive(event.IsChecked());
	}
}

void CMainFrame::OnMenuEditSettings(wxCommandEvent&)
{
	CSettingsDialog dlg(m_engineContext);
	if (!dlg.Create(this)) {
		return;
	}

	COptions* pOptions = COptions::Get();

	wxString oldLang = pOptions->GetOption(OPTION_LANGUAGE);

	int oldShowDebugMenu = pOptions->GetOptionVal(OPTION_DEBUG_MENU) != 0;

	int res = dlg.ShowModal();
	if (res != wxID_OK) {
		return;
	}

	wxString newLang = pOptions->GetOption(OPTION_LANGUAGE);

	if (oldLang != newLang ||
		oldShowDebugMenu != pOptions->GetOptionVal(OPTION_DEBUG_MENU))
	{
		CreateMenus();
	}
	if (oldLang != newLang) {
		wxMessageBoxEx(_("FileZilla needs to be restarted for the language change to take effect."), _("Language changed"), wxICON_INFORMATION, this);
	}

	CheckChangedSettings();
}

void CMainFrame::OnToggleLogView(wxCommandEvent&)
{
	if (!m_pTopSplitter) {
		return;
	}

	bool shown;

	if (COptions::Get()->GetOptionVal(OPTION_MESSAGELOG_POSITION) == 1) {
		if (!m_pQueueLogSplitter) {
			return;
		}
		if (m_pQueueLogSplitter->IsSplit()) {
			m_pQueueLogSplitter->Unsplit(m_pStatusView);
			shown = false;
		}
		else if (m_pStatusView->IsShown()) {
			m_pStatusView->Hide();
			m_pBottomSplitter->Unsplit(m_pQueueLogSplitter);
			shown = false;
		}
		else if (!m_pQueueLogSplitter->IsShown()) {
			m_pQueueLogSplitter->Initialize(m_pStatusView);
			m_pBottomSplitter->SplitHorizontally(m_pContextControl, m_pQueueLogSplitter);
			shown = true;
		}
		else {
			m_pQueueLogSplitter->SplitVertically(m_pQueuePane, m_pStatusView);
			shown = true;
		}
	}
	else {
		if (m_pTopSplitter->IsSplit()) {
			m_pTopSplitter->Unsplit(m_pStatusView);
			shown = false;
		}
		else {
			m_pTopSplitter->SplitHorizontally(m_pStatusView, m_pBottomSplitter);
			shown = true;
		}
	}

	if (COptions::Get()->GetOptionVal(OPTION_MESSAGELOG_POSITION) != 2) {
		COptions::Get()->SetOption(OPTION_SHOW_MESSAGELOG, shown);
	}
}

void CMainFrame::OnToggleDirectoryTreeView(wxCommandEvent& event)
{
	if (!m_pContextControl) {
		return;
	}

	CContextControl::_context_controls* controls = m_pContextControl->GetCurrentControls();
	if (!controls) {
		return;
	}

	bool const local = event.GetId() == XRCID("ID_TOOLBAR_LOCALTREEVIEW") || event.GetId() == XRCID("ID_VIEW_LOCALTREE");
	CSplitterWindowEx* splitter = local ? controls->pLocalSplitter : controls->pRemoteSplitter;
	bool show = !splitter->IsSplit();
	ShowDirectoryTree(local, show);
}

void CMainFrame::ShowDirectoryTree(bool local, bool show)
{
	if (!m_pContextControl) {
		return;
	}

	const int layout = COptions::Get()->GetOptionVal(OPTION_FILEPANE_LAYOUT);
	const int swap = COptions::Get()->GetOptionVal(OPTION_FILEPANE_SWAP);
	for (int i = 0; i < m_pContextControl->GetTabCount(); ++i) {
		CContextControl::_context_controls* controls = m_pContextControl->GetControlsFromTabIndex(i);
		if (!controls) {
			continue;
		}

		CSplitterWindowEx* splitter = local ? controls->pLocalSplitter : controls->pRemoteSplitter;
		CView* tree = local ? controls->pLocalTreeViewPanel : controls->pRemoteTreeViewPanel;
		CView* list = local ? controls->pLocalListViewPanel : controls->pRemoteListViewPanel;

		if (show && !splitter->IsSplit()) {
			tree->SetHeader(list->DetachHeader());

			if (layout == 3 && swap) {
				splitter->SplitVertically(list, tree);
			}
			else if (layout) {
				splitter->SplitVertically(tree, list);
			}
			else {
				splitter->SplitHorizontally(tree, list);
			}
		}
		else if (!show && splitter->IsSplit()) {
			list->SetHeader(tree->DetachHeader());
			splitter->Unsplit(tree);
		}
	}

	COptions::Get()->SetOption(local ? OPTION_SHOW_TREE_LOCAL : OPTION_SHOW_TREE_REMOTE, show);
}

void CMainFrame::OnToggleQueueView(wxCommandEvent&)
{
	if (!m_pBottomSplitter) {
		return;
	}

	bool shown;
	if (COptions::Get()->GetOptionVal(OPTION_MESSAGELOG_POSITION) == 1) {
		if (!m_pQueueLogSplitter) {
			return;
		}
		if (m_pQueueLogSplitter->IsSplit()) {
			m_pQueueLogSplitter->Unsplit(m_pQueuePane);
			shown = false;
		}
		else if (m_pQueuePane->IsShown()) {
			m_pQueuePane->Hide();
			m_pBottomSplitter->Unsplit(m_pQueueLogSplitter);
			shown = false;
		}
		else if (!m_pQueueLogSplitter->IsShown()) {
			m_pQueueLogSplitter->Initialize(m_pQueuePane);
			m_pBottomSplitter->SplitHorizontally(m_pContextControl, m_pQueueLogSplitter);
			shown = true;
		}
		else {
			m_pQueueLogSplitter->SplitVertically(m_pQueuePane, m_pStatusView);
			shown = true;
		}
	}
	else {
		if (m_pBottomSplitter->IsSplit()) {
			m_pBottomSplitter->Unsplit(m_pQueueLogSplitter);
		}
		else {
			m_pQueueLogSplitter->Initialize(m_pQueuePane);
			m_pBottomSplitter->SplitHorizontally(m_pContextControl, m_pQueueLogSplitter);
		}
		shown = m_pBottomSplitter->IsSplit();
	}

	COptions::Get()->SetOption(OPTION_SHOW_QUEUE, shown);
}

void CMainFrame::OnMenuHelpAbout(wxCommandEvent&)
{
	CAboutDialog dlg;
	if (!dlg.Create(this)) {
		return;
	}

	dlg.ShowModal();
}

void CMainFrame::OnFilter(wxCommandEvent& event)
{
	if (wxGetKeyState(WXK_SHIFT)) {
		OnFilterRightclicked(event);
		return;
	}

	bool const oldActive = CFilterManager::HasActiveFilters();

	CFilterDialog dlg;
	dlg.Create(this);
	dlg.ShowModal();

	if (oldActive == CFilterManager::HasActiveFilters() && m_pToolBar) {
		// Restore state
		m_pToolBar->ToggleTool(XRCID("ID_TOOLBAR_FILTER"), oldActive);
	}
}

#if FZ_MANUALUPDATECHECK
void CMainFrame::OnCheckForUpdates(wxCommandEvent& event)
{
	if (!m_pUpdater) {
		return;
	}

	if (event.GetId() == XRCID("ID_CHECKFORUPDATES") || (!COptions::Get()->GetOptionVal(OPTION_DEFAULT_DISABLEUPDATECHECK) && COptions::Get()->GetOptionVal(OPTION_UPDATECHECK) != 0)) {
		m_pUpdater->RunIfNeeded();
	}

	update_dialog_timer_.Stop();
	CUpdateDialog dlg(this, *m_pUpdater);
	dlg.ShowModal();
	update_dialog_timer_.Stop();
}

void CMainFrame::UpdaterStateChanged(UpdaterState s, build const& v)
{
#if FZ_AUTOUPDATECHECK
	if (!m_pMenuBar) {
		return;
	}

	if (s == UpdaterState::idle) {
		wxMenu* m = 0;
		wxMenuItem* pItem = m_pMenuBar->FindItem(GetAvailableUpdateMenuId(), &m);
		if (pItem && m) {
			for (size_t i = 0; i != m_pMenuBar->GetMenuCount(); ++i) {
				if( m_pMenuBar->GetMenu(i) == m ) {
					m_pMenuBar->Remove(i);
					delete m;
					break;
				}
			}
		}
		return;
	}
	else if (s != UpdaterState::newversion && s != UpdaterState::newversion_ready && s != UpdaterState::newversion_stale) {
		return;
	}
	
	wxString const name = v.version_.empty() ? _("Unknown version") : wxString::Format(_("&Version %s"), v.version_);

	wxMenuItem* pItem = m_pMenuBar->FindItem(GetAvailableUpdateMenuId());
	if (!pItem) {
		wxMenu* pMenu = new wxMenu();
		pMenu->Append(GetAvailableUpdateMenuId(), name);
		m_pMenuBar->Append(pMenu, _("&New version available!"));

		if (!update_dialog_timer_.IsRunning()) {
			update_dialog_timer_.Start(1, true);
		}
	}
	else {
		pItem->SetItemLabel(name);
	}
#endif
}

void CMainFrame::TriggerUpdateDialog()
{
	if (m_bQuit || !m_pUpdater) {
		return;
	}

	if (CUpdateDialog::IsRunning()) {
		return;
	}

	if (!wxDialogEx::CanShowPopupDialog()) {
		update_dialog_timer_.Start(1000, true);
		return;
	}

	CUpdateDialog dlg(this, *m_pUpdater);
	dlg.ShowModal();

	// In case the timer was started while the dialog was up.
	update_dialog_timer_.Stop();
}
#endif

void CMainFrame::UpdateLayout()
{
	int const layout = COptions::Get()->GetOptionVal(OPTION_FILEPANE_LAYOUT);
	int const swap = COptions::Get()->GetOptionVal(OPTION_FILEPANE_SWAP);

	int const messagelog_position = COptions::Get()->GetOptionVal(OPTION_MESSAGELOG_POSITION);

	// First handle changes in message log position as it can make size of the other panes change
	{
		bool shown = m_pStatusView->IsShown();
		wxWindow* parent = m_pStatusView->GetParent();

		bool changed;
		if (parent == m_pTopSplitter && messagelog_position != 0) {
			if (shown) {
				m_pTopSplitter->Unsplit(m_pStatusView);
			}
			changed = true;
		}
		else if (parent == m_pQueueLogSplitter && messagelog_position != 1) {
			if (shown) {
				if (m_pQueueLogSplitter->IsSplit()) {
					m_pQueueLogSplitter->Unsplit(m_pStatusView);
				}
				else {
					m_pBottomSplitter->Unsplit(m_pQueueLogSplitter);
				}
			}
			changed = true;
		}
		else if (parent != m_pTopSplitter && parent != m_pQueueLogSplitter && messagelog_position != 2) {
			m_pQueuePane->RemovePage(3);
			changed = true;
			shown = true;
		}
		else {
			changed = false;
		}

		if (changed) {
			switch (messagelog_position) {
			default:
				m_pStatusView->Reparent(m_pTopSplitter);
				if (shown) {
					m_pTopSplitter->SplitHorizontally(m_pStatusView, m_pBottomSplitter);
				}
				break;
			case 1:
				m_pStatusView->Reparent(m_pQueueLogSplitter);
				if (shown) {
					if (m_pQueueLogSplitter->IsShown()) {
						m_pQueueLogSplitter->SplitVertically(m_pQueuePane, m_pStatusView);
					}
					else {
						m_pQueueLogSplitter->Initialize(m_pStatusView);
						m_pBottomSplitter->SplitHorizontally(m_pContextControl, m_pQueueLogSplitter);
					}
				}
				break;
			case 2:
				m_pQueuePane->AddPage(m_pStatusView, _("Message log"));
				break;
			}
		}
	}

	// Now the other panes
	for (int i = 0; i < m_pContextControl->GetTabCount(); ++i) {
		CContextControl::_context_controls *controls = m_pContextControl->GetControlsFromTabIndex(i);
		if (!controls) {
			continue;
		}

		int mode;
		if (!layout || layout == 2 || layout == 3) {
			mode = wxSPLIT_VERTICAL;
		}
		else {
			mode = wxSPLIT_HORIZONTAL;
		}

		int isMode = controls->pViewSplitter->GetSplitMode();

		int isSwap = controls->pViewSplitter->GetWindow1() == controls->pRemoteSplitter ? 1 : 0;

		if (mode != isMode || swap != isSwap) {
			controls->pViewSplitter->Unsplit();
			if (mode == wxSPLIT_VERTICAL) {
				if (swap) {
					controls->pViewSplitter->SplitVertically(controls->pRemoteSplitter, controls->pLocalSplitter);
				}
				else {
					controls->pViewSplitter->SplitVertically(controls->pLocalSplitter, controls->pRemoteSplitter);
				}
			}
			else {
				if (swap) {
					controls->pViewSplitter->SplitHorizontally(controls->pRemoteSplitter, controls->pLocalSplitter);
				}
				else {
					controls->pViewSplitter->SplitHorizontally(controls->pLocalSplitter, controls->pRemoteSplitter);
				}
			}
		}

		if (controls->pLocalSplitter->IsSplit()) {
			if (!layout) {
				mode = wxSPLIT_HORIZONTAL;
			}
			else {
				mode = wxSPLIT_VERTICAL;
			}

			wxWindow* pFirst;
			wxWindow* pSecond;
			if (layout == 3 && swap) {
				pFirst = controls->pLocalListViewPanel;
				pSecond = controls->pLocalTreeViewPanel;
			}
			else {
				pFirst = controls->pLocalTreeViewPanel;
				pSecond = controls->pLocalListViewPanel;
			}

			if (mode != controls->pLocalSplitter->GetSplitMode() || pFirst != controls->pLocalSplitter->GetWindow1()) {
				controls->pLocalSplitter->Unsplit();
				if (mode == wxSPLIT_VERTICAL) {
					controls->pLocalSplitter->SplitVertically(pFirst, pSecond);
				}
				else {
					controls->pLocalSplitter->SplitHorizontally(pFirst, pSecond);
				}
			}
		}

		if (controls->pRemoteSplitter->IsSplit()) {
			if (!layout) {
				mode = wxSPLIT_HORIZONTAL;
			}
			else {
				mode = wxSPLIT_VERTICAL;
			}

			wxWindow* pFirst;
			wxWindow* pSecond;
			if (layout == 3 && !swap) {
				pFirst = controls->pRemoteListViewPanel;
				pSecond = controls->pRemoteTreeViewPanel;
			}
			else {
				pFirst = controls->pRemoteTreeViewPanel;
				pSecond = controls->pRemoteListViewPanel;
			}

			if (mode != controls->pRemoteSplitter->GetSplitMode() || pFirst != controls->pRemoteSplitter->GetWindow1()) {
				controls->pRemoteSplitter->Unsplit();
				if (mode == wxSPLIT_VERTICAL) {
					controls->pRemoteSplitter->SplitVertically(pFirst, pSecond);
				}
				else {
					controls->pRemoteSplitter->SplitHorizontally(pFirst, pSecond);
				}
			}
		}

		if (layout == 3) {
			if (!swap) {
				controls->pRemoteSplitter->SetSashGravity(1.0);
				controls->pLocalSplitter->SetSashGravity(0.0);
			}
			else {
				controls->pLocalSplitter->SetSashGravity(1.0);
				controls->pRemoteSplitter->SetSashGravity(0.0);
			}
		}
		else {
			controls->pLocalSplitter->SetSashGravity(0.0);
			controls->pRemoteSplitter->SetSashGravity(0.0);
		}
	}
}

void CMainFrame::OnSitemanagerDropdown(wxCommandEvent& event)
{
	if (!m_pToolBar) {
		return;
	}

	std::unique_ptr<wxMenu> pMenu = CSiteManager::GetSitesMenu();
	if (pMenu) {
		ShowDropdownMenu(pMenu.release(), m_pToolBar, event);
	}
}

bool CMainFrame::ConnectToSite(Site & data, Bookmark const& bookmark, CState* pState)
{
	// First check if we need to ask user for a password
	if (!CLoginManager::Get().GetPassword(data, false)) {
		return false;
	}

	// Check if current state is already connected and if needed ask whether to open in new tab
	if (!pState) {
		pState = CContextManager::Get()->GetCurrentContext();
		if (!pState) {
			return false;
		}
	}

	if (pState->IsRemoteConnected() || !pState->IsRemoteIdle()) {
		int action = COptions::Get()->GetOptionVal(OPTION_ALREADYCONNECTED_CHOICE);
		if (action < 2) {
			wxDialogEx dlg;
			if (!dlg.Load(this, _T("ID_ALREADYCONNECTED"))) {
				return false;
			}

			if (action != 0) {
				XRCCTRL(dlg, "ID_OLDTAB", wxRadioButton)->SetValue(true);
			}
			else {
				XRCCTRL(dlg, "ID_NEWTAB", wxRadioButton)->SetValue(true);
			}

			if (dlg.ShowModal() != wxID_OK) {
				return false;
			}

			if (XRCCTRL(dlg, "ID_NEWTAB", wxRadioButton)->GetValue()) {
				action = 0;
			}
			else {
				action = 1;
			}

			if (XRCCTRL(dlg, "ID_REMEMBER", wxCheckBox)->IsChecked()) {
				action |= 2;
			}
			COptions::Get()->SetOption(OPTION_ALREADYCONNECTED_CHOICE, action);
		}

		if (!(action & 1)) {
			if (!m_pContextControl->CreateTab()) {
				return false;
			}
			pState = CContextManager::Get()->GetCurrentContext();
		}
	}

	// Next tell the state to connect
	if (!pState->Connect(data, bookmark.m_remoteDir, bookmark.m_comparison)) {
		return false;
	}

	// Apply comparison and sync browsing options
	// FIXME: Move to state?
	if (!bookmark.m_localDir.empty()) {
		bool set = pState->SetLocalDir(bookmark.m_localDir, 0, false);

		if (set && bookmark.m_sync) {
			wxASSERT(!bookmark.m_remoteDir.empty());
			pState->SetSyncBrowse(true, bookmark.m_remoteDir);
		}
	}

	if (bookmark.m_comparison && pState->GetComparisonManager()) {
		pState->GetComparisonManager()->CompareListings();
	}

	return true;
}

void CMainFrame::CheckChangedSettings()
{
	m_pAsyncRequestQueue->RecheckDefaults();

	CAutoAsciiFiles::SettingsChanged();

#if FZ_MANUALUPDATECHECK
	if (m_pUpdater) {
		m_pUpdater->Init();
	}
#endif
}

void CMainFrame::ConnectNavigationHandler(wxEvtHandler* handler)
{
	if (!handler) {
		return;
	}

	handler->Connect(wxEVT_NAVIGATION_KEY, wxNavigationKeyEventHandler(CMainFrame::OnNavigationKeyEvent), 0, this);
}

void CMainFrame::OnNavigationKeyEvent(wxNavigationKeyEvent& event)
{
	if (wxGetKeyState(WXK_CONTROL) && event.IsFromTab()) {
		if (m_pContextControl) {
			m_pContextControl->AdvanceTab(event.GetDirection());
		}
		return;
	}

	event.Skip();
}

void CMainFrame::OnChar(wxKeyEvent& event)
{
	if (event.GetKeyCode() == WXK_F7) {
		auto * controls = m_pContextControl->GetCurrentControls();
		if (controls) {
			controls->SwitchFocusedSide();
		}
		return;
	}
	else if (event.GetKeyCode() != WXK_F6) {
		event.Skip();
		return;
	}

	// Jump between quickconnect bar and view headers

	std::list<wxWindow*> windowOrder;
	if (m_pQuickconnectBar) {
		windowOrder.push_back(m_pQuickconnectBar);
	}
	CContextControl::_context_controls* controls = m_pContextControl->GetCurrentControls();
	if (controls) {
		windowOrder.push_back(controls->pLocalViewHeader);
		windowOrder.push_back(controls->pRemoteViewHeader);
	}

	wxWindow* focused = FindFocus();

	bool skipFirst = false;
	std::list<wxWindow*>::iterator iter;
	if (!focused) {
		iter = windowOrder.begin();
		skipFirst = false;
	}
	else {
		wxWindow *parent = focused->GetParent();
		for (iter = windowOrder.begin(); iter != windowOrder.end(); ++iter) {
			if (*iter == focused || *iter == parent) {
				skipFirst = true;
				break;
			}
		}
		if (iter == windowOrder.end()) {
			iter = windowOrder.begin();
			skipFirst = false;
		}
	}

	FocusNextEnabled(windowOrder, iter, skipFirst, !event.ShiftDown());
}

void CMainFrame::FocusNextEnabled(std::list<wxWindow*>& windowOrder, std::list<wxWindow*>::iterator iter, bool skipFirst, bool forward)
{
	std::list<wxWindow*>::iterator start = iter;

	while (skipFirst || !(*iter)->IsShownOnScreen() || !(*iter)->IsEnabled()) {
		skipFirst = false;
		if (forward) {
			++iter;
			if (iter == windowOrder.end()) {
				iter = windowOrder.begin();
			}
		}
		else {
			if (iter == windowOrder.begin()) {
				iter = windowOrder.end();
			}
			--iter;
		}

		if (iter == start) {
			wxBell();
			return;
		}
	}

	(*iter)->SetFocus();
}

void CMainFrame::RememberSplitterPositions()
{
	CContextControl::_context_controls* controls = m_pContextControl ? m_pContextControl->GetCurrentControls() : 0;
	if (!controls) {
		return;
	}

	auto const positions = controls->GetSplitterPositions();
	std::wstring const posString = fz::sprintf(
		L"%d %d %d %d %d %d",
		// top_pos
		m_pTopSplitter->GetSashPosition(),

		// bottom_height
		m_pBottomSplitter->GetSashPosition(),

		// view_pos
		// Note that we cannot use %f, it is locale-dependent
		static_cast<int>(std::get<0>(positions) * 1000000000),

		// local_pos
		std::get<1>(positions),

		// remote_pos
		std::get<2>(positions),

		// queuelog splitter
		static_cast<int>(m_pQueueLogSplitter->GetRelativeSashPosition() * 1000000000)
	);

	COptions::Get()->SetOption(OPTION_MAINWINDOW_SPLITTER_POSITION, posString);
}

bool CMainFrame::RestoreSplitterPositions()
{
	if (wxGetKeyState(WXK_SHIFT) && wxGetKeyState(WXK_ALT) && wxGetKeyState(WXK_CONTROL)) {
		return false;
	}

	// top_pos bottom_height view_pos view_height_width local_pos remote_pos
	std::wstring const positions = COptions::Get()->GetOption(OPTION_MAINWINDOW_SPLITTER_POSITION);
	auto tokens = fz::strtok_view(positions, L" ");
	if (tokens.size() < 6) {
		return false;
	}

	int values[6];
	for (size_t i = 0; i < 6; ++i) {
		values[i] = fz::to_integral(tokens[i], std::numeric_limits<int>::min());
		if (values[i] == std::numeric_limits<int>::min()) {
			return false;
		}
	}

	if (m_pTopSplitter) {
		m_pTopSplitter->SetSashPosition(values[0]);
	}
	if (m_pBottomSplitter) {
		m_pBottomSplitter->SetSashPosition(values[1]);
	}

	if (m_pContextControl) {
		double pos = static_cast<double>(values[2]) / 1000000000;
		for (int i = 0; i < m_pContextControl->GetTabCount(); ++i) {
			auto controls = m_pContextControl->GetControlsFromTabIndex(i);
			if (!controls) {
				continue;
			}
			controls->SetSplitterPositions(std::make_tuple(pos, values[3], values[4]));
		}
	}

	double pos = static_cast<double>(values[5]) / 1000000000;
	if (pos >= 0 && pos <= 1 && m_pQueueLogSplitter) {
		m_pQueueLogSplitter->SetRelativeSashPosition(pos);
	}

	return true;
}

void CMainFrame::SetDefaultSplitterPositions()
{
	if (m_pTopSplitter) {
		m_pTopSplitter->SetSashPosition(97);
	}

	wxSize size = m_pBottomSplitter->GetClientSize();
	int h = size.GetHeight() - 135;
	if (h < 50) {
		h = 50;
	}
	if (m_pBottomSplitter) {
		m_pBottomSplitter->SetSashPosition(h);
	}

	if (m_pQueueLogSplitter) {
		m_pQueueLogSplitter->SetSashPosition(0);
	}

	if (m_pContextControl) {
		for (int i = 0; i < m_pContextControl->GetTabCount(); ++i) {
			CContextControl::_context_controls* controls = m_pContextControl->GetControlsFromTabIndex(i);
			if (!controls) {
				continue;
			}
			if (controls->pViewSplitter) {
				controls->pViewSplitter->SetRelativeSashPosition(0.5);
			}
			if (controls->pLocalSplitter) {
				controls->pLocalSplitter->SetRelativeSashPosition(0.4);
			}
			if (controls->pRemoteSplitter) {
				controls->pRemoteSplitter->SetRelativeSashPosition(0.4);
			}
		}
	}
}

void CMainFrame::OnActivate(wxActivateEvent& event)
{
	// According to the wx docs we should do this
	event.Skip();

	if (!event.GetActive()) {
		return;
	}

#if defined(__WXMAC__) && !wxCHECK_VERSION(3, 1, 0)
	// wxMac looses focus information if the window becomes inactive.
	// Restore focus to the previously focused child, otherwise focus ends up
	// in the quickconnect bar.
	// Go via ID of the last focused child to avoid issues with window lifetime.
	if (m_lastFocusedChild != -1) {
		m_winLastFocused = FindWindow(m_lastFocusedChild);
	}
#endif

	CEditHandler* pEditHandler = CEditHandler::Get();
	if (pEditHandler) {
		pEditHandler->CheckForModifications(true);
	}

	if (m_pAsyncRequestQueue) {
		m_pAsyncRequestQueue->TriggerProcessing();
	}
}

void CMainFrame::OnToolbarComparison(wxCommandEvent&)
{
	CState* pState = CContextManager::Get()->GetCurrentContext();
	if (!pState) {
		return;
	}

	CComparisonManager* pComparisonManager = pState->GetComparisonManager();
	if (pComparisonManager->IsComparing()) {
		pComparisonManager->ExitComparisonMode();
		return;
	}

	if (!COptions::Get()->GetOptionVal(OPTION_FILEPANE_LAYOUT)) {
		CContextControl::_context_controls* controls = m_pContextControl->GetCurrentControls();
		if (!controls) {
			return;
		}

		if ((controls->pLocalListSearchPanel && controls->pLocalListSearchPanel->IsShown()) || (controls->pRemoteListSearchPanel && controls->pRemoteListSearchPanel->IsShown())) {
			CConditionalDialog dlg(this, CConditionalDialog::quick_search, CConditionalDialog::yesno);
			dlg.SetTitle(_("Directory comparison"));
			dlg.AddText(_("To compare directories quick search must be closed."));
			dlg.AddText(_("Close quick search and continue comparing?"));
			if (!dlg.Run()) {
				return;
			}

			if (controls->pLocalListSearchPanel && controls->pLocalListSearchPanel->IsShown()) {
				controls->pLocalListSearchPanel->Close();
			}

			if (controls->pRemoteListSearchPanel && controls->pRemoteListSearchPanel->IsShown()) {
				controls->pRemoteListSearchPanel->Close();
			}
		}

		if ((controls->pLocalSplitter->IsSplit() && !controls->pRemoteSplitter->IsSplit()) ||
			(!controls->pLocalSplitter->IsSplit() && controls->pRemoteSplitter->IsSplit()))
		{
			CConditionalDialog dlg(this, CConditionalDialog::compare_treeviewmismatch, CConditionalDialog::yesno);
			dlg.SetTitle(_("Directory comparison"));
			dlg.AddText(_("To compare directories, both file lists have to be aligned."));
			dlg.AddText(_("To do this, the directory trees need to be both shown or both hidden."));
			dlg.AddText(_("Show both directory trees and continue comparing?"));
			if (!dlg.Run()) {
				// Needed to restore non-toggle state of button
				pState->NotifyHandlers(STATECHANGE_COMPARISON);
				return;
			}

			ShowDirectoryTree(true, true);
			ShowDirectoryTree(false, true);
		}

		int pos = (controls->pLocalSplitter->GetSashPosition() + controls->pRemoteSplitter->GetSashPosition()) / 2;
		controls->pLocalSplitter->SetSashPosition(pos);
		controls->pRemoteSplitter->SetSashPosition(pos);
	}

	pComparisonManager->CompareListings();
}

void CMainFrame::OnToolbarComparisonDropdown(wxCommandEvent& event)
{
	if (!m_pToolBar) {
		return;
	}

	auto menu = new wxMenu;
    menu->Append(XRCID("ID_TOOLBAR_COMPARISON"), _("&Enable"), wxString(), wxITEM_CHECK);

    menu->AppendSeparator();
	menu->Append(XRCID("ID_COMPARE_SIZE"), _("Compare file&size"), wxString(), wxITEM_RADIO);
	menu->Append(XRCID("ID_COMPARE_DATE"), _("Compare &modification time"), wxString(), wxITEM_RADIO);

    menu->AppendSeparator();
    menu->Append(XRCID("ID_COMPARE_HIDEIDENTICAL"), _("&Hide identical files"), wxString(), wxITEM_CHECK);

	CState* pState = CContextManager::Get()->GetCurrentContext();
	if (!pState) {
		return;
	}

	CComparisonManager* pComparisonManager = pState->GetComparisonManager();
	menu->FindItem(XRCID("ID_TOOLBAR_COMPARISON"))->Check(pComparisonManager->IsComparing());

	const int mode = COptions::Get()->GetOptionVal(OPTION_COMPARISONMODE);
	if (mode == 0) {
		menu->FindItem(XRCID("ID_COMPARE_SIZE"))->Check();
	}
	else {
		menu->FindItem(XRCID("ID_COMPARE_DATE"))->Check();
	}

	menu->Check(XRCID("ID_COMPARE_HIDEIDENTICAL"), COptions::Get()->GetOptionVal(OPTION_COMPARE_HIDEIDENTICAL) != 0);

	ShowDropdownMenu(menu, m_pToolBar, event);
}

void CMainFrame::ShowDropdownMenu(wxMenu* pMenu, wxToolBar* pToolBar, wxCommandEvent& event)
{
#ifdef EVT_TOOL_DROPDOWN
	if (event.GetEventType() == wxEVT_COMMAND_TOOL_DROPDOWN_CLICKED) {
		pToolBar->SetDropdownMenu(event.GetId(), pMenu);
		event.Skip();
	}
	else
#endif
	{
#ifdef __WXMSW__
		RECT r;
		if (::SendMessage((HWND)pToolBar->GetHandle(), TB_GETITEMRECT, pToolBar->GetToolPos(event.GetId()), (LPARAM)&r)) {
			pToolBar->PopupMenu(pMenu, r.left, r.bottom);
		}
		else
#endif
			pToolBar->PopupMenu(pMenu);

		delete pMenu;
	}
}

void CMainFrame::OnDropdownComparisonMode(wxCommandEvent& event)
{
	CState* pState = CContextManager::Get()->GetCurrentContext();
	if (!pState) {
		return;
	}

	int old_mode = COptions::Get()->GetOptionVal(OPTION_COMPARISONMODE);
	int new_mode = (event.GetId() == XRCID("ID_COMPARE_SIZE")) ? 0 : 1;
	COptions::Get()->SetOption(OPTION_COMPARISONMODE, new_mode);

	CComparisonManager* pComparisonManager = pState->GetComparisonManager();
	if (old_mode != new_mode && pComparisonManager) {
		pComparisonManager->SetComparisonMode(new_mode);
		if (pComparisonManager->IsComparing()) {
			pComparisonManager->CompareListings();
		}
	}
}

void CMainFrame::OnDropdownComparisonHide(wxCommandEvent&)
{
	CState* pState = CContextManager::Get()->GetCurrentContext();
	if (!pState) {
		return;
	}

	bool old_mode = COptions::Get()->GetOptionVal(OPTION_COMPARE_HIDEIDENTICAL) != 0;
	COptions::Get()->SetOption(OPTION_COMPARE_HIDEIDENTICAL, old_mode ? 0 : 1);

	CComparisonManager* pComparisonManager = pState->GetComparisonManager();
	if (pComparisonManager) {
		pComparisonManager->SetHideIdentical(old_mode ? 0 : 1);
		if (pComparisonManager->IsComparing()) {
			pComparisonManager->CompareListings();
		}
	}
}

void CMainFrame::ProcessCommandLine()
{
	CCommandLine const* pCommandLine = wxGetApp().GetCommandLine();
	if (!pCommandLine) {
		return;
	}

	std::wstring local;
	if (!(local = pCommandLine->GetOption(CCommandLine::local)).empty()) {

		if (!wxDir::Exists(local)) {
			wxString str = _("Path not found:");
			str += _T("\n") + local;
			wxMessageBoxEx(str, _("Syntax error in command line"));
			return;
		}

		CState *pState = CContextManager::Get()->GetCurrentContext();
		if (pState) {
			pState->SetLocalDir(local);
		}
	}

	std::wstring site;
	if (pCommandLine->HasSwitch(CCommandLine::sitemanager)) {
		if (COptions::Get()->GetOptionVal(OPTION_STARTUP_ACTION) != 1) {
			OpenSiteManager();
		}
	}
	else if (!(site = pCommandLine->GetOption(CCommandLine::site)).empty()) {
		auto const data = CSiteManager::GetSiteByPath(site);

		if (data.first) {
			ConnectToSite(*data.first, data.second);
		}
	}

	std::wstring param = pCommandLine->GetParameter();
	if (!param.empty()) {
		std::wstring error;

		Site site;

		wxString logontype = pCommandLine->GetOption(CCommandLine::logontype);
		if (logontype == _T("ask")) {
			site.SetLogonType(LogonType::ask);
		}
		else if (logontype == _T("interactive")) {
			site.SetLogonType(LogonType::interactive);
		}

		CServerPath path;
		if (!site.ParseUrl(param, 0, std::wstring(), std::wstring(), error, path)) {
			wxString str = _("Parameter not a valid URL");
			str += _T("\n") + error;
			wxMessageBoxEx(error, _("Syntax error in command line"));
		}

		if (COptions::Get()->GetOptionVal(OPTION_DEFAULT_KIOSKMODE) && site.credentials.logonType_ == LogonType::normal) {
			site.SetLogonType(LogonType::ask);
			CLoginManager::Get().RememberPassword(site);
		}

		Bookmark bm;
		bm.m_remoteDir = path;
		ConnectToSite(site, bm);
	}
}

void CMainFrame::OnFilterRightclicked(wxCommandEvent&)
{
	const bool active = CFilterManager::HasActiveFilters();

	CFilterManager::ToggleFilters();

	if (active == CFilterManager::HasActiveFilters()) {
		return;
	}

	CContextManager::Get()->NotifyAllHandlers(STATECHANGE_APPLYFILTER);
}

#ifdef __WXMSW__
WXLRESULT CMainFrame::MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam)
{
	if (nMsg == WM_DEVICECHANGE) {
		// Let tree control handle device change message
		// They get sent by Windows on adding or removing drive
		// letters

		if (!m_pContextControl) {
			return 0;
		}

		for (int i = 0; i < m_pContextControl->GetTabCount(); ++i) {
			CContextControl::_context_controls* controls = m_pContextControl->GetControlsFromTabIndex(i);
			if (controls && controls->pLocalTreeView) {
				controls->pLocalTreeView->OnDevicechange(wParam, lParam);
			}
		}
		return 0;
	}
	else if (nMsg == WM_DISPLAYCHANGE) {
		// wxDisplay caches the display configuration and does not
		// reset it if the display configuration changes.
		// wxDisplay uses this strange factory design pattern and uses a wxModule
		// to delete the factory on program shutdown.
		//
		// To reset the factory manually in response to WM_DISPLAYCHANGE,
		// create another instance of the module and call its Exit() member.
		// After that, the next call to a wxDisplay will create a new factory and
		// get the new display layout from Windows.
		//
		// Note: Both the factory pattern as well as the dynamic object system
		//	   are perfect example of bad design.
		//
		wxModule* pDisplayModule = (wxModule*)wxCreateDynamicObject(_T("wxDisplayModule"));
		if (pDisplayModule) {
			pDisplayModule->Exit();
			delete pDisplayModule;
		}
	}
	return wxFrame::MSWWindowProc(nMsg, wParam, lParam);
}
#endif

void CMainFrame::OnSyncBrowse(wxCommandEvent&)
{
	CState* pState = CContextManager::Get()->GetCurrentContext();
	if (!pState) {
		return;
	}

	pState->SetSyncBrowse(!pState->GetSyncBrowse());
}

#ifndef __WXMAC__
void CMainFrame::OnIconize(wxIconizeEvent& event)
{
#ifdef __WXGTK__
	if (m_taskbar_is_uniconizing) {
		return;
	}
	if (m_taskBarIcon && m_taskBarIcon->IsIconInstalled()) { // Only way to uniconize is via the taskbar icon.
		return;
	}
#endif
	if (!event.IsIconized()) {
		if (m_taskBarIcon) {
			m_taskBarIcon->RemoveIcon();
		}
		Show(true);

		if (m_pAsyncRequestQueue) {
			m_pAsyncRequestQueue->TriggerProcessing();
		}

		return;
	}

	if (!COptions::Get()->GetOptionVal(OPTION_MINIMIZE_TRAY)) {
		return;
	}

	if (!m_taskBarIcon) {
		m_taskBarIcon = new wxTaskBarIcon();
		m_taskBarIcon->Connect(wxEVT_TASKBAR_LEFT_DCLICK, wxTaskBarIconEventHandler(CMainFrame::OnTaskBarClick), 0, this);
		m_taskBarIcon->Connect(wxEVT_TASKBAR_LEFT_UP, wxTaskBarIconEventHandler(CMainFrame::OnTaskBarClick), 0, this);
		m_taskBarIcon->Connect(wxEVT_TASKBAR_RIGHT_UP, wxTaskBarIconEventHandler(CMainFrame::OnTaskBarClick), 0, this);
	}

	bool installed;
	if (!m_taskBarIcon->IsIconInstalled()) {
		installed = m_taskBarIcon->SetIcon(CThemeProvider::GetIcon(_T("ART_FILEZILLA")), GetTitle());
	}
	else {
		installed = true;
	}

	if (installed) {
		Show(false);
	}
}

void CMainFrame::OnTaskBarClick(wxTaskBarIconEvent&)
{
#ifdef __WXGTK__
	if (m_taskbar_is_uniconizing)
		return;
	m_taskbar_is_uniconizing = true;
#endif

	if (m_taskBarIcon)
		m_taskBarIcon->RemoveIcon();

	Show(true);
	Iconize(false);

	if (m_pAsyncRequestQueue)
		m_pAsyncRequestQueue->TriggerProcessing();

#ifdef __WXGTK__
	QueueEvent(new wxCommandEvent(fzEVT_TASKBAR_CLICK_DELAYED));
#endif
}

#ifdef __WXGTK__
void CMainFrame::OnTaskBarClick_Delayed(wxCommandEvent&)
{
	m_taskbar_is_uniconizing = false;
}
#endif

#endif

void CMainFrame::OnSearch(wxCommandEvent&)
{
	CState* pState = CContextManager::Get()->GetCurrentContext();
	if (!pState) {
		return;
	}

	CSearchDialog dlg(this, *pState, m_pQueueView);
	if (!dlg.Load()) {
		return;
	}

	dlg.Run();
}

void CMainFrame::PostInitialize()
{
#ifdef __WXMAC__
	// Focus first control
	NavigateIn(wxNavigationKeyEvent::IsForward);
#endif

#if FZ_MANUALUPDATECHECK
	// Need to do this after welcome screen to avoid simultaneous display of multiple dialogs
	if (!m_pUpdater) {
		update_dialog_timer_.SetOwner(this);
		m_pUpdater = new CUpdater(*this, m_engineContext,
			[this](CActiveNotification const& notification) {
			UpdateActivityLed(notification.GetDirection());
		}
		);
		m_pUpdater->Init();
	}
#endif

	int const startupAction = COptions::Get()->GetOptionVal(OPTION_STARTUP_ACTION);
	bool startupReconnect = startupAction == 2;

	if (startupAction == 1) {
		OpenSiteManager();
		startupReconnect = false;
	}

	CCommandLine const* pCommandLine = wxGetApp().GetCommandLine();
	if (pCommandLine && pCommandLine->BlocksReconnectAtStartup()) {
		startupReconnect = false;
	}

	if (m_pContextControl && startupReconnect) {
		auto xml = COptions::Get()->GetOptionXml(OPTION_TAB_DATA);
		pugi::xml_node tabs = xml.child("Tabs");
		int i = 0;
		for (auto tab = tabs.child("Tab"); tab; tab = tab.next_sibling("Tab")) {
			if (tab.attribute("connected").as_int()) {
				auto controls = m_pContextControl->GetControlsFromTabIndex(i);

				if (controls && controls->pState) {
					CState* pState = controls->pState;
					if (pState->IsRemoteConnected() || !pState->IsRemoteIdle()) {
						continue;
					}

					Site site = pState->GetLastSite();
					CServerPath path = pState->GetLastServerPath();
					Bookmark bm;
					bm.m_remoteDir = path;
					if (!ConnectToSite(site, bm, pState)) {
						break;
					}
				}
			}

			++i;
		}
	}
}

void CMainFrame::OnMenuNewTab(wxCommandEvent&)
{
	if (m_pContextControl) {
		m_pContextControl->CreateTab();
	}
}

void CMainFrame::OnMenuCloseTab(wxCommandEvent&)
{
	if (!m_pContextControl) {
		return;
	}

	m_pContextControl->CloseTab(m_pContextControl->GetCurrentTab());
}

void CMainFrame::OnToggleToolBar(wxCommandEvent& event)
{
	COptions::Get()->SetOption(OPTION_TOOLBAR_HIDDEN, event.IsChecked() ? 0 : 1);
#ifdef __WXMAC__
	if (m_pToolBar) {
		m_pToolBar->Show(event.IsChecked());
	}
#else
	CreateMainToolBar();
	if (m_pToolBar) {
		m_pToolBar->UpdateToolbarState();
	}
	HandleResize();
#endif
}

void CMainFrame::FixTabOrder()
{
	if (m_pQuickconnectBar && m_pTopSplitter) {
		m_pQuickconnectBar->MoveBeforeInTabOrder(m_pTopSplitter);
	}
}

#ifdef __WXMAC__
void CMainFrame::OnChildFocused(wxChildFocusEvent& event)
{
	m_lastFocusedChild = event.GetWindow()->GetId();
}
#endif

void CMainFrame::SetupKeyboardAccelerators()
{
	std::vector<wxAcceleratorEntry> entries;
	for (int i = 0; i < 10; i++) {
		tab_hotkey_ids[i] = wxNewId();
		entries.emplace_back(wxACCEL_CMD, (int)'0' + i, tab_hotkey_ids[i]);
		entries.emplace_back(wxACCEL_ALT, (int)'0' + i, tab_hotkey_ids[i]);
	}
	entries.emplace_back(wxACCEL_CMD | wxACCEL_SHIFT, 'O', m_comparisonToggleAcceleratorId);
	entries.emplace_back(wxACCEL_CMD | wxACCEL_SHIFT, 'I', XRCID("ID_MENU_VIEW_FILTERS"));
	entries.emplace_back(wxACCEL_CMD, WXK_F5, XRCID("ID_REFRESH"));
#ifdef __WXMAC__
	entries.emplace_back(wxACCEL_CMD, ',', XRCID("wxID_PREFERENCES"));

	keyboardCommands[wxNewId()] = std::make_pair([](wxTextEntry* e) { e->Cut(); }, 'X');
	keyboardCommands[wxNewId()] = std::make_pair([](wxTextEntry* e) { e->Copy(); }, 'C');
	keyboardCommands[wxNewId()] = std::make_pair([](wxTextEntry* e) { e->Paste(); }, 'V');
	keyboardCommands[wxNewId()] = std::make_pair([](wxTextEntry* e) { e->SelectAll(); }, 'A');

	for (auto const& command : keyboardCommands) {
		entries.emplace_back(wxACCEL_CMD, command.second.second, command.first);
	}

	// Ctrl+(Shift+)Tab to switch between tabs
	int id = wxNewId();
	entries.emplace_back(wxACCEL_RAW_CTRL, '\t', id);
	Bind(wxEVT_MENU, [this](wxEvent&) { if (m_pContextControl) { m_pContextControl->AdvanceTab(true); } }, id);
	id = wxNewId();
	entries.emplace_back(wxACCEL_RAW_CTRL | wxACCEL_SHIFT, '\t', id);
	Bind(wxEVT_MENU, [this](wxEvent&) { if (m_pContextControl) { m_pContextControl->AdvanceTab(false); } }, id);
#endif
	wxAcceleratorTable accel(entries.size(), &entries[0]);
	SetAcceleratorTable(accel);
}

void CMainFrame::OnOptionsChanged(changed_options_t const& options)
{
	if (options.test(OPTION_FILEPANE_LAYOUT) || options.test(OPTION_FILEPANE_SWAP) || options.test(OPTION_MESSAGELOG_POSITION)) {
		UpdateLayout();
	}

	if (options.test(OPTION_ICONS_THEME) || options.test(OPTION_ICONS_SCALE)) {
		CreateMainToolBar();
		if (m_pToolBar) {
			m_pToolBar->UpdateToolbarState();
		}
	}
}
