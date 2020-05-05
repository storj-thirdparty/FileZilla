#include <filezilla.h>
#include "settingsdialog.h"
#include "../Options.h"
#include "optionspage.h"
#include "optionspage_connection.h"
#include "optionspage_connection_ftp.h"
#include "optionspage_connection_active.h"
#include "optionspage_connection_passive.h"
#include "optionspage_ftpproxy.h"
#include "optionspage_connection_sftp.h"
#include "optionspage_filetype.h"
#include "optionspage_fileexists.h"
#include "optionspage_themes.h"
#include "optionspage_language.h"
#include "optionspage_transfer.h"
#include "optionspage_updatecheck.h"
#include "optionspage_logging.h"
#include "optionspage_debug.h"
#include "optionspage_interface.h"
#include "optionspage_dateformatting.h"
#include "optionspage_sizeformatting.h"
#include "optionspage_edit.h"
#include "optionspage_edit_associations.h"
#include "optionspage_passwords.h"
#include "optionspage_proxy.h"
#include "optionspage_filelists.h"
#include "../filezillaapp.h"
#include "../Mainfrm.h"
#include "../treectrlex.h"
#include "../xrc_helper.h"

BEGIN_EVENT_TABLE(CSettingsDialog, wxDialogEx)
EVT_TREE_SEL_CHANGING(XRCID("ID_TREE"), CSettingsDialog::OnPageChanging)
EVT_TREE_SEL_CHANGED(XRCID("ID_TREE"), CSettingsDialog::OnPageChanged)
EVT_BUTTON(XRCID("wxID_OK"), CSettingsDialog::OnOK)
EVT_BUTTON(XRCID("wxID_CANCEL"), CSettingsDialog::OnCancel)
END_EVENT_TABLE()

CSettingsDialog::CSettingsDialog(CFileZillaEngineContext & engine_context)
	: m_engine_context(engine_context)
{
	m_pOptions = COptions::Get();
}

CSettingsDialog::~CSettingsDialog()
{
}

bool CSettingsDialog::Create(CMainFrame* pMainFrame)
{
	m_pMainFrame = pMainFrame;

	SetExtraStyle(wxWS_EX_BLOCK_EVENTS);
	if (!wxDialogEx::Create(pMainFrame, -1, _("Settings"))) {
		return false;
	}

	auto & lay = layout();
	auto * main = lay.createMain(this, 2);
	main->AddGrowableRow(0);

	auto* left = lay.createFlex(1);
	left->AddGrowableRow(1);
	main->Add(left, 1, wxGROW);

	left->Add(new wxStaticText(this, -1, _("Select &page:")));

	tree_ = new wxTreeCtrlEx(this, XRCID("ID_TREE"), wxDefaultPosition, wxDefaultSize, DEFAULT_TREE_STYLE | wxTR_HIDE_ROOT);
	tree_->SetFocus();
	left->Add(tree_, 1, wxGROW);

	auto ok = new wxButton(this, wxID_OK, _("OK"));
	ok->SetDefault();
	left->Add(ok, lay.grow);
	left->Add(new wxButton(this, wxID_CANCEL, _("&Cancel")), lay.grow);

	pagePanel_ = new wxPanel(this);
	main->Add(pagePanel_, lay.grow);

	if (!LoadPages()) {
		return false;
	}

	return true;
}

void CSettingsDialog::AddPage(wxString const& name, COptionsPage* page, int nest)
{
	wxTreeItemId parent = tree_->GetRootItem();
	while (nest--) {
		parent = tree_->GetLastChild(parent);
		wxCHECK_RET(parent != wxTreeItemId(), "Nesting level too deep");
	}

	t_page p;
	p.page = page;
	p.id = tree_->AppendItem(parent, name);
	if (parent != tree_->GetRootItem()) {
		tree_->Expand(parent);
	}

	m_pages.push_back(p);
}

bool CSettingsDialog::LoadPages()
{
	InitXrc(L"settings.xrc");

	// Get the tree control.

	if (!tree_) {
		return false;
	}

	tree_->AddRoot(wxString());

	// Create the instances of the page classes and fill the tree.
	AddPage(_("Connection"), new COptionsPageConnection, 0);
	AddPage(_("FTP"), new COptionsPageConnectionFTP, 1);
	AddPage(_("Active mode"), new COptionsPageConnectionActive, 2);
	AddPage(_("Passive mode"), new COptionsPageConnectionPassive, 2);
	AddPage(_("FTP Proxy"), new COptionsPageFtpProxy, 2);
	AddPage(_("SFTP"), new COptionsPageConnectionSFTP, 1);
	AddPage(_("Generic proxy"), new COptionsPageProxy, 1);
	AddPage(_("Transfers"), new COptionsPageTransfer, 0);
	AddPage(_("FTP: File Types"), new COptionsPageFiletype, 1);
	AddPage(_("File exists action"), new COptionsPageFileExists, 1);
	AddPage(_("Interface"), new COptionsPageInterface, 0);
	AddPage(_("Passwords"), new COptionsPagePasswords, 1);
	AddPage(_("Themes"), new COptionsPageThemes, 1);
	AddPage(_("Date/time format"), new COptionsPageDateFormatting, 1);
	AddPage(_("Filesize format"), new COptionsPageSizeFormatting, 1);
	AddPage(_("File lists"), new COptionsPageFilelists, 1);
	AddPage(_("Language"), new COptionsPageLanguage, 0);
	AddPage(_("File editing"), new COptionsPageEdit, 0);
	AddPage(_("Filetype associations"), new COptionsPageEditAssociations, 1);
#if FZ_MANUALUPDATECHECK && FZ_AUTOUPDATECHECK
	if (!m_pOptions->GetOptionVal(OPTION_DEFAULT_DISABLEUPDATECHECK)) {
		AddPage(_("Updates"), new COptionsPageUpdateCheck, 0);
	}
#endif //FZ_MANUALUPDATECHECK && FZ_AUTOUPDATECHECK
	AddPage(_("Logging"), new COptionsPageLogging, 0);
	AddPage(_("Debug"), new COptionsPageDebug, 0);

	tree_->SetQuickBestSize(false);
	tree_->InvalidateBestSize();
	tree_->SetInitialSize();

	// Compensate for scrollbar
	wxSize size = tree_->GetBestSize();
	int scrollWidth = wxSystemSettings::GetMetric(wxSYS_VSCROLL_X, tree_);
	size.x += scrollWidth + 2;
	size.y = 400;
	tree_->SetInitialSize(size);
	Layout();

	// Before we can initialize the pages, get the target panel in the settings
	// dialog.
	if (!pagePanel_) {
		return false;
	}

	// Keep track of maximum page size
	size = wxSize();

	for (auto const& page : m_pages) {
		if (!page.page->CreatePage(m_pOptions, this, pagePanel_, size)) {
			return false;
		}
	}

	if (!LoadSettings()) {
		wxMessageBoxEx(_("Failed to load panels, invalid resource files?"));
		return false;
	}

	wxSize canvas;
	canvas.x = GetSize().x - pagePanel_->GetSize().x;
	canvas.y = GetSize().y - pagePanel_->GetSize().y;

	// Wrap pages nicely
	std::vector<wxWindow*> pages;
	for (auto const& page : m_pages) {
		pages.push_back(page.page);
	}
	wxGetApp().GetWrapEngine()->WrapRecursive(pages, 1.33, "Settings", canvas);

#ifdef __WXGTK__
	// Pre-show dialog under GTK, else panels won't get initialized properly
	Show();
#endif

	// Keep track of maximum page size
	size = wxSize(0, 0);
	for (auto const& page : m_pages) {
		auto sizer = page.page->GetSizer();
		if (sizer) {
			size.IncTo(sizer->GetMinSize());
		}
	}

	wxSize panelSize = size;
#ifdef __WXGTK__
	panelSize.x += 1;
#endif
	pagePanel_->SetInitialSize(panelSize);

	// Adjust pages sizes according to maximum size
	for (auto const& page : m_pages) {
		auto sizer = page.page->GetSizer();
		if (sizer) {
			sizer->SetMinSize(size);
			sizer->Fit(page.page);
			sizer->SetSizeHints(page.page);
		}
		if (GetLayoutDirection() == wxLayout_RightToLeft) {
			page.page->Move(wxPoint(1, 0));
		}
	}

	GetSizer()->Fit(this);
	GetSizer()->SetSizeHints(this);

	for (auto const& page : m_pages) {
		page.page->Hide();
	}

	// Select first page
	tree_->SelectItem(m_pages[0].id);
	if (!m_activePanel)	{
		m_activePanel = m_pages[0].page;
		m_activePanel->Display();
	}

	return true;
}

bool CSettingsDialog::LoadSettings()
{
	for (auto const& page : m_pages) {
		if (!page.page->LoadPage()) {
			return false;
		}
	}

	return true;
}

void CSettingsDialog::OnPageChanged(wxTreeEvent& event)
{
	if (m_activePanel) {
		m_activePanel->Hide();
	}

	wxTreeItemId item = event.GetItem();

	for (auto const& page : m_pages) {
		if (page.id == item) {
			m_activePanel = page.page;
			m_activePanel->Display();
			break;
		}
	}
}

void CSettingsDialog::OnOK(wxCommandEvent&)
{
	for (auto const& page : m_pages) {
		if (!page.page->Validate()) {
			if (m_activePanel != page.page) {
				tree_->SelectItem(page.id);
			}
			return;
		}
	}

	for (auto const& page : m_pages) {
		page.page->SavePage();
	}

	EndModal(wxID_OK);
}

void CSettingsDialog::OnCancel(wxCommandEvent&)
{
	EndModal(wxID_CANCEL);

	for (auto const& saved : m_oldValues) {
		m_pOptions->SetOption(saved.first, saved.second);
	}
}

void CSettingsDialog::OnPageChanging(wxTreeEvent& event)
{
	if (!m_activePanel) {
		return;
	}

	if (!m_activePanel->Validate()) {
		event.Veto();
	}
}

void CSettingsDialog::RememberOldValue(int option)
{
	m_oldValues[option] = m_pOptions->GetOption(option);
}
