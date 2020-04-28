#include <filezilla.h>
#include "sitemanager_site.h"

#include "filezillaapp.h"
#include "fzputtygen_interface.h"
#include "Options.h"
#if USE_MAC_SANDBOX
#include "osx_sandbox_userdirs.h"
#endif
#include "sitemanager_dialog.h"
#if ENABLE_STORJ
#include "storj_key_interface.h"
#endif
#include "xrc_helper.h"

#include <s3sse.h>

#include <wx/dcclient.h>
#include <wx/gbsizer.h>
#include <wx/hyperlink.h>
#include <wx/statline.h>

#ifdef __WXMSW__
#include "commctrl.h"
#endif

#include <array>

BEGIN_EVENT_TABLE(CSiteManagerSite, wxNotebook)
EVT_CHOICE(XRCID("ID_PROTOCOL"), CSiteManagerSite::OnProtocolSelChanged)
EVT_CHOICE(XRCID("ID_LOGONTYPE"), CSiteManagerSite::OnLogontypeSelChanged)
EVT_RADIOBUTTON(XRCID("ID_CHARSET_AUTO"), CSiteManagerSite::OnCharsetChange)
EVT_RADIOBUTTON(XRCID("ID_CHARSET_UTF8"), CSiteManagerSite::OnCharsetChange)
EVT_RADIOBUTTON(XRCID("ID_CHARSET_CUSTOM"), CSiteManagerSite::OnCharsetChange)
EVT_CHECKBOX(XRCID("ID_LIMITMULTIPLE"), CSiteManagerSite::OnLimitMultipleConnectionsChanged)
EVT_BUTTON(XRCID("ID_BROWSE"), CSiteManagerSite::OnRemoteDirBrowse)
EVT_BUTTON(XRCID("ID_KEYFILE_BROWSE"), CSiteManagerSite::OnKeyFileBrowse)
END_EVENT_TABLE()

namespace {
std::array<ServerProtocol, 4> const ftpSubOptions{ FTP, FTPES, FTPS, INSECURE_FTP };
}

CSiteManagerSite::CSiteManagerSite(CSiteManagerDialog &sitemanager)
    : sitemanager_(sitemanager)
{
}

bool CSiteManagerSite::Load(wxWindow* parent)
{
	Create(parent, -1);

	DialogLayout lay(static_cast<wxTopLevelWindow*>(wxGetTopLevelParent(parent)));

	{
		wxPanel* generalPage = new wxPanel(this);
		AddPage(generalPage, _("General"));

		auto* main = lay.createMain(generalPage, 1);
		main->AddGrowableCol(0);

		auto * bag = lay.createGridBag(2);
		bag->AddGrowableCol(1);
		main->Add(bag, 0, wxGROW);

		lay.gbNewRow(bag);
		lay.gbAdd(bag, new wxStaticText(generalPage, -1, _("Pro&tocol:")), lay.valign);
		lay.gbAdd(bag, new wxChoice(generalPage, XRCID("ID_PROTOCOL")), lay.valigng);
		
		lay.gbNewRow(bag);
		lay.gbAdd(bag, new wxStaticText(generalPage, XRCID("ID_HOST_DESC"), _("&Host:")), lay.valign);

		auto * row = lay.createFlex(0, 1);
		row = lay.createFlex(0, 1);

		row->AddGrowableCol(0);
		lay.gbAdd(bag, row, lay.valigng);
		row->Add(new wxTextCtrl(generalPage, XRCID("ID_HOST")), lay.valigng);

		row->Add(new wxStaticText(generalPage, XRCID("ID_PORT_DESC"), _("&Port:")), lay.valign);

		auto* port = new wxTextCtrl(generalPage, XRCID("ID_PORT"), wxString(), wxDefaultPosition, wxSize(lay.dlgUnits(27), -1));
		port->SetMaxLength(5);
		row->Add(port, lay.valign);

		lay.gbNewRow(bag);
		lay.gbAdd(bag, new wxStaticText(generalPage, XRCID("ID_ENCRYPTION_DESC"), _("&Encryption:")), lay.valign);
		auto brow = new wxBoxSizer(wxHORIZONTAL);
		lay.gbAdd(bag, brow, lay.valigng);
		brow->Add(new wxChoice(generalPage, XRCID("ID_ENCRYPTION")), 1);
		brow->Add(new wxHyperlinkCtrl(generalPage, XRCID("ID_DOCS"), _("Docs"), L"https://github.com/storj/storj/wiki/Vanguard-Release-Setup-Instructions"), lay.valign)->Show(false);
		brow->AddSpacer(5);
		brow->Add(new wxHyperlinkCtrl(generalPage, XRCID("ID_SIGNUP"), _("Signup"), L"https://app.storj.io/#/signup"), lay.valign)->Show(false);
		brow->AddSpacer(0);

		lay.gbNewRow(bag);
		lay.gbAdd(bag, new wxStaticText(generalPage, XRCID("ID_EXTRA_HOST_DESC"), L""), lay.valign)->Show(false);
		lay.gbAdd(bag, new wxTextCtrl(generalPage, XRCID("ID_EXTRA_HOST")), lay.valigng)->Show(false);

		lay.gbAddRow(bag, new wxStaticLine(generalPage), lay.grow);

		lay.gbNewRow(bag);
		
		lay.gbAdd(bag, new wxStaticText(generalPage, -1, _("&Logon Type:")), lay.valign);
		lay.gbAdd(bag, new wxChoice(generalPage, XRCID("ID_LOGONTYPE")), lay.valigng);

		lay.gbNewRow(bag);
		lay.gbAdd(bag, new wxStaticText(generalPage, XRCID("ID_USER_DESC"), _("&User:")), lay.valign);
		lay.gbAdd(bag, new wxTextCtrl(generalPage, XRCID("ID_USER")), lay.valigng);

		lay.gbNewRow(bag);
		lay.gbAdd(bag, new wxStaticText(generalPage, XRCID("ID_EXTRA_USER_DESC"), L""), lay.valign)->Show(false);
		lay.gbAdd(bag, new wxTextCtrl(generalPage, XRCID("ID_EXTRA_USER")), lay.valigng)->Show(false);

		lay.gbNewRow(bag);
		lay.gbAdd(bag, new wxStaticText(generalPage, XRCID("ID_PASS_DESC"), _("Pass&word:")), lay.valign);
		lay.gbAdd(bag, new wxTextCtrl(generalPage, XRCID("ID_PASS"), L"", wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD), lay.valigng);

		lay.gbNewRow(bag);
		lay.gbAdd(bag, new wxStaticText(generalPage, XRCID("ID_ACCOUNT_DESC"), _("&Account:")), lay.valign);
		lay.gbAdd(bag, new wxTextCtrl(generalPage, XRCID("ID_ACCOUNT")), lay.valigng);

		lay.gbNewRow(bag);
		lay.gbAdd(bag, new wxStaticText(generalPage, XRCID("ID_KEYFILE_DESC"), _("&Key file:")), lay.valign)->Show(false);
		row = lay.createFlex(0, 1);
		row->AddGrowableCol(0);
		lay.gbAdd(bag, row, lay.valigng);
		row->Add(new wxTextCtrl(generalPage, XRCID("ID_KEYFILE")), lay.valigng)->Show(false);
		row->Add(new wxButton(generalPage, XRCID("ID_KEYFILE_BROWSE"), _("Browse...")), lay.valign)->Show(false);

		lay.gbNewRow(bag);
		lay.gbAdd(bag, new wxStaticText(generalPage, XRCID("ID_EXTRA_CREDENTIALS_DESC"), L""), lay.valign)->Show(false);
		lay.gbAdd(bag, new wxTextCtrl(generalPage, XRCID("ID_EXTRA_CREDENTIALS"), L"", wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD), lay.valigng)->Show(false);

		lay.gbNewRow(bag);
		lay.gbAdd(bag, new wxStaticText(generalPage, XRCID("ID_EXTRA_EXTRA_DESC"), L""), lay.valign)->Show(false);
		lay.gbAdd(bag, new wxTextCtrl(generalPage, XRCID("ID_EXTRA_EXTRA")), lay.valigng)->Show(false);

		main->Add(new wxStaticLine(generalPage), lay.grow);
	
		row = lay.createFlex(0, 1);
		main->Add(row);
		row->Add(new wxStaticText(generalPage, -1, _("&Background color:")), lay.valign);
		row->Add(new wxChoice(generalPage, XRCID("ID_COLOR")), lay.valign);

		main->Add(new wxStaticText(generalPage, -1, _("Co&mments:")));
		main->Add(new wxTextCtrl(generalPage, XRCID("ID_COMMENTS"), L"", wxDefaultPosition, wxSize(-1, lay.dlgUnits(43)), wxTE_MULTILINE), 1, wxGROW);
		main->AddGrowableRow(main->GetEffectiveRowsCount() - 1);
	}

	{
		wxPanel* advancedPage = new wxPanel(this);
		AddPage(advancedPage, _("Advanced"));

		auto * main = lay.createMain(advancedPage, 1);
		main->AddGrowableCol(0);
		auto* row = lay.createFlex(0, 1);
		main->Add(row);

		row->Add(new wxStaticText(advancedPage, XRCID("ID_SERVERTYPE_LABEL"), _("Server &type:")), lay.valign);
		row->Add(new wxChoice(advancedPage, XRCID("ID_SERVERTYPE")), lay.valign);
		main->AddSpacer(0);
		main->Add(new wxCheckBox(advancedPage, XRCID("ID_BYPASSPROXY"), _("B&ypass proxy")));

		main->Add(new wxStaticLine(advancedPage), lay.grow);

		main->Add(new wxStaticText(advancedPage, -1, _("Default &local directory:")));

		row = lay.createFlex(0, 1);
		main->Add(row, lay.grow);
		row->AddGrowableCol(0);
		row->Add(new wxTextCtrl(advancedPage, XRCID("ID_LOCALDIR")), lay.valigng);
		row->Add(new wxButton(advancedPage, XRCID("ID_BROWSE"), _("&Browse...")), lay.valign);
		main->AddSpacer(0);
		main->Add(new wxStaticText(advancedPage, -1, _("Default r&emote directory:")));
		main->Add(new wxTextCtrl(advancedPage, XRCID("ID_REMOTEDIR")), lay.grow);
		main->AddSpacer(0);
		main->Add(new wxCheckBox(advancedPage, XRCID("ID_SYNC"), _("&Use synchronized browsing")));
		main->Add(new wxCheckBox(advancedPage, XRCID("ID_COMPARISON"), _("Directory comparison")));

		main->Add(new wxStaticLine(advancedPage), lay.grow);

		main->Add(new wxStaticText(advancedPage, -1, _("&Adjust server time, offset by:")));
		row = lay.createFlex(0, 1);
		main->Add(row);
		auto* hours = new wxSpinCtrl(advancedPage, XRCID("ID_TIMEZONE_HOURS"), wxString(), wxDefaultPosition, wxSize(lay.dlgUnits(26), -1));
		hours->SetRange(-24, 24);
		row->Add(hours, lay.valign);
		row->Add(new wxStaticText(advancedPage, -1, _("Hours,")), lay.valign);
		auto* minutes = new wxSpinCtrl(advancedPage, XRCID("ID_TIMEZONE_MINUTES"), wxString(), wxDefaultPosition, wxSize(lay.dlgUnits(26), -1));
		minutes->SetRange(-59, 59);
		row->Add(minutes, lay.valign);
		row->Add(new wxStaticText(advancedPage, -1, _("Minutes")), lay.valign);
	}

	{
		wxPanel* transferPage = new wxPanel(this);
		AddPage(transferPage, _("Transfer Settings"));

		auto * main = lay.createMain(transferPage, 1);
		main->Add(new wxStaticText(transferPage, XRCID("ID_TRANSFERMODE_LABEL"), _("&Transfer mode:")));
		auto * row = lay.createFlex(0, 1);
		main->Add(row);
		row->Add(new wxRadioButton(transferPage, XRCID("ID_TRANSFERMODE_DEFAULT"), _("D&efault"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP), lay.valign);
		row->Add(new wxRadioButton(transferPage, XRCID("ID_TRANSFERMODE_ACTIVE"), _("&Active")), lay.valign);
		row->Add(new wxRadioButton(transferPage, XRCID("ID_TRANSFERMODE_PASSIVE"), _("&Passive")), lay.valign);
		main->AddSpacer(0);

		main->Add(new wxCheckBox(transferPage, XRCID("ID_LIMITMULTIPLE"), _("&Limit number of simultaneous connections")));
		row = lay.createFlex(0, 1);
		main->Add(row, 0, wxLEFT, lay.dlgUnits(10));
		row->Add(new wxStaticText(transferPage, -1, _("&Maximum number of connections:")), lay.valign);
		auto * spin = new wxSpinCtrl(transferPage, XRCID("ID_MAXMULTIPLE"), wxString(), wxDefaultPosition, wxSize(lay.dlgUnits(26), -1));
		spin->SetRange(1, 10);
		row->Add(spin, lay.valign);
	}

	{
		m_pCharsetPage = new wxPanel(this);
		AddPage(m_pCharsetPage, _("Charset"));

		auto * main = lay.createMain(m_pCharsetPage, 1);
		main->Add(new wxStaticText(m_pCharsetPage, -1, _("The server uses following charset encoding for filenames:")));
		main->Add(new wxRadioButton(m_pCharsetPage, XRCID("ID_CHARSET_AUTO"), _("&Autodetect"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP));
		main->Add(new wxStaticText(m_pCharsetPage, -1, _("Uses UTF-8 if the server supports it, else uses local charset.")), 0, wxLEFT, 18);
		main->Add(new wxRadioButton(m_pCharsetPage, XRCID("ID_CHARSET_UTF8"), _("Force &UTF-8")));
		main->Add(new wxRadioButton(m_pCharsetPage, XRCID("ID_CHARSET_CUSTOM"), _("Use &custom charset")));
		auto * row = lay.createFlex(0, 1);
		row->Add(new wxStaticText(m_pCharsetPage, -1, _("&Encoding:")), lay.valign);
		row->Add(new wxTextCtrl(m_pCharsetPage, XRCID("ID_ENCODING")), 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 18);
		main->Add(row);
		main->AddSpacer(lay.dlgUnits(6));
		main->Add(new wxStaticText(m_pCharsetPage, -1, _("Using the wrong charset can result in filenames not displaying properly.")));
	}

	{
		m_pS3Page = new wxPanel(this);
		m_pS3Page->Hide();

		auto * main = lay.createMain(m_pS3Page, 1);
		main->AddGrowableCol(0);
		main->Add(new wxStaticText(m_pS3Page, -1, _("Server Side Encryption:")));

		main->Add(new wxRadioButton(m_pS3Page, XRCID("ID_S3_NOENCRYPTION"), _("N&o encryption"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP));

		main->Add(new wxRadioButton(m_pS3Page, XRCID("ID_S3_AES256"), _("&AWS S3 encryption")));

		main->Add(new wxRadioButton(m_pS3Page, XRCID("ID_S3_AWSKMS"), _("AWS &KMS encryption")));
		auto * row = lay.createFlex(2);
		row->AddGrowableCol(1);
		main->Add(row, 0, wxLEFT|wxGROW, lay.dlgUnits(10));
		row->Add(new wxStaticText(m_pS3Page, -1, _("&Select a key:")), lay.valign);
		auto * choice = new wxChoice(m_pS3Page, XRCID("ID_S3_KMSKEY"));
		choice->Append(_("Default (AWS/S3)"));
		choice->Append(_("Custom KMS ARN"));
		row->Add(choice, lay.valigng);
		row->Add(new wxStaticText(m_pS3Page, -1, _("C&ustom KMS ARN:")), lay.valign);
		row->Add(new wxTextCtrl(m_pS3Page, XRCID("ID_S3_CUSTOM_KMS")), lay.valigng);

		main->Add(new wxRadioButton(m_pS3Page, XRCID("ID_S3_CUSTOMER_ENCRYPTION"), _("Cu&stomer encryption")));
		row = lay.createFlex(2);
		row->AddGrowableCol(1);
		main->Add(row, 0, wxLEFT | wxGROW, lay.dlgUnits(10));
		row->Add(new wxStaticText(m_pS3Page, -1, _("Cus&tomer Key:")), lay.valign);
		row->Add(new wxTextCtrl(m_pS3Page, XRCID("ID_S3_CUSTOMER_KEY")), lay.valigng);
	}

	extraParameters_[ParameterSection::host].emplace_back(XRCCTRL(*this, "ID_EXTRA_HOST_DESC", wxStaticText), XRCCTRL(*this, "ID_EXTRA_HOST", wxTextCtrl));
	extraParameters_[ParameterSection::user].emplace_back(XRCCTRL(*this, "ID_EXTRA_USER_DESC", wxStaticText), XRCCTRL(*this, "ID_EXTRA_USER", wxTextCtrl));
	extraParameters_[ParameterSection::credentials].emplace_back(XRCCTRL(*this, "ID_EXTRA_CREDENTIALS_DESC", wxStaticText), XRCCTRL(*this, "ID_EXTRA_CREDENTIALS", wxTextCtrl));
	extraParameters_[ParameterSection::extra].emplace_back(XRCCTRL(*this, "ID_EXTRA_EXTRA_DESC", wxStaticText), XRCCTRL(*this, "ID_EXTRA_EXTRA", wxTextCtrl));

	InitProtocols();

	m_totalPages = GetPageCount();

	int const charsetPageIndex = FindPage(m_pCharsetPage);
	m_charsetPageText = GetPageText(charsetPageIndex);
	wxGetApp().GetWrapEngine()->WrapRecursive(m_pCharsetPage, 1.3);

	auto generalSizer = static_cast<wxGridBagSizer*>(xrc_call(*this, "ID_PROTOCOL", &wxWindow::GetContainingSizer));
	generalSizer->SetEmptyCellSize(wxSize(-generalSizer->GetHGap(), -generalSizer->GetVGap()));

	GetPage(0)->GetSizer()->Fit(GetPage(0));

#ifdef __WXMSW__
	// Make pages at least wide enough to fit all tabs
	HWND hWnd = (HWND)GetHandle();

	int width = 4;
	for (unsigned int i = 0; i < GetPageCount(); ++i) {
		RECT tab_rect{};
		if (TabCtrl_GetItemRect(hWnd, i, &tab_rect)) {
			width += tab_rect.right - tab_rect.left;
		}
	}
#else
	// Make pages at least wide enough to fit all tabs
	int width = 10; // Guessed
	wxClientDC dc(this);
	for (unsigned int i = 0; i < GetPageCount(); ++i) {
		wxCoord w, h;
		dc.GetTextExtent(GetPageText(i), &w, &h);

		width += w;
#ifdef __WXMAC__
		width += 20; // Guessed
#else
		width += 20;
#endif
	}
#endif

	wxSize const descSize = XRCCTRL(*this, "ID_ENCRYPTION_DESC", wxWindow)->GetSize();
	wxSize const encSize = XRCCTRL(*this, "ID_ENCRYPTION", wxWindow)->GetSize();
	
	int dataWidth = std::max(encSize.GetWidth(), XRCCTRL(*this, "ID_PROTOCOL", wxWindow)->GetSize().GetWidth());

	width = std::max(width, static_cast<int>(descSize.GetWidth() * 2 + dataWidth + generalSizer->GetHGap() * 3));
	
	wxSize page_min_size = GetPage(0)->GetSizer()->GetMinSize();
	if (page_min_size.x < width) {
		page_min_size.x = width;
		GetPage(0)->GetSizer()->SetMinSize(page_min_size);
	}

	// Set min height of general page sizer
	generalSizer->SetMinSize(generalSizer->GetMinSize());

	// Set min height of encryption row
	auto encSizer = xrc_call(*this, "ID_ENCRYPTION", &wxWindow::GetContainingSizer);
	encSizer->GetItem(encSizer->GetItemCount() - 1)->SetMinSize(0, std::max(descSize.GetHeight(), encSize.GetHeight()));
	
	return true;
}

void CSiteManagerSite::InitProtocols()
{
	wxChoice *pProtocol = XRCCTRL(*this, "ID_PROTOCOL", wxChoice);
	if (!pProtocol) {
		return;
	}

	mainProtocolListIndex_[FTP] = pProtocol->Append(_("FTP - File Transfer Protocol"));
	for (auto const& proto : CServer::GetDefaultProtocols()) {
		if (std::find(ftpSubOptions.cbegin(), ftpSubOptions.cend(), proto) == ftpSubOptions.cend()) {
			mainProtocolListIndex_[proto] = pProtocol->Append(CServer::GetProtocolName(proto));
		}
		else {
			mainProtocolListIndex_[proto] = mainProtocolListIndex_[FTP];
		}
	}

	wxChoice *pChoice = XRCCTRL(*this, "ID_SERVERTYPE", wxChoice);
	wxASSERT(pChoice);
	for (int i = 0; i < SERVERTYPE_MAX; ++i) {
		pChoice->Append(CServer::GetNameFromServerType(static_cast<ServerType>(i)));
	}

	pChoice = XRCCTRL(*this, "ID_LOGONTYPE", wxChoice);
	wxASSERT(pChoice);
	for (int i = 0; i < static_cast<int>(LogonType::count); ++i) {
		pChoice->Append(GetNameFromLogonType(static_cast<LogonType>(i)));
	}

	wxChoice* pEncryption = XRCCTRL(*this, "ID_ENCRYPTION", wxChoice);

	// Order must match ftpSubOptions
	pEncryption->Append(_("Use explicit FTP over TLS if available"));
	pEncryption->Append(_("Require explicit FTP over TLS"));
	pEncryption->Append(_("Require implicit FTP over TLS"));
	pEncryption->Append(_("Only use plain FTP (insecure)"));
	pEncryption->SetSelection(0);
	
	wxChoice* pColors = XRCCTRL(*this, "ID_COLOR", wxChoice);
	if (pColors) {
		for (int i = 0; ; ++i) {
			wxString name = CSiteManager::GetColourName(i);
			if (name.empty()) {
				break;
			}
			pColors->AppendString(wxGetTranslation(name));
		}
	}
}

void CSiteManagerSite::SetProtocol(ServerProtocol protocol)
{
	wxChoice* pProtocol = XRCCTRL(*this, "ID_PROTOCOL", wxChoice);
	wxChoice* pEncryption = XRCCTRL(*this, "ID_ENCRYPTION", wxChoice);
	wxStaticText* pEncryptionDesc = XRCCTRL(*this, "ID_ENCRYPTION_DESC", wxStaticText);
	
	auto const it = std::find(ftpSubOptions.cbegin(), ftpSubOptions.cend(), protocol);

	if (it != ftpSubOptions.cend()) {
		pEncryption->SetSelection(it - ftpSubOptions.cbegin());
		pEncryption->Show();
		pEncryptionDesc->Show();
	}
	else {
		pEncryption->Hide();
		pEncryptionDesc->Hide();
	}
	auto const protoIt = mainProtocolListIndex_.find(protocol);
	if (protoIt != mainProtocolListIndex_.cend()) {
		pProtocol->SetSelection(protoIt->second);
	}
	else if (protocol != ServerProtocol::UNKNOWN) {
		mainProtocolListIndex_[protocol] = pProtocol->Append(CServer::GetProtocolName(protocol));
		pProtocol->SetSelection(mainProtocolListIndex_[protocol]);
	}
	else {
		pProtocol->SetSelection(mainProtocolListIndex_[FTP]);
	}
	UpdateHostFromDefaults(GetProtocol());

	previousProtocol_ = protocol;
}

ServerProtocol CSiteManagerSite::GetProtocol() const
{
	int const sel = xrc_call(*this, "ID_PROTOCOL", &wxChoice::GetSelection);
	if (sel == mainProtocolListIndex_.at(FTP)) {
		int encSel = xrc_call(*this, "ID_ENCRYPTION", &wxChoice::GetSelection);
		if (encSel >= 0 && encSel < static_cast<int>(ftpSubOptions.size())) {
			return ftpSubOptions[encSel];
		}
		
		return FTP;
	}
	else {
		for (auto const it : mainProtocolListIndex_) {
			if (it.second == sel) {
				return it.first;
			}
		}
	}

	return UNKNOWN;
}

void CSiteManagerSite::SetControlVisibility(ServerProtocol protocol, LogonType type)
{
	bool const isFtp = std::find(ftpSubOptions.cbegin(), ftpSubOptions.cend(), protocol) != ftpSubOptions.cend();

	xrc_call(*this, "ID_ENCRYPTION_DESC", &wxStaticText::Show, isFtp);
	xrc_call(*this, "ID_ENCRYPTION", &wxChoice::Show, isFtp);
	
	xrc_call(*this, "ID_DOCS", &wxControl::Show, protocol == STORJ);
	xrc_call(*this, "ID_SIGNUP", &wxControl::Show, protocol == STORJ);

	xrc_call(*this, "ID_PORT_DESC", &wxStaticText::Show, !(protocol == STORJ && type == LogonType::anonymous));
	xrc_call(*this, "ID_PORT", &wxControl::Show, !(protocol == STORJ && type == LogonType::anonymous));

	auto const supportedlogonTypes = GetSupportedLogonTypes(protocol);
	assert(!supportedlogonTypes.empty());

	auto choice = XRCCTRL(*this, "ID_LOGONTYPE", wxChoice);
	choice->Clear();

	if (std::find(supportedlogonTypes.cbegin(), supportedlogonTypes.cend(), type) == supportedlogonTypes.cend()) {
		type = supportedlogonTypes.front();
	}

	for (auto const supportedLogonType : supportedlogonTypes) {
		if (protocol == STORJ && (GetNameFromLogonType(supportedLogonType) == "Anonymous"))
			choice->Append("Serialized Key");
		else {
			choice->Append(GetNameFromLogonType(supportedLogonType));
		}

		if (supportedLogonType == type) {
			choice->SetSelection(choice->GetCount() - 1);
		}
	}

	bool const hasUser = ProtocolHasUser(protocol) && type != LogonType::anonymous;

	xrc_call(*this, "ID_USER_DESC", &wxStaticText::Show, hasUser);
	xrc_call(*this, "ID_USER", &wxTextCtrl::Show, hasUser);
	xrc_call(*this, "ID_PASS_DESC", &wxStaticText::Show, type != LogonType::anonymous && type != LogonType::interactive  && (protocol != SFTP || type != LogonType::key));
	xrc_call(*this, "ID_PASS", &wxTextCtrl::Show, type != LogonType::anonymous && type != LogonType::interactive && (protocol != SFTP || type != LogonType::key));
	xrc_call(*this, "ID_ACCOUNT_DESC", &wxStaticText::Show, isFtp && type == LogonType::account);
	xrc_call(*this, "ID_ACCOUNT", &wxTextCtrl::Show, isFtp && type == LogonType::account);
	xrc_call(*this, "ID_KEYFILE_DESC", &wxStaticText::Show, protocol == SFTP && type == LogonType::key);
	xrc_call(*this, "ID_KEYFILE", &wxTextCtrl::Show, protocol == SFTP && type == LogonType::key);
	xrc_call(*this, "ID_KEYFILE_BROWSE", &wxButton::Show, protocol == SFTP && type == LogonType::key);

	wxString hostLabel = _("&Host:");
	wxString hostHint;
	wxString userHint;
	wxString userLabel = _("&User:");
	wxString passLabel = _("Pass&word:");
	switch (protocol) {
	case STORJ:
		// @translator: Keep short
		if(type == LogonType::anonymous)
			hostLabel = _("Serialized Key:");
		else
			hostLabel = _("&Satellite:");
		// @translator: Keep short
		userLabel = _("API &Key:");
		// @translator: Keep short
		passLabel = _("E&ncryption Key:");
		break;
	case S3:
		// @translator: Keep short
		userLabel = _("&Access key ID:");
		// @translator: Keep short
		passLabel = _("Secret Access &Key:");
		break;
	case AZURE_FILE:
	case AZURE_BLOB:
		// @translator: Keep short
		userLabel = _("Storage &account:");
		passLabel = _("Access &Key:");
		break;
	case GOOGLE_CLOUD:
		userLabel = _("Pro&ject ID:");
		break;
	case SWIFT:
		// @translator: Keep short
		hostLabel = _("Identity &host:");
		// @translator: Keep short
		hostHint = _("Host name of identity service");
		userLabel = _("Pro&ject:");
		// @translator: Keep short
		userHint = _("Project (or tenant) name or ID");
		break;
	case B2:
		// @translator: Keep short
		userLabel = _("&Account ID:");
		// @translator: Keep short
		passLabel = _("Application &Key:");
		break;
	default:
		break;
	}
	xrc_call(*this, "ID_HOST_DESC", &wxStaticText::SetLabel, hostLabel);
	xrc_call(*this, "ID_HOST", &wxTextCtrl::SetHint, hostHint);
	xrc_call(*this, "ID_USER_DESC", &wxStaticText::SetLabel, userLabel);
	xrc_call(*this, "ID_PASS_DESC", &wxStaticText::SetLabel, passLabel);
	xrc_call(*this, "ID_USER", &wxTextCtrl::SetHint, userHint);

	auto InsertRow = [this](std::vector<std::pair<wxStaticText*, wxTextCtrl*>> & rows, bool password) {

		if (rows.empty()) {
			return rows.end();
		}

		wxGridBagSizer* sizer = dynamic_cast<wxGridBagSizer*>(rows.back().first->GetContainingSizer());
		if (!sizer) {
			return rows.end();
		}
		auto pos = sizer->GetItemPosition(rows.back().first);

		for (int row = sizer->GetRows() - 1; row > pos.GetRow(); --row) {
			auto left = sizer->FindItemAtPosition(wxGBPosition(row, 0));
			auto right = sizer->FindItemAtPosition(wxGBPosition(row, 1));
			if (!left) {
				break;
			}
			left->SetPos(wxGBPosition(row + 1, 0));
			if (right) {
				right->SetPos(wxGBPosition(row + 1, 1));
			}
		}
		auto label = new wxStaticText(rows.back().first->GetParent(), wxID_ANY, L"");
		auto text = new wxTextCtrl(rows.back().first->GetParent(), wxID_ANY, wxString(), wxDefaultPosition, wxDefaultSize, password ? wxTE_PASSWORD : 0);
		sizer->Add(label, wxGBPosition(pos.GetRow() + 1, 0), wxDefaultSpan, wxALIGN_CENTER_VERTICAL);
		sizer->Add(text, wxGBPosition(pos.GetRow() + 1, 1), wxDefaultSpan, wxALIGN_CENTER_VERTICAL|wxGROW);

		rows.emplace_back(label, text);
		return rows.end() - 1;
	};

	auto SetLabel = [](wxStaticText & label, ServerProtocol const, std::string const& name) {
		if (name == "email") {
			label.SetLabel(_("E-&mail account:"));
		}
		else if (name == "identpath") {
			// @translator: Keep short
			label.SetLabel(_("Identity service path:"));
		}
		else if (name == "identuser") {
			label.SetLabel(_("&User:"));
		}
		else {
			label.SetLabel(name);
		}
	};

	std::vector<std::pair<wxStaticText*, wxTextCtrl*>>::iterator paramIt[ParameterSection::section_count];
	for (int i = 0; i < ParameterSection::section_count; ++i) {
		paramIt[i] = extraParameters_[i].begin();
	}

	std::vector<ParameterTraits> const& parameterTraits = ExtraServerParameterTraits(protocol);
	for (auto const& trait : parameterTraits) {
		if (trait.section_ == ParameterSection::custom) {
			continue;
		}
		auto & parameters = extraParameters_[trait.section_];
		auto & it = paramIt[trait.section_];

		if (it == parameters.cend()) {
			it = InsertRow(parameters, trait.section_ == ParameterSection::credentials);
		}

		if (it == parameters.cend()) {
			continue;
		}
		it->first->Show();
		it->second->Show();
		SetLabel(*it->first, protocol, trait.name_);
		it->second->SetHint(trait.hint_);

		++it;
	}

	auto encSizer = xrc_call(*this, "ID_ENCRYPTION", &wxWindow::GetContainingSizer);
	encSizer->Show(encSizer->GetItemCount() - 1, paramIt[ParameterSection::host] == extraParameters_[ParameterSection::host].cbegin());
	
	for (int i = 0; i < ParameterSection::section_count; ++i) {
		for (; paramIt[i] != extraParameters_[i].cend(); ++paramIt[i]) {
			paramIt[i]->first->Hide();
			paramIt[i]->second->Hide();
		}
	}

	auto keyfileSizer = xrc_call(*this, "ID_KEYFILE_DESC", &wxStaticText::GetContainingSizer);
	if (keyfileSizer) {
		keyfileSizer->CalcMin();
		keyfileSizer->Layout();
	}

	bool const hasServerType = CServer::ProtocolHasFeature(protocol, ProtocolFeature::ServerType);
	xrc_call(*this, "ID_SERVERTYPE_LABEL", &wxWindow::Show, hasServerType);
	xrc_call(*this, "ID_SERVERTYPE", &wxWindow::Show, hasServerType);
	auto * serverTypeSizer = xrc_call(*this, "ID_SERVERTYPE_LABEL", &wxWindow::GetContainingSizer)->GetContainingWindow()->GetSizer();
	serverTypeSizer->CalcMin();
	serverTypeSizer->Layout();

	bool const hasTransferMode = CServer::ProtocolHasFeature(protocol, ProtocolFeature::TransferMode);
	xrc_call(*this, "ID_TRANSFERMODE_DEFAULT", &wxWindow::Show, hasTransferMode);
	xrc_call(*this, "ID_TRANSFERMODE_ACTIVE", &wxWindow::Show, hasTransferMode);
	xrc_call(*this, "ID_TRANSFERMODE_PASSIVE", &wxWindow::Show, hasTransferMode);
	auto* transferModeLabel = XRCCTRL(*this, "ID_TRANSFERMODE_LABEL", wxStaticText);
	transferModeLabel->Show(hasTransferMode);
	transferModeLabel->GetContainingSizer()->CalcMin();
	transferModeLabel->GetContainingSizer()->Layout();
	
	if (CServer::ProtocolHasFeature(protocol, ProtocolFeature::Charset)) {
		if (FindPage(m_pCharsetPage) == wxNOT_FOUND) {
			AddPage(m_pCharsetPage, m_charsetPageText);
			wxGetApp().GetWrapEngine()->WrapRecursive(XRCCTRL(*this, "ID_CHARSET_AUTO", wxWindow)->GetParent(), 1.3);
		}
	}
	else {
		int const charsetPageIndex = FindPage(m_pCharsetPage);
		if (charsetPageIndex != wxNOT_FOUND) {
			RemovePage(charsetPageIndex);
		}
	}

	if (protocol == S3) {
		if (FindPage(m_pS3Page) == wxNOT_FOUND) {
			AddPage(m_pS3Page, L"S3");
		}
	}
	else {
		int const s3PageIndex = FindPage(m_pS3Page);
		if (s3PageIndex != wxNOT_FOUND) {
			RemovePage(s3PageIndex);
		}
	}

	GetPage(0)->GetSizer()->Fit(GetPage(0));
}


void CSiteManagerSite::SetLogonTypeCtrlState()
{
	LogonType const t = GetLogonType();
	
	xrc_call(*this, "ID_USER", &wxTextCtrl::Enable, !predefined_ && t != LogonType::anonymous);
	xrc_call(*this, "ID_PASS", &wxTextCtrl::Enable, !predefined_ && (t == LogonType::normal || t == LogonType::account));
	xrc_call(*this, "ID_ACCOUNT", &wxTextCtrl::Enable, !predefined_ && t == LogonType::account);
	xrc_call(*this, "ID_KEYFILE", &wxTextCtrl::Enable, !predefined_ && t == LogonType::key);
	xrc_call(*this, "ID_KEYFILE_BROWSE", &wxButton::Enable, !predefined_ && t == LogonType::key);
	
	for (int i = 0; i < ParameterSection::section_count; ++i) {
		for (auto & pair : extraParameters_[i]) {
			pair.second->Enable(!predefined_);
		}
	}
}

LogonType CSiteManagerSite::GetLogonType() const
{
	return GetLogonTypeFromName(xrc_call(*this, "ID_LOGONTYPE", &wxChoice::GetStringSelection).ToStdWstring());
}

bool CSiteManagerSite::Verify(bool predefined)
{
	ServerProtocol protocol = GetProtocol();
	wxASSERT(protocol != UNKNOWN);

	std::wstring const host = xrc_call(*this, "ID_HOST", &wxTextCtrl::GetValue).ToStdWstring();
	if (host.empty()) {
		XRCCTRL(*this, "ID_HOST", wxTextCtrl)->SetFocus();
		if (protocol == STORJ) {
			wxMessageBoxEx(_("You have to enter a satellite url."), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
		} else {
			wxMessageBoxEx(_("You have to enter a hostname."), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
		}
		return false;
	}

	auto logon_type = GetLogonType();

	if (protocol == SFTP &&
	        logon_type == LogonType::account)
	{
		XRCCTRL(*this, "ID_LOGONTYPE", wxChoice)->SetFocus();
		wxMessageBoxEx(_("'Account' logontype not supported by selected protocol"), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
		return false;
	}

	if (COptions::Get()->GetOptionVal(OPTION_DEFAULT_KIOSKMODE) != 0 &&
	        !predefined &&
	        (logon_type == LogonType::account || logon_type == LogonType::normal))
	{
		XRCCTRL(*this, "ID_LOGONTYPE", wxChoice)->SetFocus();
		wxString msg;
		if (COptions::Get()->OptionFromFzDefaultsXml(OPTION_DEFAULT_KIOSKMODE) && COptions::Get()->GetOptionVal(OPTION_DEFAULT_KIOSKMODE) != 0) {
			msg = _("Saving of password has been disabled by your system administrator.");
		}
		else {
			msg = _("Saving of passwords has been disabled by you.");
		}
		msg += _T("\n");
		msg += _("'Normal' and 'Account' logontypes are not available. Your entry has been changed to 'Ask for password'.");
		XRCCTRL(*this, "ID_LOGONTYPE", wxChoice)->SetStringSelection(GetNameFromLogonType(LogonType::ask));
		XRCCTRL(*this, "ID_PASS", wxTextCtrl)->ChangeValue(wxString());
		logon_type = LogonType::ask;
		wxMessageBoxEx(msg, _("Site Manager - Cannot remember password"), wxICON_INFORMATION, this);
	}

	// Set selected type
	Site site;
	site.SetLogonType(logon_type);
	site.server.SetProtocol(protocol);

	std::wstring port = xrc_call(*this, "ID_PORT", &wxTextCtrl::GetValue).ToStdWstring();
	CServerPath path;
	std::wstring error;
	
	if (!site.ParseUrl(host, port, std::wstring(), std::wstring(), error, path, protocol)) {
		XRCCTRL(*this, "ID_HOST", wxTextCtrl)->SetFocus();
		wxMessageBoxEx(error, _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
		return false;
	}
	
	XRCCTRL(*this, "ID_HOST", wxTextCtrl)->ChangeValue(site.Format(ServerFormat::host_only));
	if (site.server.GetPort() != CServer::GetDefaultPort(site.server.GetProtocol())) {
		XRCCTRL(*this, "ID_PORT", wxTextCtrl)->ChangeValue(wxString::Format(_T("%d"), site.server.GetPort()));
	}
	else {
		XRCCTRL(*this, "ID_PORT", wxTextCtrl)->ChangeValue(wxString());
	}

	SetProtocol(site.server.GetProtocol());

	if (XRCCTRL(*this, "ID_CHARSET_CUSTOM", wxRadioButton)->GetValue()) {
		if (XRCCTRL(*this, "ID_ENCODING", wxTextCtrl)->GetValue().empty()) {
			XRCCTRL(*this, "ID_ENCODING", wxTextCtrl)->SetFocus();
			wxMessageBoxEx(_("Need to specify a character encoding"), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
			return false;
		}
	}

	// Require username for non-anonymous, non-ask logon type
	const wxString user = XRCCTRL(*this, "ID_USER", wxTextCtrl)->GetValue();
		
	if (logon_type != LogonType::anonymous &&
	        logon_type != LogonType::ask &&
	        logon_type != LogonType::interactive &&
	        user.empty())
	{
		XRCCTRL(*this, "ID_USER", wxTextCtrl)->SetFocus();
		if (protocol == STORJ) {
			wxMessageBoxEx(_("You have to specify an api key"), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
		} else {
			wxMessageBoxEx(_("You have to specify a user name"), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
		}
		return false;
	}

	// We don't allow username of only spaces, confuses both users and XML libraries
	if (!user.empty()) {
		bool space_only = true;
		for (unsigned int i = 0; i < user.Len(); ++i) {
			if (user[i] != ' ') {
				space_only = false;
				break;
			}
		}
		if (space_only) {
			XRCCTRL(*this, "ID_USER", wxTextCtrl)->SetFocus();
			wxMessageBoxEx(_("Username cannot be a series of spaces"), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
			return false;
		}
	}

	// Require account for account logon type
	if (logon_type == LogonType::account &&
	        XRCCTRL(*this, "ID_ACCOUNT", wxTextCtrl)->GetValue().empty())
	{
		XRCCTRL(*this, "ID_ACCOUNT", wxTextCtrl)->SetFocus();
		wxMessageBoxEx(_("You have to enter an account name"), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
		return false;
	}

	// In key file logon type, check that the provided key file exists
	if (logon_type == LogonType::key) {
		std::wstring keyFile = xrc_call(*this, "ID_KEYFILE", &wxTextCtrl::GetValue).ToStdWstring();
		if (keyFile.empty()) {
			wxMessageBoxEx(_("You have to enter a key file path"), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
			xrc_call(*this, "ID_KEYFILE", &wxWindow::SetFocus);
			return false;
		}

		// Check (again) that the key file is in the correct format since it might have been introduced manually
		CFZPuttyGenInterface cfzg(this);

		std::wstring keyFileComment, keyFileData;
		if (cfzg.LoadKeyFile(keyFile, false, keyFileComment, keyFileData)) {
			xrc_call(*this, "ID_KEYFILE", &wxTextCtrl::ChangeValue, keyFile);
		}
		else {
			xrc_call(*this, "ID_KEYFILE", &wxWindow::SetFocus);
			return false;
		}
	}

	std::wstring const remotePathRaw = XRCCTRL(*this, "ID_REMOTEDIR", wxTextCtrl)->GetValue().ToStdWstring();
	if (!remotePathRaw.empty()) {
		std::wstring serverType = XRCCTRL(*this, "ID_SERVERTYPE", wxChoice)->GetStringSelection().ToStdWstring();

		CServerPath remotePath;
		remotePath.SetType(CServer::GetServerTypeFromName(serverType));
		if (!remotePath.SetPath(remotePathRaw)) {
			XRCCTRL(*this, "ID_REMOTEDIR", wxTextCtrl)->SetFocus();
			wxMessageBoxEx(_("Default remote path cannot be parsed. Make sure it is a valid absolute path for the selected server type."), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
			return false;
		}
	}

	std::wstring const localPath = XRCCTRL(*this, "ID_LOCALDIR", wxTextCtrl)->GetValue().ToStdWstring();
	if (XRCCTRL(*this, "ID_SYNC", wxCheckBox)->GetValue()) {
		if (remotePathRaw.empty() || localPath.empty()) {
			XRCCTRL(*this, "ID_SYNC", wxCheckBox)->SetFocus();
			wxMessageBoxEx(_("You need to enter both a local and a remote path to enable synchronized browsing for this site."), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
			return false;
		}
	}

	std::vector<std::pair<wxStaticText*, wxTextCtrl*>>::iterator paramIt[ParameterSection::section_count];
	for (int i = 0; i < ParameterSection::section_count; ++i) {
		paramIt[i] = extraParameters_[i].begin();
	}

	std::vector<ParameterTraits> const& parameterTraits = ExtraServerParameterTraits(protocol);
	for (auto const& trait : parameterTraits) {
		if (trait.section_ == ParameterSection::custom) {
			continue;
		}
		assert(paramIt[trait.section_] != extraParameters_[trait.section_].cend());

		if (!(trait.flags_ & ParameterTraits::optional)) {
			auto & controls = *paramIt[trait.section_];
			if (controls.second->GetValue().empty()) {
				controls.second->SetFocus();
				wxMessageBoxEx(_("You need to enter a value."), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, this);
				return false;
			}
		}

		++paramIt[trait.section_];
	}

	return true;
}

void CSiteManagerSite::UpdateSite(Site &site)
{
	ServerProtocol const protocol = GetProtocol();
	wxASSERT(protocol != UNKNOWN);
	site.server.SetProtocol(protocol);

	unsigned long port;
	if (!xrc_call(*this, "ID_PORT", &wxTextCtrl::GetValue).ToULong(&port) || !port || port > 65535) {
		port = CServer::GetDefaultPort(protocol);
	}
	std::wstring host = xrc_call(*this, "ID_HOST", &wxTextCtrl::GetValue).ToStdWstring();
	// SetHost does not accept URL syntax
	if (!host.empty() && host[0] == '[') {
		host = host.substr(1, host.size() - 2);
	}
	site.server.SetHost(host, port);

	auto logon_type = GetLogonType();
	site.SetLogonType(logon_type);

	site.SetUser(xrc_call(*this, "ID_USER", &wxTextCtrl::GetValue).ToStdWstring());
	auto pw = xrc_call(*this, "ID_PASS", &wxTextCtrl::GetValue).ToStdWstring();
	//
	if (protocol == STORJ && logon_type == LogonType::normal && (!pw.empty() || !site.credentials.encrypted_)) {
		pw += '|';
	}

	if (site.credentials.encrypted_) {
		if (!pw.empty()) {
			site.credentials.encrypted_ = fz::public_key();
			site.credentials.SetPass(pw);
		}
	}
	else {
		site.credentials.SetPass(pw);
	}
	site.credentials.account_ = xrc_call(*this, "ID_ACCOUNT", &wxTextCtrl::GetValue).ToStdWstring();

	site.credentials.keyFile_ = xrc_call(*this, "ID_KEYFILE", &wxTextCtrl::GetValue).ToStdWstring();

	site.comments_ = xrc_call(*this, "ID_COMMENTS", &wxTextCtrl::GetValue).ToStdWstring();
	site.m_colour = CSiteManager::GetColourFromIndex(xrc_call(*this, "ID_COLOR", &wxChoice::GetSelection));

	std::wstring const serverType = xrc_call(*this, "ID_SERVERTYPE", &wxChoice::GetStringSelection).ToStdWstring();
	site.server.SetType(CServer::GetServerTypeFromName(serverType));

	site.m_default_bookmark.m_localDir = xrc_call(*this, "ID_LOCALDIR", &wxTextCtrl::GetValue).ToStdWstring();
	site.m_default_bookmark.m_remoteDir = CServerPath();
	site.m_default_bookmark.m_remoteDir.SetType(site.server.GetType());
	site.m_default_bookmark.m_remoteDir.SetPath(xrc_call(*this, "ID_REMOTEDIR", &wxTextCtrl::GetValue).ToStdWstring());
	site.m_default_bookmark.m_sync = xrc_call(*this, "ID_SYNC", &wxCheckBox::GetValue);
	site.m_default_bookmark.m_comparison = xrc_call(*this, "ID_COMPARISON", &wxCheckBox::GetValue);

	int hours = xrc_call(*this, "ID_TIMEZONE_HOURS", &wxSpinCtrl::GetValue);
	int minutes = xrc_call(*this, "ID_TIMEZONE_MINUTES", &wxSpinCtrl::GetValue);

	site.server.SetTimezoneOffset(hours * 60 + minutes);

	if (xrc_call(*this, "ID_TRANSFERMODE_ACTIVE", &wxRadioButton::GetValue)) {
		site.server.SetPasvMode(MODE_ACTIVE);
	}
	else if (xrc_call(*this, "ID_TRANSFERMODE_PASSIVE", &wxRadioButton::GetValue)) {
		site.server.SetPasvMode(MODE_PASSIVE);
	}
	else {
		site.server.SetPasvMode(MODE_DEFAULT);
	}

	if (xrc_call(*this, "ID_LIMITMULTIPLE", &wxCheckBox::GetValue)) {
		site.server.MaximumMultipleConnections(xrc_call(*this, "ID_MAXMULTIPLE", &wxSpinCtrl::GetValue));
	}
	else {
		site.server.MaximumMultipleConnections(0);
	}

	if (xrc_call(*this, "ID_CHARSET_UTF8", &wxRadioButton::GetValue))
		site.server.SetEncodingType(ENCODING_UTF8);
	else if (xrc_call(*this, "ID_CHARSET_CUSTOM", &wxRadioButton::GetValue)) {
		std::wstring encoding = xrc_call(*this, "ID_ENCODING", &wxTextCtrl::GetValue).ToStdWstring();
		site.server.SetEncodingType(ENCODING_CUSTOM, encoding);
	}
	else {
		site.server.SetEncodingType(ENCODING_AUTO);
	}

	if (xrc_call(*this, "ID_BYPASSPROXY", &wxCheckBox::GetValue)) {
		site.server.SetBypassProxy(true);
	}
	else {
		site.server.SetBypassProxy(false);
	}

	UpdateExtraParameters(site.server);
}

void CSiteManagerSite::UpdateExtraParameters(CServer & server)
{
	server.ClearExtraParameters();

	std::vector<std::pair<wxStaticText*, wxTextCtrl*>>::iterator paramIt[ParameterSection::section_count];
	for (int i = 0; i < ParameterSection::section_count; ++i) {
		paramIt[i] = extraParameters_[i].begin();
	}
	auto const& traits = ExtraServerParameterTraits(server.GetProtocol());
	for (auto const& trait : traits) {
		if (trait.section_ == ParameterSection::credentials || trait.section_ == ParameterSection::custom) {
			continue;
		}

		server.SetExtraParameter(trait.name_, paramIt[trait.section_]->second->GetValue().ToStdWstring());
		++paramIt[trait.section_];
	}

	if (server.GetProtocol() == S3) {
		if (xrc_call(*this, "ID_S3_NOENCRYPTION", &wxRadioButton::GetValue)) {
			server.ClearExtraParameter("ssealgorithm");
		}
		else if (xrc_call(*this, "ID_S3_AES256", &wxRadioButton::GetValue)) {
			server.SetExtraParameter("ssealgorithm", L"AES256");
			
		}
		else if (xrc_call(*this, "ID_S3_AWSKMS", &wxRadioButton::GetValue)) {
			server.SetExtraParameter("ssealgorithm", L"aws:kms");
			if (xrc_call(*this, "ID_S3_KMSKEY", &wxChoice::GetSelection) == static_cast<int>(s3_sse::KmsKey::CUSTOM)) {
				server.SetExtraParameter("ssekmskey", fz::to_wstring(xrc_call(*this, "ID_S3_CUSTOM_KMS", &wxTextCtrl::GetValue)));
			}
		}
		else if (xrc_call(*this, "ID_S3_CUSTOMER_ENCRYPTION", &wxRadioButton::GetValue)) {
			server.SetExtraParameter("ssealgorithm", L"customer");
			server.SetExtraParameter("ssecustomerkey", fz::to_wstring(xrc_call(*this, "ID_S3_CUSTOMER_KEY", &wxTextCtrl::GetValue)));
		}
	}

}

void CSiteManagerSite::SetSite(Site const& site, bool predefined)
{
	predefined_ = predefined;
	
	xrc_call(*this, "ID_HOST", &wxWindow::Enable, !predefined);
	xrc_call(*this, "ID_PORT", &wxWindow::Enable, !predefined);
	xrc_call(*this, "ID_PROTOCOL", &wxWindow::Enable, !predefined);
	xrc_call(*this, "ID_ENCRYPTION", &wxWindow::Enable, !predefined);
	xrc_call(*this, "ID_TRANSFERMODE_ACTIVE", &wxWindow::Enable, !predefined);
	xrc_call(*this, "ID_TRANSFERMODE_PASSIVE", &wxWindow::Enable, !predefined);
	xrc_call(*this, "ID_TRANSFERMODE_DEFAULT", &wxWindow::Enable, !predefined);
	xrc_call(*this, "ID_SYNC", &wxCheckBox::Enable, !predefined);
	xrc_call(*this, "ID_REMOTEDIR", &wxWindow::Enable, !predefined);
	xrc_call(*this, "ID_LOCALDIR", &wxWindow::Enable, !predefined);
	xrc_call(*this, "ID_SERVERTYPE", &wxWindow::Enable, !predefined);
	xrc_call(*this, "ID_LOGONTYPE", &wxWindow::Enable, !predefined);
	xrc_call(*this, "ID_COMMENTS", &wxWindow::Enable, !predefined);
	xrc_call(*this, "ID_COLOR", &wxWindow::Enable, !predefined);
	xrc_call(*this, "ID_COMPARISON", &wxCheckBox::Enable, !predefined);
	xrc_call(*this, "ID_TIMEZONE_HOURS", &wxWindow::Enable, !predefined);
	xrc_call(*this, "ID_TIMEZONE_MINUTES", &wxWindow::Enable, !predefined);
	xrc_call(*this, "ID_LIMITMULTIPLE", &wxWindow::Enable, !predefined);
	xrc_call(*this, "ID_CHARSET_AUTO", &wxWindow::Enable, !predefined);
	xrc_call(*this, "ID_CHARSET_UTF8", &wxWindow::Enable, !predefined);
	xrc_call(*this, "ID_CHARSET_CUSTOM", &wxWindow::Enable, !predefined);

	if (!site) {
		// Empty all site information
		xrc_call(*this, "ID_HOST", &wxTextCtrl::ChangeValue, wxString());
		xrc_call(*this, "ID_PORT", &wxTextCtrl::ChangeValue, wxString());
		SetProtocol(FTP);
		xrc_call(*this, "ID_BYPASSPROXY", &wxCheckBox::SetValue, false);
		bool const kiosk_mode = COptions::Get()->GetOptionVal(OPTION_DEFAULT_KIOSKMODE) != 0;
		auto const logonType = kiosk_mode ? LogonType::ask : LogonType::normal;
		xrc_call(*this, "ID_LOGONTYPE", &wxChoice::SetStringSelection, GetNameFromLogonType(logonType));
		xrc_call(*this, "ID_USER", &wxTextCtrl::ChangeValue, wxString());
		xrc_call(*this, "ID_PASS", &wxTextCtrl::ChangeValue, wxString());
		xrc_call(*this, "ID_PASS", &wxTextCtrl::SetHint, wxString());
		xrc_call(*this, "ID_ACCOUNT", &wxTextCtrl::ChangeValue, wxString());
		xrc_call(*this, "ID_KEYFILE", &wxTextCtrl::ChangeValue, wxString());
		xrc_call(*this, "ID_COMMENTS", &wxTextCtrl::ChangeValue, wxString());
		xrc_call(*this, "ID_COLOR", &wxChoice::Select, 0);

		SetControlVisibility(FTP, logonType);
		SetLogonTypeCtrlState();

		xrc_call(*this, "ID_SERVERTYPE", &wxChoice::SetSelection, 0);
		xrc_call(*this, "ID_LOCALDIR", &wxTextCtrl::ChangeValue, wxString());
		xrc_call(*this, "ID_REMOTEDIR", &wxTextCtrl::ChangeValue, wxString());
		xrc_call(*this, "ID_SYNC", &wxCheckBox::SetValue, false);
		xrc_call<wxSpinCtrl, int>(*this, "ID_TIMEZONE_HOURS", &wxSpinCtrl::SetValue, 0);
		xrc_call<wxSpinCtrl, int>(*this, "ID_TIMEZONE_MINUTES", &wxSpinCtrl::SetValue, 0);

		xrc_call(*this, "ID_TRANSFERMODE_DEFAULT", &wxRadioButton::SetValue, true);
		xrc_call(*this, "ID_LIMITMULTIPLE", &wxCheckBox::SetValue, false);
		xrc_call(*this, "ID_MAXMULTIPLE", &wxSpinCtrl::Enable, false);
		xrc_call<wxSpinCtrl, int>(*this, "ID_MAXMULTIPLE", &wxSpinCtrl::SetValue, 1);

		xrc_call(*this, "ID_CHARSET_AUTO", &wxRadioButton::SetValue, true);
		xrc_call(*this, "ID_ENCODING", &wxTextCtrl::ChangeValue, wxString());
		xrc_call(*this, "ID_ENCODING", &wxTextCtrl::Enable, false);
	}
	else {
		xrc_call(*this, "ID_HOST", &wxTextCtrl::ChangeValue, site.Format(ServerFormat::host_only));
		unsigned int port = site.server.GetPort();

		if (port != CServer::GetDefaultPort(site.server.GetProtocol())) {
			xrc_call(*this, "ID_PORT", &wxTextCtrl::ChangeValue, wxString::Format(_T("%d"), port));
		}
		else {
			xrc_call(*this, "ID_PORT", &wxTextCtrl::ChangeValue, wxString());
		}

		ServerProtocol protocol = site.server.GetProtocol();
		SetProtocol(protocol);
		xrc_call(*this, "ID_BYPASSPROXY", &wxCheckBox::SetValue, site.server.GetBypassProxy());

		LogonType const logonType = site.credentials.logonType_;
		xrc_call(*this, "ID_LOGONTYPE", &wxChoice::SetStringSelection, GetNameFromLogonType(logonType));

		SetControlVisibility(protocol, logonType);

		SetLogonTypeCtrlState();

		xrc_call(*this, "ID_USER", &wxTextCtrl::ChangeValue, site.server.GetUser());
		xrc_call(*this, "ID_ACCOUNT", &wxTextCtrl::ChangeValue, site.credentials.account_);

		std::wstring pass = site.credentials.GetPass();
		if (protocol == STORJ) {
			size_t pos = pass.rfind('|');
			if (pos != std::wstring::npos) {
				pass = pass.substr(0, pos);
			}
		}
		
		if (site.credentials.encrypted_) {
			xrc_call(*this, "ID_PASS", &wxTextCtrl::ChangeValue, wxString());
			
			// @translator: Keep this string as short as possible
			xrc_call(*this, "ID_PASS", &wxTextCtrl::SetHint, _("Leave empty to keep existing password."));
			for (auto & control : extraParameters_[ParameterSection::credentials]) {
				control.second->SetHint(_("Leave empty to keep existing data."));
			}
		}
		else {
			xrc_call(*this, "ID_PASS", &wxTextCtrl::ChangeValue, pass);
			xrc_call(*this, "ID_PASS", &wxTextCtrl::SetHint, wxString());
			
			auto it = extraParameters_[ParameterSection::credentials].begin();

			auto const& traits = ExtraServerParameterTraits(protocol);
			for (auto const& trait : traits) {
				if (trait.section_ != ParameterSection::credentials) {
					continue;
				}

				it->second->ChangeValue(site.credentials.GetExtraParameter(trait.name_));
				++it;
			}
		}

		SetExtraParameters(site.server);

		xrc_call(*this, "ID_KEYFILE", &wxTextCtrl::ChangeValue, site.credentials.keyFile_);
		xrc_call(*this, "ID_COMMENTS", &wxTextCtrl::ChangeValue, site.comments_);
		xrc_call(*this, "ID_COLOR", &wxChoice::Select, CSiteManager::GetColourIndex(site.m_colour));

		xrc_call(*this, "ID_SERVERTYPE", &wxChoice::SetSelection, site.server.GetType());
		xrc_call(*this, "ID_LOCALDIR", &wxTextCtrl::ChangeValue, site.m_default_bookmark.m_localDir);
		xrc_call(*this, "ID_REMOTEDIR", &wxTextCtrl::ChangeValue, site.m_default_bookmark.m_remoteDir.GetPath());
		xrc_call(*this, "ID_SYNC", &wxCheckBox::SetValue, site.m_default_bookmark.m_sync);
		xrc_call(*this, "ID_COMPARISON", &wxCheckBox::SetValue, site.m_default_bookmark.m_comparison);
		xrc_call<wxSpinCtrl, int>(*this, "ID_TIMEZONE_HOURS", &wxSpinCtrl::SetValue, site.server.GetTimezoneOffset() / 60);
		xrc_call<wxSpinCtrl, int>(*this, "ID_TIMEZONE_MINUTES", &wxSpinCtrl::SetValue, site.server.GetTimezoneOffset() % 60);

		if (CServer::ProtocolHasFeature(site.server.GetProtocol(), ProtocolFeature::TransferMode)) {
			PasvMode pasvMode = site.server.GetPasvMode();
			if (pasvMode == MODE_ACTIVE) {
				xrc_call(*this, "ID_TRANSFERMODE_ACTIVE", &wxRadioButton::SetValue, true);
			}
			else if (pasvMode == MODE_PASSIVE) {
				xrc_call(*this, "ID_TRANSFERMODE_PASSIVE", &wxRadioButton::SetValue, true);
			}
			else {
				xrc_call(*this, "ID_TRANSFERMODE_DEFAULT", &wxRadioButton::SetValue, true);
			}
		}

		int const maxMultiple = site.server.MaximumMultipleConnections();
		xrc_call(*this, "ID_LIMITMULTIPLE", &wxCheckBox::SetValue, maxMultiple != 0);
		if (maxMultiple != 0) {
			xrc_call(*this, "ID_MAXMULTIPLE", &wxSpinCtrl::Enable, !predefined);
			xrc_call<wxSpinCtrl, int>(*this, "ID_MAXMULTIPLE", &wxSpinCtrl::SetValue, maxMultiple);
		}
		else {
			xrc_call(*this, "ID_MAXMULTIPLE", &wxSpinCtrl::Enable, false);
			xrc_call<wxSpinCtrl, int>(*this, "ID_MAXMULTIPLE", &wxSpinCtrl::SetValue, 1);
		}

		switch (site.server.GetEncodingType()) {
		default:
		case ENCODING_AUTO:
			xrc_call(*this, "ID_CHARSET_AUTO", &wxRadioButton::SetValue, true);
			break;
		case ENCODING_UTF8:
			xrc_call(*this, "ID_CHARSET_UTF8", &wxRadioButton::SetValue, true);
			break;
		case ENCODING_CUSTOM:
			xrc_call(*this, "ID_CHARSET_CUSTOM", &wxRadioButton::SetValue, true);
			break;
		}
		xrc_call(*this, "ID_ENCODING", &wxTextCtrl::Enable, !predefined && site.server.GetEncodingType() == ENCODING_CUSTOM);
		xrc_call(*this, "ID_ENCODING", &wxTextCtrl::ChangeValue, site.server.GetCustomEncoding());

		xrc_call(*this, "ID_S3_KMSKEY", &wxChoice::SetSelection, static_cast<int>(s3_sse::KmsKey::DEFAULT));
		auto ssealgorithm = site.server.GetExtraParameter("ssealgorithm");
		if (ssealgorithm.empty()) {
			xrc_call(*this, "ID_S3_NOENCRYPTION", &wxRadioButton::SetValue, true);
		}
		else if (ssealgorithm == "AES256") {
			xrc_call(*this, "ID_S3_AES256", &wxRadioButton::SetValue, true);
		}
		else if (ssealgorithm == "aws:kms") {
			xrc_call(*this, "ID_S3_AWSKMS", &wxRadioButton::SetValue, true);
			auto sseKmsKey = site.server.GetExtraParameter("ssekmskey");
			if (!sseKmsKey.empty()) {
				xrc_call(*this, "ID_S3_KMSKEY", &wxChoice::SetSelection, static_cast<int>(s3_sse::KmsKey::CUSTOM));
				xrc_call(*this, "ID_S3_CUSTOM_KMS", &wxTextCtrl::ChangeValue, sseKmsKey);
			}
		}
		else if (ssealgorithm == "customer") {
			xrc_call(*this, "ID_S3_CUSTOMER_ENCRYPTION", &wxRadioButton::SetValue, true);
			auto customerKey = site.server.GetExtraParameter("ssecustomerkey");
			xrc_call(*this, "ID_S3_CUSTOMER_KEY", &wxTextCtrl::ChangeValue, customerKey);
		}
	}
}

void CSiteManagerSite::SetExtraParameters(CServer const& server)
{
	std::vector<std::pair<wxStaticText*, wxTextCtrl*>>::iterator paramIt[ParameterSection::section_count];
	for (int i = 0; i < ParameterSection::section_count; ++i) {
		paramIt[i] = extraParameters_[i].begin();
	}
	auto const& traits = ExtraServerParameterTraits(server.GetProtocol());
	for (auto const& trait : traits) {
		if (trait.section_ == ParameterSection::credentials || trait.section_ == ParameterSection::custom) {
			continue;
		}

		std::wstring value = server.GetExtraParameter(trait.name_);
		paramIt[trait.section_]->second->ChangeValue(value.empty() ? trait.default_ : value);
		++paramIt[trait.section_];
	}
}

void CSiteManagerSite::OnProtocolSelChanged(wxCommandEvent&)
{
	auto const protocol = GetProtocol();
	UpdateHostFromDefaults(protocol);

	CServer server;
	if (previousProtocol_ != UNKNOWN) {
		server.SetProtocol(previousProtocol_);
		UpdateExtraParameters(server);
	}
	server.SetProtocol(protocol);
	SetExtraParameters(server);

	auto const logonType = GetLogonType();
	
	SetControlVisibility(protocol, logonType);
	
	SetLogonTypeCtrlState();

	previousProtocol_ = protocol;
}

void CSiteManagerSite::OnLogontypeSelChanged(wxCommandEvent&)
{
	LogonType const t = GetLogonType();

	SetControlVisibility(GetProtocol(), t);

	SetLogonTypeCtrlState();
}

void CSiteManagerSite::OnCharsetChange(wxCommandEvent&)
{
	bool checked = xrc_call(*this, "ID_CHARSET_CUSTOM", &wxRadioButton::GetValue);
	xrc_call(*this, "ID_ENCODING", &wxTextCtrl::Enable, checked);
}

void CSiteManagerSite::OnLimitMultipleConnectionsChanged(wxCommandEvent& event)
{
	XRCCTRL(*this, "ID_MAXMULTIPLE", wxSpinCtrl)->Enable(event.IsChecked());
}

void CSiteManagerSite::OnRemoteDirBrowse(wxCommandEvent&)
{
	wxDirDialog dlg(this, _("Choose the default local directory"), XRCCTRL(*this, "ID_LOCALDIR", wxTextCtrl)->GetValue(), wxDD_NEW_DIR_BUTTON);
	if (dlg.ShowModal() == wxID_OK) {
		XRCCTRL(*this, "ID_LOCALDIR", wxTextCtrl)->ChangeValue(dlg.GetPath());
	}
}

void CSiteManagerSite::OnKeyFileBrowse(wxCommandEvent&)
{
	wxString wildcards(_T("PPK files|*.ppk|PEM files|*.pem|All files|*.*"));
	wxFileDialog dlg(this, _("Choose a key file"), wxString(), wxString(), wildcards, wxFD_OPEN|wxFD_FILE_MUST_EXIST);

	if (dlg.ShowModal() == wxID_OK) {
		std::wstring keyFilePath = dlg.GetPath().ToStdWstring();
		// If the selected file was a PEM file, LoadKeyFile() will automatically convert it to PPK
		// and tell us the new location.
		CFZPuttyGenInterface fzpg(this);

		std::wstring keyFileComment, keyFileData;
		if (fzpg.LoadKeyFile(keyFilePath, false, keyFileComment, keyFileData)) {
			XRCCTRL(*this, "ID_KEYFILE", wxTextCtrl)->ChangeValue(keyFilePath);
#if USE_MAC_SANDBOX
			OSXSandboxUserdirs::Get().AddFile(keyFilePath);
#endif

		}
		else {
			xrc_call(*this, "ID_KEYFILE", &wxWindow::SetFocus);
		}
	}
}

void CSiteManagerSite::OnGenerateEncryptionKey(wxCommandEvent&)
{
#if ENABLE_STORJ
#endif
}

void CSiteManagerSite::UpdateHostFromDefaults(ServerProtocol const protocol)
{
	if (protocol != previousProtocol_) {
		auto const oldDefault = std::get<0>(GetDefaultHost(previousProtocol_));
		auto const newDefault = GetDefaultHost(protocol);

		std::wstring const host = xrc_call(*this, "ID_HOST", &wxTextCtrl::GetValue).ToStdWstring();
		if (host.empty() || host == oldDefault) {
			xrc_call(*this, "ID_HOST", &wxTextCtrl::ChangeValue, std::get<0>(newDefault));
		}
		xrc_call(*this, "ID_HOST", &wxTextCtrl::SetHint, std::get<1>(newDefault));
	}
}
