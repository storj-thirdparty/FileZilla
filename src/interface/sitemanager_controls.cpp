#include <filezilla.h>
#include "sitemanager_controls.h"

#include "dialogex.h"
#include "fzputtygen_interface.h"
#include "Options.h"
#include "sitemanager.h"
#if ENABLE_STORJ
#include "storj_key_interface.h"
#endif
#include "textctrlex.h"
#include "xrc_helper.h"
#include "wxext/spinctrlex.h"

#include <s3sse.h>

#include <libfilezilla/translate.hpp>

#include <wx/dirdlg.h>
#include <wx/filedlg.h>
#include <wx/gbsizer.h>
#include <wx/hyperlink.h>
#include <wx/statline.h>

#include <array>
#include <map>

namespace {
struct ProtocolGroup {
	std::wstring name;
	std::vector<std::pair<ServerProtocol, std::wstring>> protocols;
};

std::array<ProtocolGroup, 2> const& protocolGroups()
{
	static auto const groups = std::array<ProtocolGroup, 2>{{
		{
			fztranslate("FTP - File Transfer Protocol"), {
				{ FTP, fztranslate("Use explicit FTP over TLS if available") },
				{ FTPES, fztranslate("Require explicit FTP over TLS") },
				{ FTPS, fztranslate("Require implicit FTP over TLS") },
				{ INSECURE_FTP, fztranslate("Only use plain FTP (insecure)") }
			}
		},
		{
			fztranslate("WebDAV"), {
				{ WEBDAV, fztranslate("Using secure HTTPS") },
				{ INSECURE_WEBDAV, fztranslate("Using insecure HTTP") }
			}
		}
	}};
	return groups;
}

std::pair<std::array<ProtocolGroup, 2>::const_iterator, std::vector<std::pair<ServerProtocol, std::wstring>>::const_iterator> findGroup(ServerProtocol protocol)
{
	auto const& groups = protocolGroups();
	for (auto group = groups.cbegin(); group != groups.cend(); ++group) {
		for (auto entry = group->protocols.cbegin(); entry != group->protocols.cend(); ++entry) {
			if (entry->first == protocol) {
				return std::make_pair(group, entry);
			}
		}
	}

	return std::make_pair(groups.cend(), std::vector<std::pair<ServerProtocol, std::wstring>>::const_iterator());
}
}

GeneralSiteControls::GeneralSiteControls(wxWindow & parent, DialogLayout const& lay, wxFlexGridSizer & sizer, std::function<void(ServerProtocol protocol, LogonType logon_type)> const& changeHandler)
    : SiteControls(parent)
    , changeHandler_(changeHandler)
{
	if (!sizer.IsColGrowable(0)) {
		sizer.AddGrowableCol(0);
	}

	auto * bag = lay.createGridBag(2);
	bag->AddGrowableCol(1);
	sizer.Add(bag, 0, wxGROW);

	bag->SetEmptyCellSize(wxSize(-bag->GetHGap(), -bag->GetVGap()));

	lay.gbNewRow(bag);
	lay.gbAdd(bag, new wxStaticText(&parent, -1, _("Pro&tocol:")), lay.valign);
	auto protocols = new wxChoice(&parent, XRCID("ID_PROTOCOL"));
	lay.gbAdd(bag, protocols, lay.valigng);

	lay.gbNewRow(bag);
	lay.gbAdd(bag, new wxStaticText(&parent, XRCID("ID_HOST_DESC"), _("&Host:")), lay.valign);
	auto * row = lay.createFlex(0, 1);
	row->AddGrowableCol(0);
	lay.gbAdd(bag, row, lay.valigng);
	row->Add(new wxTextCtrlEx(&parent, XRCID("ID_HOST")), lay.valigng);
	row->Add(new wxStaticText(&parent, -1, _("&Port:")), lay.valign);
	auto* port = new wxTextCtrlEx(&parent, XRCID("ID_PORT"), wxString(), wxDefaultPosition, wxSize(lay.dlgUnits(27), -1));
	port->SetMaxLength(5);
	row->Add(port, lay.valign);

	lay.gbNewRow(bag);
	lay.gbAdd(bag, new wxStaticText(&parent, XRCID("ID_ENCRYPTION_DESC"), _("&Encryption:")), lay.valign);
	auto brow = new wxBoxSizer(wxHORIZONTAL);
	lay.gbAdd(bag, brow, lay.valigng);
	brow->Add(new wxChoice(&parent, XRCID("ID_ENCRYPTION")), 1);
	brow->Add(new wxHyperlinkCtrl(&parent, XRCID("ID_DOCS"), _("Docs"), L"https://github.com/storj/storj/wiki/Vanguard-Release-Setup-Instructions"), lay.valign)->Show(false);
	brow->AddSpacer(5);
	brow->Add(new wxHyperlinkCtrl(&parent, XRCID("ID_SIGNUP"), _("Signup"), L"https://app.storj.io/#/signup"), lay.valign)->Show(false);
	brow->AddSpacer(0);	
	
	lay.gbNewRow(bag);
	lay.gbAdd(bag, new wxStaticText(&parent, XRCID("ID_EXTRA_HOST_DESC"), L""), lay.valign)->Show(false);
	lay.gbAdd(bag, new wxTextCtrlEx(&parent, XRCID("ID_EXTRA_HOST")), lay.valigng)->Show(false);

	lay.gbAddRow(bag, new wxStaticLine(&parent), lay.grow);

	lay.gbNewRow(bag);
	lay.gbAdd(bag, new wxStaticText(&parent, -1, _("&Logon Type:")), lay.valign);
	auto logonTypes = new wxChoice(&parent, XRCID("ID_LOGONTYPE"));
	lay.gbAdd(bag, logonTypes, lay.valigng);

	lay.gbNewRow(bag);
	lay.gbAdd(bag, new wxStaticText(&parent, XRCID("ID_USER_DESC"), _("&User:")), lay.valign);
	lay.gbAdd(bag, new wxTextCtrlEx(&parent, XRCID("ID_USER")), lay.valigng);

	lay.gbNewRow(bag);
	lay.gbAdd(bag, new wxStaticText(&parent, XRCID("ID_EXTRA_USER_DESC"), L""), lay.valign)->Show(false);
	lay.gbAdd(bag, new wxTextCtrlEx(&parent, XRCID("ID_EXTRA_USER")), lay.valigng)->Show(false);

	lay.gbNewRow(bag);
	lay.gbAdd(bag, new wxStaticText(&parent, XRCID("ID_PASS_DESC"), _("Pass&word:")), lay.valign);
	lay.gbAdd(bag, new wxTextCtrlEx(&parent, XRCID("ID_PASS"), L"", wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD), lay.valigng);

	lay.gbNewRow(bag);
	lay.gbAdd(bag, new wxStaticText(&parent, XRCID("ID_ACCOUNT_DESC"), _("&Account:")), lay.valign);
	lay.gbAdd(bag, new wxTextCtrlEx(&parent, XRCID("ID_ACCOUNT")), lay.valigng);

	lay.gbNewRow(bag);
	lay.gbAdd(bag, new wxStaticText(&parent, XRCID("ID_KEYFILE_DESC"), _("&Key file:")), lay.valign)->Show(false);
	row = lay.createFlex(0, 1);
	row->AddGrowableCol(0);
	lay.gbAdd(bag, row, lay.valigng);
	row->Add(new wxTextCtrlEx(&parent, XRCID("ID_KEYFILE")), lay.valigng)->Show(false);
	auto keyfileBrowse = new wxButton(&parent, XRCID("ID_KEYFILE_BROWSE"), _("Browse..."));
	row->Add(keyfileBrowse, lay.valign)->Show(false);

	lay.gbNewRow(bag);
	lay.gbAdd(bag, new wxStaticText(&parent, XRCID("ID_ENCRYPTIONKEY_DESC"), _("Encryption &key:")), lay.valign)->Show(false);
	row = lay.createFlex(0, 1);
	row->AddGrowableCol(0);
	lay.gbAdd(bag, row, lay.valigng);
	row->Add(new wxTextCtrlEx(&parent, XRCID("ID_ENCRYPTIONKEY")), lay.valigng)->Show(false);
	auto storjKeyGenerate = new wxButton(&parent, XRCID("ID_ENCRYPTIONKEY_GENERATE"), _("Generate..."));
	row->Add(storjKeyGenerate, lay.valign)->Show(false);

	lay.gbNewRow(bag);
	lay.gbAdd(bag, new wxStaticText(&parent, XRCID("ID_EXTRA_CREDENTIALS_DESC"), L""), lay.valign)->Show(false);
	lay.gbAdd(bag, new wxTextCtrlEx(&parent, XRCID("ID_EXTRA_CREDENTIALS"), L"", wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD), lay.valigng)->Show(false);

	lay.gbNewRow(bag);
	lay.gbAdd(bag, new wxStaticText(&parent, XRCID("ID_EXTRA_EXTRA_DESC"), L""), lay.valign)->Show(false);
	lay.gbAdd(bag, new wxTextCtrlEx(&parent, XRCID("ID_EXTRA_EXTRA")), lay.valigng)->Show(false);

	extraParameters_[ParameterSection::host].emplace_back("", XRCCTRL(parent_, "ID_EXTRA_HOST_DESC", wxStaticText), XRCCTRL(parent_, "ID_EXTRA_HOST", wxTextCtrl));
	extraParameters_[ParameterSection::user].emplace_back("", XRCCTRL(parent_, "ID_EXTRA_USER_DESC", wxStaticText), XRCCTRL(parent_, "ID_EXTRA_USER", wxTextCtrl));
	extraParameters_[ParameterSection::credentials].emplace_back("", XRCCTRL(parent_, "ID_EXTRA_CREDENTIALS_DESC", wxStaticText), XRCCTRL(parent_, "ID_EXTRA_CREDENTIALS", wxTextCtrl));
	extraParameters_[ParameterSection::extra].emplace_back("", XRCCTRL(parent_, "ID_EXTRA_EXTRA_DESC", wxStaticText), XRCCTRL(parent_, "ID_EXTRA_EXTRA", wxTextCtrl));

	for (auto const& proto : CServer::GetDefaultProtocols()) {
		auto const entry = findGroup(proto);
		if (entry.first != protocolGroups().cend()) {
			if (entry.second == entry.first->protocols.cbegin()) {
				mainProtocolListIndex_[proto] = protocols->Append(entry.first->name);
			}
			else {
				mainProtocolListIndex_[proto] = mainProtocolListIndex_[entry.first->protocols.front().first];
			}
		}
		else {
			mainProtocolListIndex_[proto] = protocols->Append(CServer::GetProtocolName(proto));
		}
	}

	for (int i = 0; i < static_cast<int>(LogonType::count); ++i) {
		logonTypes->Append(GetNameFromLogonType(static_cast<LogonType>(i)));
	}

	protocols->Bind(wxEVT_CHOICE, [this](wxEvent const&) {
		auto p = GetProtocol();
		SetProtocol(p);
		if (changeHandler_) {
			changeHandler_(p, GetLogonType());
		}
	});

	logonTypes->Bind(wxEVT_CHOICE, [this](wxEvent const&) {
		if (changeHandler_) {
			changeHandler_(GetProtocol(), GetLogonType());
		}
	});

	keyfileBrowse->Bind(wxEVT_BUTTON, [this](wxEvent const&) {
		wxString wildcards(_T("PPK files|*.ppk|PEM files|*.pem|All files|*.*"));
		wxFileDialog dlg(&parent_, _("Choose a key file"), wxString(), wxString(), wildcards, wxFD_OPEN|wxFD_FILE_MUST_EXIST);

		if (dlg.ShowModal() == wxID_OK) {
			std::wstring keyFilePath = dlg.GetPath().ToStdWstring();
			// If the selected file was a PEM file, LoadKeyFile() will automatically convert it to PPK
			// and tell us the new location.
			CFZPuttyGenInterface fzpg(&parent_);

			std::wstring keyFileComment, keyFileData;
			if (fzpg.LoadKeyFile(keyFilePath, false, keyFileComment, keyFileData)) {
				XRCCTRL(parent_, "ID_KEYFILE", wxTextCtrl)->ChangeValue(keyFilePath);
	#if USE_MAC_SANDBOX
				OSXSandboxUserdirs::Get().AddFile(keyFilePath);
	#endif
			}
			else {
				xrc_call(parent_, "ID_KEYFILE", &wxWindow::SetFocus);
			}
		}
	});

#if ENABLE_STORJ
	storjKeyGenerate->Bind(wxEVT_BUTTON, [this](wxEvent const&) {
		CStorjKeyInterface generator(&parent_);
		std::wstring key = generator.GenerateKey();
		if (!key.empty()) {
			xrc_call(parent_, "ID_ENCRYPTIONKEY", &wxTextCtrl::ChangeValue, wxString(key));
			xrc_call(parent_, "ID_ENCRYPTIONKEY", &wxWindow::SetFocus);

			wxDialogEx dlg;
			if (dlg.Load(&parent_, "ID_STORJ_GENERATED_KEY")) {
				dlg.WrapRecursive(&dlg, 2.5);
				dlg.GetSizer()->Fit(&dlg);
				dlg.GetSizer()->SetSizeHints(&dlg);
				xrc_call(dlg, "ID_KEY", &wxTextCtrl::ChangeValue, wxString(key));
				dlg.ShowModal();
			}
		}
	});
#endif
}

void GeneralSiteControls::SetSite(Site const& site)
{
	if (!site) {
		// Empty all site information
		xrc_call(parent_, "ID_HOST", &wxTextCtrl::ChangeValue, wxString());
		xrc_call(parent_, "ID_PORT", &wxTextCtrl::ChangeValue, wxString());
		SetProtocol(FTP);
		xrc_call(parent_, "ID_USER", &wxTextCtrl::ChangeValue, wxString());
		xrc_call(parent_, "ID_PASS", &wxTextCtrl::ChangeValue, wxString());
		xrc_call(parent_, "ID_PASS", &wxTextCtrl::SetHint, wxString());
		xrc_call(parent_, "ID_ACCOUNT", &wxTextCtrl::ChangeValue, wxString());
		xrc_call(parent_, "ID_KEYFILE", &wxTextCtrl::ChangeValue, wxString());
		xrc_call(parent_, "ID_ENCRYPTIONKEY", &wxTextCtrl::ChangeValue, wxString());

		xrc_call(parent_, "ID_LOGONTYPE", &wxChoice::SetStringSelection, GetNameFromLogonType(logonType_));
	}
	else {
		xrc_call(parent_, "ID_HOST", &wxTextCtrl::ChangeValue, site.Format(ServerFormat::host_only));

		unsigned int port = site.server.GetPort();
		if (port != CServer::GetDefaultPort(site.server.GetProtocol())) {
			xrc_call(parent_, "ID_PORT", &wxTextCtrl::ChangeValue, wxString::Format(_T("%d"), port));
		}
		else {
			xrc_call(parent_, "ID_PORT", &wxTextCtrl::ChangeValue, wxString());
		}

		ServerProtocol protocol = site.server.GetProtocol();
		SetProtocol(protocol);

		xrc_call(parent_, "ID_LOGONTYPE", &wxChoice::SetStringSelection, GetNameFromLogonType(site.credentials.logonType_));

		xrc_call(parent_, "ID_USER", &wxTextCtrl::ChangeValue, site.server.GetUser());
		xrc_call(parent_, "ID_ACCOUNT", &wxTextCtrl::ChangeValue, site.credentials.account_);

		std::wstring pass = site.credentials.GetPass();
		std::wstring encryptionKey;
		if (protocol == STORJ) {
			size_t pos = pass.rfind('|');
			if (pos != std::wstring::npos) {
				encryptionKey = pass.substr(pos + 1);
				pass = pass.substr(0, pos);
			}
		}

		if (logonType_ != LogonType::anonymous && logonType_ != LogonType::interactive && (protocol != SFTP || logonType_ != LogonType::key)) {
			if (site.credentials.encrypted_) {
				xrc_call(parent_, "ID_PASS", &wxTextCtrl::ChangeValue, wxString());
				xrc_call(parent_, "ID_ENCRYPTIONKEY", &wxTextCtrl::ChangeValue, wxString());

				// @translator: Keep this string as short as possible
				xrc_call(parent_, "ID_PASS", &wxTextCtrl::SetHint, _("Leave empty to keep existing password."));
				for (auto & control : extraParameters_[ParameterSection::credentials]) {
					std::get<2>(control)->SetHint(_("Leave empty to keep existing data."));
				}
			}
			else {
				xrc_call(parent_, "ID_PASS", &wxTextCtrl::ChangeValue, pass);
				xrc_call(parent_, "ID_PASS", &wxTextCtrl::SetHint, wxString());
				xrc_call(parent_, "ID_ENCRYPTIONKEY", &wxTextCtrl::ChangeValue, encryptionKey);

				auto it = extraParameters_[ParameterSection::credentials].begin();

				auto const& traits = ExtraServerParameterTraits(protocol);
				for (auto const& trait : traits) {
					if (trait.section_ != ParameterSection::credentials) {
						continue;
					}

					std::get<2>(*it)->ChangeValue(site.credentials.GetExtraParameter(trait.name_));
					++it;
				}
			}
		}
		else {
			xrc_call(parent_, "ID_PASS", &wxTextCtrl::ChangeValue, wxString());
			xrc_call(parent_, "ID_PASS", &wxTextCtrl::SetHint, wxString());
		}

		std::vector<GeneralSiteControls::Parameter>::iterator paramIt[ParameterSection::section_count];
		for (int i = 0; i < ParameterSection::section_count; ++i) {
			paramIt[i] = extraParameters_[i].begin();
		}
		auto const& traits = ExtraServerParameterTraits(protocol);
		for (auto const& trait : traits) {
			if (trait.section_ == ParameterSection::credentials || trait.section_ == ParameterSection::custom) {
				continue;
			}

			std::wstring value = site.server.GetExtraParameter(trait.name_);
			std::get<2>(*paramIt[trait.section_])->ChangeValue(value.empty() ? trait.default_ : value);
			++paramIt[trait.section_];
		}

		xrc_call(parent_, "ID_KEYFILE", &wxTextCtrl::ChangeValue, site.credentials.keyFile_);
	}
}

void GeneralSiteControls::SetControlVisibility(ServerProtocol protocol, LogonType type)
{
	protocol_ = protocol;
	logonType_ = type;

	auto const group = findGroup(protocol);
	bool const isFtp = group.first != protocolGroups().cend() && group.first->protocols.front().first == FTP;

	xrc_call(parent_, "ID_ENCRYPTION_DESC", &wxStaticText::Show, group.first != protocolGroups().cend());
	xrc_call(parent_, "ID_ENCRYPTION", &wxChoice::Show, group.first != protocolGroups().cend());

	xrc_call(parent_, "ID_DOCS", &wxControl::Show, protocol == STORJ);
	xrc_call(parent_, "ID_SIGNUP", &wxControl::Show, protocol == STORJ);

	auto const supportedlogonTypes = GetSupportedLogonTypes(protocol);
	assert(!supportedlogonTypes.empty());

	auto choice = XRCCTRL(parent_, "ID_LOGONTYPE", wxChoice);
	choice->Clear();

	if (std::find(supportedlogonTypes.cbegin(), supportedlogonTypes.cend(), type) == supportedlogonTypes.cend()) {
		type = supportedlogonTypes.front();
	}

	for (auto const supportedLogonType : supportedlogonTypes) {
		if (protocol == STORJ && (GetNameFromLogonType(supportedLogonType) == "Anonymous"))
			choice->Append("Serialized Key");
		else
			choice->Append(GetNameFromLogonType(supportedLogonType));
		if (supportedLogonType == type) {
			choice->SetSelection(choice->GetCount() - 1);
		}
	}

	bool const hasUser = ProtocolHasUser(protocol) && type != LogonType::anonymous;

	xrc_call(parent_, "ID_USER_DESC", &wxStaticText::Show, hasUser);
	xrc_call(parent_, "ID_USER", &wxTextCtrl::Show, hasUser);
	xrc_call(parent_, "ID_PASS_DESC", &wxStaticText::Show, type != LogonType::anonymous && type != LogonType::interactive  && (protocol != SFTP || type != LogonType::key));
	xrc_call(parent_, "ID_PASS", &wxTextCtrl::Show, type != LogonType::anonymous && type != LogonType::interactive && (protocol != SFTP || type != LogonType::key));
	xrc_call(parent_, "ID_ACCOUNT_DESC", &wxStaticText::Show, isFtp && type == LogonType::account);
	xrc_call(parent_, "ID_ACCOUNT", &wxTextCtrl::Show, isFtp && type == LogonType::account);
	xrc_call(parent_, "ID_KEYFILE_DESC", &wxStaticText::Show, protocol == SFTP && type == LogonType::key);
	xrc_call(parent_, "ID_KEYFILE", &wxTextCtrl::Show, protocol == SFTP && type == LogonType::key);
	xrc_call(parent_, "ID_KEYFILE_BROWSE", &wxButton::Show, protocol == SFTP && type == LogonType::key);

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
	xrc_call(parent_, "ID_HOST_DESC", &wxStaticText::SetLabel, hostLabel);
	xrc_call(parent_, "ID_HOST", &wxTextCtrl::SetHint, hostHint);
	xrc_call(parent_, "ID_USER_DESC", &wxStaticText::SetLabel, userLabel);
	xrc_call(parent_, "ID_PASS_DESC", &wxStaticText::SetLabel, passLabel);
	xrc_call(parent_, "ID_USER", &wxTextCtrl::SetHint, userHint);

	auto InsertRow = [this](std::vector<GeneralSiteControls::Parameter> & rows, std::string const &name, bool password) {

		if (rows.empty()) {
			return rows.end();
		}

		wxWindow* const parent = std::get<1>(rows.back())->GetParent();
		wxGridBagSizer* const sizer = dynamic_cast<wxGridBagSizer*>(std::get<1>(rows.back())->GetContainingSizer());

		if (!sizer) {
			return rows.end();
		}
		auto pos = sizer->GetItemPosition(std::get<1>(rows.back()));

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
		auto label = new wxStaticText(parent, wxID_ANY, L"");
		auto text = new wxTextCtrlEx(parent, wxID_ANY, wxString(), wxDefaultPosition, wxDefaultSize, password ? wxTE_PASSWORD : 0);
		sizer->Add(label, wxGBPosition(pos.GetRow() + 1, 0), wxDefaultSpan, wxALIGN_CENTER_VERTICAL);
		sizer->Add(text, wxGBPosition(pos.GetRow() + 1, 1), wxDefaultSpan, wxALIGN_CENTER_VERTICAL|wxGROW);

		rows.emplace_back(name, label, text);
		return rows.end() - 1;
	};

	auto SetLabel = [](wxStaticText & label, ServerProtocol const, std::string const& name) {
		if (name == "login_hint") {
			label.SetLabel(_("Login (optional):"));
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

	std::vector<GeneralSiteControls::Parameter>::iterator paramIt[ParameterSection::section_count];
	for (int i = 0; i < ParameterSection::section_count; ++i) {
		paramIt[i] = extraParameters_[i].begin();
	}

	std::map<std::pair<std::string, ParameterSection::type>, std::wstring> values;
	for (int i = 0; i < ParameterSection::section_count; ++i) {
		for (auto const& row : extraParameters_[i]) {
			auto const& name = std::get<0>(row);
			if (!name.empty()) {
				auto value = std::get<2>(row)->GetValue().ToStdWstring();
				if (!value.empty()) {
					values[std::make_pair(name, static_cast<ParameterSection::type>(i))] = value;
				}
			}
		}
	}

	std::vector<ParameterTraits> const& parameterTraits = ExtraServerParameterTraits(protocol);
	for (auto const& trait : parameterTraits) {
		if (trait.section_ == ParameterSection::custom) {
			continue;
		}
		auto & parameters = extraParameters_[trait.section_];
		auto & it = paramIt[trait.section_];

		if (it == parameters.cend()) {
			it = InsertRow(parameters, trait.name_, trait.section_ == ParameterSection::credentials);
		}

		if (it == parameters.cend()) {
			continue;
		}

		std::get<0>(*it) = trait.name_;
		std::get<1>(*it)->Show();
		SetLabel(*std::get<1>(*it), protocol, trait.name_);

		auto * pValue = std::get<2>(*it);
		pValue->Show();
		auto valueIt = values.find(std::make_pair(trait.name_, trait.section_));
		if (valueIt != values.cend()) {
			pValue->ChangeValue(valueIt->second);
		}
		else {
			pValue->ChangeValue(wxString());
		}
		pValue->SetHint(trait.hint_);

		++it;
	}

	auto encSizer = xrc_call(parent_, "ID_ENCRYPTION", &wxWindow::GetContainingSizer);
	encSizer->Show(encSizer->GetItemCount() - 1, paramIt[ParameterSection::host] == extraParameters_[ParameterSection::host].cbegin());

	for (int i = 0; i < ParameterSection::section_count; ++i) {
		for (; paramIt[i] != extraParameters_[i].cend(); ++paramIt[i]) {
			std::get<0>(*paramIt[i]).clear();
			std::get<1>(*paramIt[i])->Hide();
			std::get<2>(*paramIt[i])->Hide();
		}
	}

	auto keyfileSizer = xrc_call(parent_, "ID_KEYFILE_DESC", &wxStaticText::GetContainingSizer);
	if (keyfileSizer) {
		keyfileSizer->CalcMin();
		keyfileSizer->Layout();
	}

	auto encryptionkeySizer = xrc_call(parent_, "ID_ENCRYPTIONKEY_DESC", &wxStaticText::GetContainingSizer);
	if (encryptionkeySizer) {
		encryptionkeySizer->CalcMin();
		encryptionkeySizer->Layout();
	}
}

bool GeneralSiteControls::UpdateSite(Site & site, bool silent)
{
	ServerProtocol protocol = GetProtocol();
	if (protocol == UNKNOWN) {
		// How can this happen?
		return false;
	}

	std::wstring user = xrc_call(parent_, "ID_USER", &wxTextCtrl::GetValue).ToStdWstring();
	std::wstring pw = xrc_call(parent_, "ID_PASS", &wxTextCtrl::GetValue).ToStdWstring();

	{
		std::wstring const host = xrc_call(parent_, "ID_HOST", &wxTextCtrl::GetValue).ToStdWstring();
		std::wstring const port = xrc_call(parent_, "ID_PORT", &wxTextCtrl::GetValue).ToStdWstring();

		if (host.empty()) {
			if (!silent) {
				XRCCTRL(parent_, "ID_HOST", wxTextCtrl)->SetFocus();
				wxMessageBoxEx(_("You have to enter a hostname."), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, wxGetTopLevelParent(&parent_));
			}
			return false;
		}


		Site parsedSite;
		std::wstring error;
		site.m_default_bookmark.m_remoteDir = CServerPath();
		if (!parsedSite.ParseUrl(host, port, user, pw, error, site.m_default_bookmark.m_remoteDir, protocol)) {
			if (!silent) {
				XRCCTRL(parent_, "ID_HOST", wxTextCtrl)->SetFocus();
				wxMessageBoxEx(error, _("Site Manager - Invalid data"), wxICON_EXCLAMATION, wxGetTopLevelParent(&parent_));
			}
			return false;
		}

		protocol = parsedSite.server.GetProtocol();
		site.server.SetProtocol(protocol);
		site.server.SetHost(parsedSite.server.GetHost(), parsedSite.server.GetPort());

		if (!parsedSite.server.GetUser().empty()) {
			user = parsedSite.server.GetUser();
		}
		if (!parsedSite.credentials.GetPass().empty()) {
			pw = parsedSite.credentials.GetPass();
		}
	}

	auto logon_type = GetLogonType();
	auto const supportedlogonTypes = GetSupportedLogonTypes(protocol);
	if (std::find(supportedlogonTypes.cbegin(), supportedlogonTypes.cend(), logon_type) == supportedlogonTypes.cend()) {
		logon_type = supportedlogonTypes.front();
	}

	if (COptions::Get()->GetOptionVal(OPTION_DEFAULT_KIOSKMODE) != 0 &&
			!predefined_ &&
	        (logon_type == LogonType::account || logon_type == LogonType::normal))
	{
		if (!silent) {
			wxString msg;
			if (COptions::Get()->OptionFromFzDefaultsXml(OPTION_DEFAULT_KIOSKMODE) && COptions::Get()->GetOptionVal(OPTION_DEFAULT_KIOSKMODE) != 0) {
				msg = _("Saving of password has been disabled by your system administrator.");
			}
			else {
				msg = _("Saving of passwords has been disabled by you.");
			}
			msg += _T("\n");
			msg += _("'Normal' and 'Account' logontypes are not available. Your entry has been changed to 'Ask for password'.");
			wxMessageBoxEx(msg, _("Site Manager - Cannot remember password"), wxICON_INFORMATION, wxGetTopLevelParent(&parent_));
		}
		logon_type = LogonType::ask;
	}
	site.SetLogonType(logon_type);

	// At this point we got:
	// - Valid protocol, host, port, logontype
	// - Optional remotePath
	// - Optional, unvalidated username, password

	if (!ProtocolHasUser(protocol) || logon_type == LogonType::anonymous) {
		user.clear();
	}
	else if (logon_type != LogonType::ask &&
	         logon_type != LogonType::interactive &&
	         user.empty())
	{
		// Require username for non-anonymous, non-ask logon type
		if (!silent) {
			XRCCTRL(parent_, "ID_USER", wxTextCtrl)->SetFocus();
			wxMessageBoxEx(_("You have to specify a user name"), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, wxGetTopLevelParent(&parent_));
		}
		return false;
	}

	// We don't allow username of only spaces, confuses both users and XML libraries
	if (!user.empty()) {
		bool space_only = true;
		for (auto const& c : user) {
			if (c != ' ') {
				space_only = false;
				break;
			}
		}
		if (space_only) {
			if (!silent) {
				XRCCTRL(parent_, "ID_USER", wxTextCtrl)->SetFocus();
				wxMessageBoxEx(_("Username cannot be a series of spaces"), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, wxGetTopLevelParent(&parent_));
			}
			return false;
		}
	}
	site.SetUser(user);

	// At this point username has been validated

	// Require account for account logon type
	if (logon_type == LogonType::account) {
		std::wstring account = XRCCTRL(parent_, "ID_ACCOUNT", wxTextCtrl)->GetValue().ToStdWstring();
		if (account.empty()) {
			if (!silent) {
				XRCCTRL(parent_, "ID_ACCOUNT", wxTextCtrl)->SetFocus();
				wxMessageBoxEx(_("You have to enter an account name"), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, wxGetTopLevelParent(&parent_));
			}
			return false;
		}
		site.credentials.account_ = account;
	}

	// In key file logon type, check that the provided key file exists
	if (logon_type == LogonType::key) {
		std::wstring keyFile = xrc_call(parent_, "ID_KEYFILE", &wxTextCtrl::GetValue).ToStdWstring();
		if (keyFile.empty()) {
			if (!silent) {
				wxMessageBoxEx(_("You have to enter a key file path"), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, wxGetTopLevelParent(&parent_));
				xrc_call(parent_, "ID_KEYFILE", &wxWindow::SetFocus);
			}
			return false;
		}

		// Check (again) that the key file is in the correct format since it might have been changed in the meantime
		CFZPuttyGenInterface cfzg(wxGetTopLevelParent(&parent_));

		std::wstring keyFileComment, keyFileData;
		if (!cfzg.LoadKeyFile(keyFile, silent, keyFileComment, keyFileData)) {
			if (!silent) {
				xrc_call(parent_, "ID_KEYFILE", &wxWindow::SetFocus);
			}
			return false;
		}

		site.credentials.keyFile_ = keyFile;
	}

	if (protocol == STORJ && logon_type == LogonType::normal) {
		std::wstring encryptionKey = xrc_call(parent_, "ID_ENCRYPTIONKEY", &wxTextCtrl::GetValue).ToStdWstring();

		bool encrypted = !xrc_call(parent_, "ID_PASS", &wxTextCtrl::GetHint).empty();
		if (encrypted) {
			if (pw.empty() != encryptionKey.empty()) {
				if (!silent) {
					wxMessageBoxEx(_("You cannot change password and encryption key individually if using a master password."), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, wxGetTopLevelParent(&parent_));
					xrc_call(parent_, "ID_ENCRYPTIONKEY", &wxWindow::SetFocus);
				}
				return false;
			}
		}
#if ENABLE_STORJ
		if (!encryptionKey.empty() || !encrypted) {
			CStorjKeyInterface validator(&parent_);
			if (!validator.ValidateKey(encryptionKey, false)) {
				if (!silent) {
					wxMessageBoxEx(_("You have to enter a valid encryption key"), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, wxGetTopLevelParent(&parent_));
					xrc_call(parent_, "ID_ENCRYPTIONKEY", &wxWindow::SetFocus);
				}
				return false;
			}
		}
#endif
		if (!pw.empty() || !site.credentials.encrypted_) {
			pw += '|';
			pw += encryptionKey;
		}
	}


	site.server.ClearExtraParameters();

	std::vector<ParameterTraits> const& parameterTraits = ExtraServerParameterTraits(protocol);
	for (auto const& trait : parameterTraits) {
		if (trait.section_ == ParameterSection::custom || trait.section_ == ParameterSection::credentials) {
			continue;
		}
		for (auto const& row : extraParameters_[trait.section_]) {
			if (std::get<0>(row) == trait.name_) {
				std::wstring value = std::get<2>(row)->GetValue().ToStdWstring();
				if (!(trait.flags_ & ParameterTraits::optional)) {
					if (value.empty()) {
						if (!silent) {
							std::get<2>(row)->SetFocus();
							wxMessageBoxEx(_("You need to enter a value."), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, wxGetTopLevelParent(&parent_));
						}
						return false;
					}
				}
				site.server.SetExtraParameter(trait.name_, value);
				break;
			}
		}

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

	return true;
}

LogonType GeneralSiteControls::GetLogonType() const
{
	return GetLogonTypeFromName(xrc_call(parent_, "ID_LOGONTYPE", &wxChoice::GetStringSelection).ToStdWstring());
}

void GeneralSiteControls::SetProtocol(ServerProtocol protocol)
{
	if (protocol == UNKNOWN) {
		protocol = FTP;
	}
	wxChoice* pProtocol = XRCCTRL(parent_, "ID_PROTOCOL", wxChoice);
	wxChoice* pEncryption = XRCCTRL(parent_, "ID_ENCRYPTION", wxChoice);
	wxStaticText* pEncryptionDesc = XRCCTRL(parent_, "ID_ENCRYPTION_DESC", wxStaticText);

	auto const entry = findGroup(protocol);
	if (entry.first != protocolGroups().cend()) {
		pEncryption->Clear();
		for (auto const& prot : entry.first->protocols) {
			std::wstring name = prot.second;
			if (!CServer::ProtocolHasFeature(prot.first, ProtocolFeature::Security)) {
				name += ' ';
				name += 0x26a0; // Unicode's warning emoji
				name += 0xfe0f; // Variant selector, makes it colorful
			}
			pEncryption->AppendString(name);
		}
		pEncryption->Show();
		pEncryptionDesc->Show();
		pEncryption->SetSelection(entry.second - entry.first->protocols.cbegin());
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
		auto const entry = findGroup(protocol);
		if (entry.first != protocolGroups().cend()) {
			mainProtocolListIndex_[protocol] = pProtocol->Append(entry.first->name);
			for (auto const& sub : entry.first->protocols) {
				mainProtocolListIndex_[sub.first] = mainProtocolListIndex_[protocol];
			}
		}
		else {
			mainProtocolListIndex_[protocol] = pProtocol->Append(CServer::GetProtocolName(protocol));
		}

		pProtocol->SetSelection(mainProtocolListIndex_[protocol]);
	}
	else {
		pProtocol->SetSelection(mainProtocolListIndex_[FTP]);
	}
	UpdateHostFromDefaults(GetProtocol());
}

ServerProtocol GeneralSiteControls::GetProtocol() const
{
	int const sel = xrc_call(parent_, "ID_PROTOCOL", &wxChoice::GetSelection);

	ServerProtocol protocol = UNKNOWN;
	for (auto const it : mainProtocolListIndex_) {
		if (it.second == sel) {
			protocol = it.first;
			break;
		}
	}

	auto const group = findGroup(protocol);
	if (group.first != protocolGroups().cend()) {
		int encSel = xrc_call(parent_, "ID_ENCRYPTION", &wxChoice::GetSelection);
		if (encSel < 0 || encSel >= static_cast<int>(group.first->protocols.size())) {
			encSel = 0;
		}
		protocol = group.first->protocols[encSel].first;
	}

	return protocol;
}


void GeneralSiteControls::UpdateHostFromDefaults(ServerProtocol const newProtocol)
{
	if (newProtocol != protocol_) {
		auto const oldDefault = std::get<0>(GetDefaultHost(protocol_));
		auto const newDefault = GetDefaultHost(newProtocol);

		std::wstring const host = xrc_call(parent_, "ID_HOST", &wxTextCtrl::GetValue).ToStdWstring();
		if (host.empty() || host == oldDefault) {
			xrc_call(parent_, "ID_HOST", &wxTextCtrl::ChangeValue, std::get<0>(newDefault));
		}
		xrc_call(parent_, "ID_HOST", &wxTextCtrl::SetHint, std::get<1>(newDefault));
	}
}

void GeneralSiteControls::SetControlState()
{
	xrc_call(parent_, "ID_HOST", &wxWindow::Enable, !predefined_);
	xrc_call(parent_, "ID_PORT", &wxWindow::Enable, !predefined_ && !(protocol_ == STORJ && logonType_ == LogonType::anonymous));
	xrc_call(parent_, "ID_PROTOCOL", &wxWindow::Enable, !predefined_);
	xrc_call(parent_, "ID_ENCRYPTION", &wxWindow::Enable, !predefined_);
	xrc_call(parent_, "ID_LOGONTYPE", &wxWindow::Enable, !predefined_);

	xrc_call(parent_, "ID_USER", &wxTextCtrl::Enable, !predefined_ && logonType_ != LogonType::anonymous);
	xrc_call(parent_, "ID_PASS", &wxTextCtrl::Enable, !predefined_ && (logonType_ == LogonType::normal || logonType_ == LogonType::account));
	xrc_call(parent_, "ID_ACCOUNT", &wxTextCtrl::Enable, !predefined_ && logonType_ == LogonType::account);
	xrc_call(parent_, "ID_KEYFILE", &wxTextCtrl::Enable, !predefined_ && logonType_ == LogonType::key);
	xrc_call(parent_, "ID_KEYFILE_BROWSE", &wxButton::Enable, !predefined_ && logonType_ == LogonType::key);
	xrc_call(parent_, "ID_ENCRYPTIONKEY", &wxTextCtrl::Enable, !predefined_ && logonType_ == LogonType::normal);
	xrc_call(parent_, "ID_ENCRYPTIONKEY_GENERATE", &wxButton::Enable, !predefined_ && logonType_ == LogonType::normal);

	for (int i = 0; i < ParameterSection::section_count; ++i) {
		for (auto & param : extraParameters_[i]) {
			std::get<2>(param)->Enable(!predefined_);
		}
	}
}

AdvancedSiteControls::AdvancedSiteControls(wxWindow & parent, DialogLayout const& lay, wxFlexGridSizer & sizer)
    : SiteControls(parent)
{
	if (!sizer.IsColGrowable(0)) {
		sizer.AddGrowableCol(0);
	}

	auto* row = lay.createFlex(0, 1);
	sizer.Add(row);

	row->Add(new wxStaticText(&parent, XRCID("ID_SERVERTYPE_LABEL"), _("Server &type:")), lay.valign);

	auto types = new wxChoice(&parent, XRCID("ID_SERVERTYPE"));
	row->Add(types, lay.valign);

	for (int i = 0; i < SERVERTYPE_MAX; ++i) {
		types->Append(CServer::GetNameFromServerType(static_cast<ServerType>(i)));
	}

	sizer.AddSpacer(0);
	sizer.Add(new wxCheckBox(&parent, XRCID("ID_BYPASSPROXY"), _("B&ypass proxy")));

	sizer.Add(new wxStaticLine(&parent), lay.grow);

	sizer.Add(new wxStaticText(&parent, -1, _("Default &local directory:")));

	row = lay.createFlex(0, 1);
	sizer.Add(row, lay.grow);
	row->AddGrowableCol(0);
	auto localDir = new wxTextCtrlEx(&parent, XRCID("ID_LOCALDIR"));
	row->Add(localDir, lay.valigng);
	auto browse = new wxButton(&parent, XRCID("ID_BROWSE"), _("&Browse..."));
	row->Add(browse, lay.valign);

	browse->Bind(wxEVT_BUTTON, [localDir, p = &parent](wxEvent const&) {
		wxDirDialog dlg(wxGetTopLevelParent(p), _("Choose the default local directory"), localDir->GetValue(), wxDD_NEW_DIR_BUTTON);
		if (dlg.ShowModal() == wxID_OK) {
			localDir->ChangeValue(dlg.GetPath());
		}
	});

	sizer.AddSpacer(0);
	sizer.Add(new wxStaticText(&parent, -1, _("Default r&emote directory:")));
	sizer.Add(new wxTextCtrlEx(&parent, XRCID("ID_REMOTEDIR")), lay.grow);
	sizer.AddSpacer(0);
	sizer.Add(new wxCheckBox(&parent, XRCID("ID_SYNC"), _("&Use synchronized browsing")));
	sizer.Add(new wxCheckBox(&parent, XRCID("ID_COMPARISON"), _("Directory comparison")));

	sizer.Add(new wxStaticLine(&parent), lay.grow);

	sizer.Add(new wxStaticText(&parent, -1, _("&Adjust server time, offset by:")));
	row = lay.createFlex(0, 1);
	sizer.Add(row);
	auto* hours = new wxSpinCtrlEx(&parent, XRCID("ID_TIMEZONE_HOURS"), wxString(), wxDefaultPosition, wxSize(lay.dlgUnits(26), -1));
	hours->SetRange(-24, 24);
	hours->SetMaxLength(3);
	row->Add(hours, lay.valign);
	row->Add(new wxStaticText(&parent, -1, _("Hours,")), lay.valign);
	auto* minutes = new wxSpinCtrlEx(&parent, XRCID("ID_TIMEZONE_MINUTES"), wxString(), wxDefaultPosition, wxSize(lay.dlgUnits(26), -1));
	minutes->SetRange(-59, 59);
	minutes->SetMaxLength(3);
	row->Add(minutes, lay.valign);
	row->Add(new wxStaticText(&parent, -1, _("Minutes")), lay.valign);
}

void AdvancedSiteControls::SetSite(Site const& site)
{
	xrc_call(parent_, "ID_SERVERTYPE", &wxWindow::Enable, !predefined_);
	xrc_call(parent_, "ID_BYPASSPROXY", &wxWindow::Enable, !predefined_);
	xrc_call(parent_, "ID_SYNC", &wxWindow::Enable, !predefined_);
	xrc_call(parent_, "ID_COMPARISON", &wxCheckBox::Enable, !predefined_);
	xrc_call(parent_, "ID_LOCALDIR", &wxWindow::Enable, !predefined_);
	xrc_call(parent_, "ID_BROWSE", &wxWindow::Enable, !predefined_);
	xrc_call(parent_, "ID_REMOTEDIR", &wxWindow::Enable, !predefined_);
	xrc_call(parent_, "ID_TIMEZONE_HOURS", &wxWindow::Enable, !predefined_);
	xrc_call(parent_, "ID_TIMEZONE_MINUTES", &wxWindow::Enable, !predefined_);

	if (site) {
		xrc_call(parent_, "ID_SERVERTYPE", &wxChoice::SetSelection, site.server.GetType());
		xrc_call(parent_, "ID_BYPASSPROXY", &wxCheckBox::SetValue, site.server.GetBypassProxy());
		xrc_call(parent_, "ID_LOCALDIR", &wxTextCtrl::ChangeValue, site.m_default_bookmark.m_localDir);
		xrc_call(parent_, "ID_REMOTEDIR", &wxTextCtrl::ChangeValue, site.m_default_bookmark.m_remoteDir.GetPath());
		xrc_call(parent_, "ID_SYNC", &wxCheckBox::SetValue, site.m_default_bookmark.m_sync);
		xrc_call(parent_, "ID_COMPARISON", &wxCheckBox::SetValue, site.m_default_bookmark.m_comparison);
		xrc_call<wxSpinCtrl, int>(parent_, "ID_TIMEZONE_HOURS", &wxSpinCtrl::SetValue, site.server.GetTimezoneOffset() / 60);
		xrc_call<wxSpinCtrl, int>(parent_, "ID_TIMEZONE_MINUTES", &wxSpinCtrl::SetValue, site.server.GetTimezoneOffset() % 60);
	}
	else {
		xrc_call(parent_, "ID_SERVERTYPE", &wxChoice::SetSelection, 0);
		xrc_call(parent_, "ID_BYPASSPROXY", &wxCheckBox::SetValue, false);
		xrc_call(parent_, "ID_SYNC", &wxCheckBox::SetValue, false);
		xrc_call(parent_, "ID_COMPARISON", &wxCheckBox::SetValue, false);
		xrc_call(parent_, "ID_LOCALDIR", &wxTextCtrl::ChangeValue, wxString());
		xrc_call(parent_, "ID_REMOTEDIR", &wxTextCtrl::ChangeValue, wxString());
		xrc_call<wxSpinCtrl, int>(parent_, "ID_TIMEZONE_HOURS", &wxSpinCtrl::SetValue, 0);
		xrc_call<wxSpinCtrl, int>(parent_, "ID_TIMEZONE_MINUTES", &wxSpinCtrl::SetValue, 0);
	}
}

void AdvancedSiteControls::SetControlVisibility(ServerProtocol protocol, LogonType)
{
	bool const hasServerType = CServer::ProtocolHasFeature(protocol, ProtocolFeature::ServerType);
	xrc_call(parent_, "ID_SERVERTYPE_LABEL", &wxWindow::Show, hasServerType);
	xrc_call(parent_, "ID_SERVERTYPE", &wxWindow::Show, hasServerType);
	auto * serverTypeSizer = xrc_call(parent_, "ID_SERVERTYPE_LABEL", &wxWindow::GetContainingSizer)->GetContainingWindow()->GetSizer();
	serverTypeSizer->CalcMin();
	serverTypeSizer->Layout();
}

bool AdvancedSiteControls::UpdateSite(Site & site, bool silent)
{
	ServerType serverType = DEFAULT;
	if (!site.m_default_bookmark.m_remoteDir.empty()) {
		if (site.server.HasFeature(ProtocolFeature::ServerType)) {
			serverType = site.m_default_bookmark.m_remoteDir.GetType();
		}
		else if (site.m_default_bookmark.m_remoteDir.GetType() != DEFAULT && site.m_default_bookmark.m_remoteDir.GetType() != UNIX) {
			site.m_default_bookmark.m_remoteDir = CServerPath();
		}
	}
	else {
		if (site.server.HasFeature(ProtocolFeature::ServerType)) {
			serverType = CServer::GetServerTypeFromName(xrc_call(parent_, "ID_SERVERTYPE", &wxChoice::GetStringSelection).ToStdWstring());
		}
	}

	site.server.SetType(serverType);

	if (xrc_call(parent_, "ID_BYPASSPROXY", &wxCheckBox::GetValue)) {
		site.server.SetBypassProxy(true);
	}
	else {
		site.server.SetBypassProxy(false);
	}

	if (site.m_default_bookmark.m_remoteDir.empty()) {
		std::wstring const remotePathRaw = XRCCTRL(parent_, "ID_REMOTEDIR", wxTextCtrl)->GetValue().ToStdWstring();
		if (!remotePathRaw.empty()) {
			site.m_default_bookmark.m_remoteDir.SetType(serverType);
			if (!site.m_default_bookmark.m_remoteDir.SetPath(remotePathRaw)) {
				if (!silent) {
					XRCCTRL(parent_, "ID_REMOTEDIR", wxTextCtrl)->SetFocus();
					wxMessageBoxEx(_("Default remote path cannot be parsed. Make sure it is a valid absolute path for the selected server type."), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, wxGetTopLevelParent(&parent_));
				}
				return false;
			}
		}
	}

	std::wstring const localPath = XRCCTRL(parent_, "ID_LOCALDIR", wxTextCtrl)->GetValue().ToStdWstring();
	site.m_default_bookmark.m_localDir = localPath;
	if (XRCCTRL(parent_, "ID_SYNC", wxCheckBox)->GetValue()) {
		if (site.m_default_bookmark.m_remoteDir.empty() || localPath.empty()) {
			if (!silent) {
				XRCCTRL(parent_, "ID_SYNC", wxCheckBox)->SetFocus();
				wxMessageBoxEx(_("You need to enter both a local and a remote path to enable synchronized browsing for this site."), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, wxGetTopLevelParent(&parent_));
			}
			return false;
		}
	}


	site.m_default_bookmark.m_sync = xrc_call(parent_, "ID_SYNC", &wxCheckBox::GetValue);
	site.m_default_bookmark.m_comparison = xrc_call(parent_, "ID_COMPARISON", &wxCheckBox::GetValue);

	int hours = xrc_call(parent_, "ID_TIMEZONE_HOURS", &wxSpinCtrl::GetValue);
	int minutes = xrc_call(parent_, "ID_TIMEZONE_MINUTES", &wxSpinCtrl::GetValue);

	site.server.SetTimezoneOffset(hours * 60 + minutes);

	return true;
}

CharsetSiteControls::CharsetSiteControls(wxWindow & parent, DialogLayout const& lay, wxFlexGridSizer & sizer)
    : SiteControls(parent)
{
	sizer.Add(new wxStaticText(&parent, -1, _("The server uses following charset encoding for filenames:")));
	auto rbAuto = new wxRadioButton(&parent, XRCID("ID_CHARSET_AUTO"), _("&Autodetect"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
	sizer.Add(rbAuto);
	sizer.Add(new wxStaticText(&parent, -1, _("Uses UTF-8 if the server supports it, else uses local charset.")), 0, wxLEFT, 18);

	auto rbUtf8 = new wxRadioButton(&parent, XRCID("ID_CHARSET_UTF8"), _("Force &UTF-8"));
	sizer.Add(rbUtf8);
	auto rbCustom = new wxRadioButton(&parent, XRCID("ID_CHARSET_CUSTOM"), _("Use &custom charset"));
	sizer.Add(rbCustom);

	auto * row = lay.createFlex(0, 1);
	row->Add(new wxStaticText(&parent, -1, _("&Encoding:")), lay.valign);
	auto * encoding = new wxTextCtrlEx(&parent, XRCID("ID_ENCODING"));
	row->Add(encoding, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 18);
	sizer.Add(row);
	sizer.AddSpacer(lay.dlgUnits(6));
	sizer.Add(new wxStaticText(&parent, -1, _("Using the wrong charset can result in filenames not displaying properly.")));

	rbAuto->Bind(wxEVT_RADIOBUTTON, [encoding](wxEvent const&){ encoding->Disable(); });
	rbUtf8->Bind(wxEVT_RADIOBUTTON, [encoding](wxEvent const&){ encoding->Disable(); });
	rbCustom->Bind(wxEVT_RADIOBUTTON, [encoding](wxEvent const&){ encoding->Enable(); });
}

void CharsetSiteControls::SetSite(Site const& site)
{
	xrc_call(parent_, "ID_CHARSET_AUTO", &wxWindow::Enable, !predefined_);
	xrc_call(parent_, "ID_CHARSET_UTF8", &wxWindow::Enable, !predefined_);
	xrc_call(parent_, "ID_CHARSET_CUSTOM", &wxWindow::Enable, !predefined_);
	xrc_call(parent_, "ID_ENCODING", &wxWindow::Enable, !predefined_);

	if (!site) {
		xrc_call(parent_, "ID_CHARSET_AUTO", &wxRadioButton::SetValue, true);
		xrc_call(parent_, "ID_ENCODING", &wxTextCtrl::ChangeValue, wxString());
		xrc_call(parent_, "ID_ENCODING", &wxTextCtrl::Enable, false);
	}
	else {
		switch (site.server.GetEncodingType()) {
		default:
		case ENCODING_AUTO:
			xrc_call(parent_, "ID_CHARSET_AUTO", &wxRadioButton::SetValue, true);
			break;
		case ENCODING_UTF8:
			xrc_call(parent_, "ID_CHARSET_UTF8", &wxRadioButton::SetValue, true);
			break;
		case ENCODING_CUSTOM:
			xrc_call(parent_, "ID_CHARSET_CUSTOM", &wxRadioButton::SetValue, true);
			break;
		}
		xrc_call(parent_, "ID_ENCODING", &wxTextCtrl::Enable, !predefined_ && site.server.GetEncodingType() == ENCODING_CUSTOM);
		xrc_call(parent_, "ID_ENCODING", &wxTextCtrl::ChangeValue, site.server.GetCustomEncoding());
	}
}

bool CharsetSiteControls::UpdateSite(Site & site, bool silent)
{
	if (CServer::ProtocolHasFeature(site.server.GetProtocol(), ProtocolFeature::Charset)) {
		if (xrc_call(parent_, "ID_CHARSET_UTF8", &wxRadioButton::GetValue)) {
			site.server.SetEncodingType(ENCODING_UTF8);
		}
		else if (xrc_call(parent_, "ID_CHARSET_CUSTOM", &wxRadioButton::GetValue)) {
			std::wstring encoding = xrc_call(parent_, "ID_ENCODING", &wxTextCtrl::GetValue).ToStdWstring();

			if (encoding.empty()) {
				if (!silent) {
					XRCCTRL(parent_, "ID_ENCODING", wxTextCtrl)->SetFocus();
					wxMessageBoxEx(_("Need to specify a character encoding"), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, wxGetTopLevelParent(&parent_));
				}
				return false;
			}

			site.server.SetEncodingType(ENCODING_CUSTOM, encoding);
		}
		else {
			site.server.SetEncodingType(ENCODING_AUTO);
		}
	}
	else {
		site.server.SetEncodingType(ENCODING_AUTO);
	}

	return true;
}

TransferSettingsSiteControls::TransferSettingsSiteControls(wxWindow & parent, DialogLayout const& lay, wxFlexGridSizer & sizer)
    : SiteControls(parent)
{
	sizer.Add(new wxStaticText(&parent, XRCID("ID_TRANSFERMODE_LABEL"), _("&Transfer mode:")));
	auto * row = lay.createFlex(0, 1);
	sizer.Add(row);
	row->Add(new wxRadioButton(&parent, XRCID("ID_TRANSFERMODE_DEFAULT"), _("D&efault"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP), lay.valign);
	row->Add(new wxRadioButton(&parent, XRCID("ID_TRANSFERMODE_ACTIVE"), _("&Active")), lay.valign);
	row->Add(new wxRadioButton(&parent, XRCID("ID_TRANSFERMODE_PASSIVE"), _("&Passive")), lay.valign);
	sizer.AddSpacer(0);

	auto limit = new wxCheckBox(&parent, XRCID("ID_LIMITMULTIPLE"), _("&Limit number of simultaneous connections"));
	sizer.Add(limit);
	row = lay.createFlex(0, 1);
	sizer.Add(row, 0, wxLEFT, lay.dlgUnits(10));
	row->Add(new wxStaticText(&parent, -1, _("&Maximum number of connections:")), lay.valign);
	auto * spin = new wxSpinCtrlEx(&parent, XRCID("ID_MAXMULTIPLE"), wxString(), wxDefaultPosition, wxSize(lay.dlgUnits(26), -1));
	spin->SetMaxLength(2);
	spin->SetRange(1, 10);
	row->Add(spin, lay.valign);

	limit->Bind(wxEVT_CHECKBOX, [spin](wxCommandEvent const& ev){ spin->Enable(ev.IsChecked()); });
}

void TransferSettingsSiteControls::SetSite(Site const& site)
{
	xrc_call(parent_, "ID_TRANSFERMODE_DEFAULT", &wxWindow::Enable, !predefined_);
	xrc_call(parent_, "ID_TRANSFERMODE_ACTIVE", &wxWindow::Enable, !predefined_);
	xrc_call(parent_, "ID_TRANSFERMODE_PASSIVE", &wxWindow::Enable, !predefined_);
	xrc_call(parent_, "ID_LIMITMULTIPLE", &wxWindow::Enable, !predefined_);

	if (!site) {
		xrc_call(parent_, "ID_TRANSFERMODE_DEFAULT", &wxRadioButton::SetValue, true);
		xrc_call(parent_, "ID_LIMITMULTIPLE", &wxCheckBox::SetValue, false);
		xrc_call(parent_, "ID_MAXMULTIPLE", &wxSpinCtrl::Enable, false);
		xrc_call<wxSpinCtrl, int>(parent_, "ID_MAXMULTIPLE", &wxSpinCtrl::SetValue, 1);
	}
	else {
		if (CServer::ProtocolHasFeature(site.server.GetProtocol(), ProtocolFeature::TransferMode)) {
			PasvMode pasvMode = site.server.GetPasvMode();
			if (pasvMode == MODE_ACTIVE) {
				xrc_call(parent_, "ID_TRANSFERMODE_ACTIVE", &wxRadioButton::SetValue, true);
			}
			else if (pasvMode == MODE_PASSIVE) {
				xrc_call(parent_, "ID_TRANSFERMODE_PASSIVE", &wxRadioButton::SetValue, true);
			}
			else {
				xrc_call(parent_, "ID_TRANSFERMODE_DEFAULT", &wxRadioButton::SetValue, true);
			}
		}

		int const maxMultiple = site.server.MaximumMultipleConnections();
		xrc_call(parent_, "ID_LIMITMULTIPLE", &wxCheckBox::SetValue, maxMultiple != 0);
		if (maxMultiple != 0) {
			xrc_call(parent_, "ID_MAXMULTIPLE", &wxSpinCtrl::Enable, !predefined_);
			xrc_call<wxSpinCtrl, int>(parent_, "ID_MAXMULTIPLE", &wxSpinCtrl::SetValue, maxMultiple);
		}
		else {
			xrc_call(parent_, "ID_MAXMULTIPLE", &wxSpinCtrl::Enable, false);
			xrc_call<wxSpinCtrl, int>(parent_, "ID_MAXMULTIPLE", &wxSpinCtrl::SetValue, 1);
		}

	}
}

bool TransferSettingsSiteControls::UpdateSite(Site & site, bool)
{
	if (CServer::ProtocolHasFeature(site.server.GetProtocol(), ProtocolFeature::TransferMode)) {
		if (xrc_call(parent_, "ID_TRANSFERMODE_ACTIVE", &wxRadioButton::GetValue)) {
			site.server.SetPasvMode(MODE_ACTIVE);
		}
		else if (xrc_call(parent_, "ID_TRANSFERMODE_PASSIVE", &wxRadioButton::GetValue)) {
			site.server.SetPasvMode(MODE_PASSIVE);
		}
		else {
			site.server.SetPasvMode(MODE_DEFAULT);
		}
	}
	else {
		site.server.SetPasvMode(MODE_DEFAULT);
	}

	if (xrc_call(parent_, "ID_LIMITMULTIPLE", &wxCheckBox::GetValue)) {
		site.server.MaximumMultipleConnections(xrc_call(parent_, "ID_MAXMULTIPLE", &wxSpinCtrl::GetValue));
	}
	else {
		site.server.MaximumMultipleConnections(0);
	}

	return true;
}

void TransferSettingsSiteControls::SetControlVisibility(ServerProtocol protocol, LogonType)
{
	bool const hasTransferMode = CServer::ProtocolHasFeature(protocol, ProtocolFeature::TransferMode);
	xrc_call(parent_, "ID_TRANSFERMODE_DEFAULT", &wxWindow::Show, hasTransferMode);
	xrc_call(parent_, "ID_TRANSFERMODE_ACTIVE", &wxWindow::Show, hasTransferMode);
	xrc_call(parent_, "ID_TRANSFERMODE_PASSIVE", &wxWindow::Show, hasTransferMode);
	auto* transferModeLabel = XRCCTRL(parent_, "ID_TRANSFERMODE_LABEL", wxStaticText);
	transferModeLabel->Show(hasTransferMode);
	transferModeLabel->GetContainingSizer()->CalcMin();
	transferModeLabel->GetContainingSizer()->Layout();
}


S3SiteControls::S3SiteControls(wxWindow & parent, DialogLayout const& lay, wxFlexGridSizer & sizer)
    : SiteControls(parent)
{
	if (!sizer.IsColGrowable(0)) {
		sizer.AddGrowableCol(0);
	}

	sizer.Add(new wxStaticText(&parent, -1, _("Server Side Encryption:")));

	auto none = new wxRadioButton(&parent, XRCID("ID_S3_NOENCRYPTION"), _("N&o encryption"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
	sizer.Add(none);

	auto aes = new wxRadioButton(&parent, XRCID("ID_S3_AES256"), _("&AWS S3 encryption"));
	sizer.Add(aes);

	auto kms = new wxRadioButton(&parent, XRCID("ID_S3_AWSKMS"), _("AWS &KMS encryption"));
	sizer.Add(kms);

	auto * row = lay.createFlex(2);
	row->AddGrowableCol(1);
	sizer.Add(row, 0, wxLEFT|wxGROW, lay.indent);
	row->Add(new wxStaticText(&parent, -1, _("&Select a key:")), lay.valign);
	auto * choice = new wxChoice(&parent, XRCID("ID_S3_KMSKEY"));
	choice->Append(_("Default (AWS/S3)"));
	choice->Append(_("Custom KMS ARN"));
	row->Add(choice, lay.valigng);
	row->Add(new wxStaticText(&parent, -1, _("C&ustom KMS ARN:")), lay.valign);
	row->Add(new wxTextCtrlEx(&parent, XRCID("ID_S3_CUSTOM_KMS")), lay.valigng);

	auto customer = new wxRadioButton(&parent, XRCID("ID_S3_CUSTOMER_ENCRYPTION"), _("Cu&stomer encryption"));
	sizer.Add(customer);
	row = lay.createFlex(2);
	row->AddGrowableCol(1);
	sizer.Add(row, 0, wxLEFT | wxGROW, lay.indent);
	row->Add(new wxStaticText(&parent, -1, _("Cus&tomer Key:")), lay.valign);
	row->Add(new wxTextCtrlEx(&parent, XRCID("ID_S3_CUSTOMER_KEY")), lay.valigng);

	auto l = [this](wxEvent const&) { SetControlState(); };
	none->Bind(wxEVT_RADIOBUTTON, l);
	aes->Bind(wxEVT_RADIOBUTTON, l);
	kms->Bind(wxEVT_RADIOBUTTON, l);
	customer->Bind(wxEVT_RADIOBUTTON, l);
	choice->Bind(wxEVT_CHOICE, l);
}

void S3SiteControls::SetControlState()
{
	bool enableKey{};
	bool enableKMS{};
	bool enableCustomer{};
	if (xrc_call(parent_, "ID_S3_AWSKMS", &wxRadioButton::GetValue)) {
		enableKey = true;
		if (xrc_call(parent_, "ID_S3_KMSKEY", &wxChoice::GetSelection) == static_cast<int>(s3_sse::KmsKey::CUSTOM)) {
			enableKMS = true;
		}
	}
	else if (xrc_call(parent_, "ID_S3_CUSTOMER_ENCRYPTION", &wxRadioButton::GetValue)) {
		enableCustomer = true;
	}
	xrc_call(parent_, "ID_S3_KMSKEY", &wxWindow::Enable, !predefined_ && enableKey);
	xrc_call(parent_, "ID_S3_CUSTOM_KMS", &wxWindow::Enable, !predefined_ && enableKMS);
	xrc_call(parent_, "ID_S3_CUSTOMER_KEY", &wxWindow::Enable, !predefined_ && enableCustomer);
}

void S3SiteControls::SetSite(Site const& site)
{
	xrc_call(parent_, "ID_S3_KMSKEY", &wxWindow::Enable, !predefined_);
	xrc_call(parent_, "ID_S3_NOENCRYPTION", &wxWindow::Enable, !predefined_);
	xrc_call(parent_, "ID_S3_AES256", &wxWindow::Enable, !predefined_);
	xrc_call(parent_, "ID_S3_AWSKMS", &wxWindow::Enable, !predefined_);
	xrc_call(parent_, "ID_S3_CUSTOMER_ENCRYPTION", &wxWindow::Enable, !predefined_);

	if (site.server.GetProtocol() == S3) {
		xrc_call(parent_, "ID_S3_KMSKEY", &wxChoice::SetSelection, static_cast<int>(s3_sse::KmsKey::DEFAULT));
		auto ssealgorithm = site.server.GetExtraParameter("ssealgorithm");
		if (ssealgorithm.empty()) {
			xrc_call(parent_, "ID_S3_NOENCRYPTION", &wxRadioButton::SetValue, true);
		}
		else if (ssealgorithm == "AES256") {
			xrc_call(parent_, "ID_S3_AES256", &wxRadioButton::SetValue, true);
		}
		else if (ssealgorithm == "aws:kms") {
			xrc_call(parent_, "ID_S3_AWSKMS", &wxRadioButton::SetValue, true);
			auto sseKmsKey = site.server.GetExtraParameter("ssekmskey");
			if (!sseKmsKey.empty()) {
				xrc_call(parent_, "ID_S3_KMSKEY", &wxChoice::SetSelection, static_cast<int>(s3_sse::KmsKey::CUSTOM));
				xrc_call(parent_, "ID_S3_CUSTOM_KMS", &wxTextCtrl::ChangeValue, sseKmsKey);
			}
			else {
				xrc_call(parent_, "ID_S3_KMSKEY", &wxChoice::SetSelection, static_cast<int>(s3_sse::KmsKey::DEFAULT));
			}
		}
		else if (ssealgorithm == "customer") {
			xrc_call(parent_, "ID_S3_CUSTOMER_ENCRYPTION", &wxRadioButton::SetValue, true);
			auto customerKey = site.server.GetExtraParameter("ssecustomerkey");
			xrc_call(parent_, "ID_S3_CUSTOMER_KEY", &wxTextCtrl::ChangeValue, customerKey);
		}
	}
}

bool S3SiteControls::UpdateSite(Site & site, bool silent)
{
	CServer & server = site.server;
	if (server.GetProtocol() == S3) {
		if (xrc_call(parent_, "ID_S3_NOENCRYPTION", &wxRadioButton::GetValue)) {
			server.ClearExtraParameter("ssealgorithm");
		}
		else if (xrc_call(parent_, "ID_S3_AES256", &wxRadioButton::GetValue)) {
			server.SetExtraParameter("ssealgorithm", L"AES256");
		}
		else if (xrc_call(parent_, "ID_S3_AWSKMS", &wxRadioButton::GetValue)) {
			server.SetExtraParameter("ssealgorithm", L"aws:kms");
			if (xrc_call(parent_, "ID_S3_KMSKEY", &wxChoice::GetSelection) == static_cast<int>(s3_sse::KmsKey::CUSTOM)) {
				auto keyId = xrc_call(parent_, "ID_S3_CUSTOM_KMS", &wxTextCtrl::GetValue).ToStdWstring();
				if (keyId.empty()) {
					if (!silent) {
						xrc_call(parent_, "ID_S3_CUSTOM_KMS", &wxWindow::SetFocus);
						wxMessageBoxEx(_("Custom KMS ARN id cannot be empty."), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, wxGetTopLevelParent(&parent_));
					}
					return false;
				}
				server.SetExtraParameter("ssekmskey", fz::to_wstring(xrc_call(parent_, "ID_S3_CUSTOM_KMS", &wxTextCtrl::GetValue)));
			}
		}
		else if (xrc_call(parent_, "ID_S3_CUSTOMER_ENCRYPTION", &wxRadioButton::GetValue)) {
			auto keyId = xrc_call(parent_, "ID_S3_CUSTOMER_KEY", &wxTextCtrl::GetValue).ToStdString();
			if (keyId.empty()) {
				if (!silent) {
					xrc_call(parent_, "ID_S3_CUSTOMER_KEY", &wxWindow::SetFocus);
					wxMessageBoxEx(_("Custom key cannot be empty."), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, wxGetTopLevelParent(&parent_));
				}
				return false;
			}

			std::string const base64prefix = "base64:";
			if (fz::starts_with(keyId, base64prefix)) {
				keyId = keyId.substr(base64prefix.size());
				keyId = fz::base64_decode_s(keyId);
			}

			if (keyId.size() != 32) {		// 256-bit encryption key
				if (!silent) {
					xrc_call(parent_, "ID_S3_CUSTOMER_KEY", &wxWindow::SetFocus);
					wxMessageBoxEx(_("Custom key length must be 256-bit."), _("Site Manager - Invalid data"), wxICON_EXCLAMATION, wxGetTopLevelParent(&parent_));
				}
				return false;
			}

			server.SetExtraParameter("ssealgorithm", L"customer");
			server.SetExtraParameter("ssecustomerkey", fz::to_wstring(xrc_call(parent_, "ID_S3_CUSTOMER_KEY", &wxTextCtrl::GetValue)));
		}
	}

	return true;
}
