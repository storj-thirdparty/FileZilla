#include <filezilla.h>
#include "../Options.h"
#include "settingsdialog.h"
#include "optionspage_passwords.h"
#include "../loginmanager.h"
#include "../recentserverlist.h"
#include "../state.h"
#include "../textctrlex.h"
#include "../xrc_helper.h"
#include <libfilezilla/util.hpp>

#include <wx/statbox.h>

bool COptionsPagePasswords::LoadPage()
{
	bool failure = false;

	auto onChange = [this](wxEvent const&) {
		bool checked = xrc_call(*this, "ID_PASSWORDS_USEMASTERPASSWORD", &wxRadioButton::GetValue);
		xrc_call(*this, "ID_MASTERPASSWORD", &wxControl::Enable, checked);
		xrc_call(*this, "ID_MASTERPASSWORD_REPEAT", &wxControl::Enable, checked);

	};
	XRCCTRL(*this, "ID_PASSWORDS_SAVE", wxEvtHandler)->Bind(wxEVT_RADIOBUTTON, onChange);
	XRCCTRL(*this, "ID_PASSWORDS_NOSAVE", wxEvtHandler)->Bind(wxEVT_RADIOBUTTON, onChange);
	XRCCTRL(*this, "ID_PASSWORDS_USEMASTERPASSWORD", wxEvtHandler)->Bind(wxEVT_RADIOBUTTON, onChange);

	bool const disabledByDefault = m_pOptions->OptionFromFzDefaultsXml(OPTION_DEFAULT_KIOSKMODE) && m_pOptions->GetOptionVal(OPTION_DEFAULT_KIOSKMODE) != 0;
	if (disabledByDefault || m_pOptions->GetOptionVal(OPTION_DEFAULT_KIOSKMODE) == 2) {
		xrc_call(*this, "ID_PASSWORDS_NOSAVE", &wxRadioButton::SetValue, true);
		xrc_call(*this, "ID_PASSWORDS_SAVE", &wxControl::Disable);
		xrc_call(*this, "ID_PASSWORDS_NOSAVE", &wxControl::Disable);
		xrc_call(*this, "ID_PASSWORDS_USEMASTERPASSWORD", &wxControl::Disable);
	}
	else {
		if (m_pOptions->GetOptionVal(OPTION_DEFAULT_KIOSKMODE) != 0) {
			xrc_call(*this, "ID_PASSWORDS_NOSAVE", &wxRadioButton::SetValue, true);
		}
		else {
			auto key = fz::public_key::from_base64(fz::to_utf8(m_pOptions->GetOption(OPTION_MASTERPASSWORDENCRYPTOR)));
			if (key) {
				xrc_call(*this, "ID_PASSWORDS_USEMASTERPASSWORD", &wxRadioButton::SetValue, true);

				// @translator: Keep this string as short as possible
				xrc_call(*this, "ID_MASTERPASSWORD", &wxTextCtrl::SetHint, _("Leave empty to keep existing password."));
			}
			else {
				xrc_call(*this, "ID_PASSWORDS_SAVE", &wxRadioButton::SetValue, true);
			}
		}
	}
	onChange(wxCommandEvent());

	return !failure;
}

bool COptionsPagePasswords::SavePage()
{
	int const old_kiosk_mode = m_pOptions->GetOptionVal(OPTION_DEFAULT_KIOSKMODE);
	auto const oldPub = fz::public_key::from_base64(fz::to_utf8(m_pOptions->GetOption(OPTION_MASTERPASSWORDENCRYPTOR)));

	bool const disabledByDefault = m_pOptions->OptionFromFzDefaultsXml(OPTION_DEFAULT_KIOSKMODE) && old_kiosk_mode != 0;
	if (disabledByDefault || m_pOptions->GetOptionVal(OPTION_DEFAULT_KIOSKMODE) == 2) {
		return true;
	}

	std::wstring const newPw = xrc_call(*this, "ID_MASTERPASSWORD", &wxTextCtrl::GetValue).ToStdWstring();

	bool const save = xrc_call(*this, "ID_PASSWORDS_SAVE", &wxRadioButton::GetValue);
	bool const useMaster = xrc_call(*this, "ID_PASSWORDS_USEMASTERPASSWORD", &wxRadioButton::GetValue);
	bool const forget = !save && !useMaster;

	if (save && !old_kiosk_mode && !oldPub) {
		// Not changing mode
		return true;
	}
	else if (forget && old_kiosk_mode) {
		// Not changing mode
		return true;
	}
	else if (useMaster && newPw.empty()) {
		// Keeping existing master password
		return true;
	}

	// Something is being changed

	CLoginManager loginManager;
	if (oldPub && !forget) {
		if (!loginManager.AskDecryptor(oldPub, true, true)) {
			return true;
		}
	}

	if (useMaster) {
		auto priv = fz::private_key::from_password(fz::to_utf8(newPw), fz::random_bytes(fz::private_key::salt_size));
		auto pub = priv.pubkey();
		if (!pub) {
			wxMessageBoxEx(_("Could not generate key"), _("Error"));
		}
		else {
			m_pOptions->SetOption(OPTION_DEFAULT_KIOSKMODE, 0);
			m_pOptions->SetOption(OPTION_MASTERPASSWORDENCRYPTOR, fz::to_wstring_from_utf8(pub.to_base64()));
		}
	}
	else {
		m_pOptions->SetOption(OPTION_DEFAULT_KIOSKMODE, save ? 0 : 1);
		m_pOptions->SetOption(OPTION_MASTERPASSWORDENCRYPTOR, std::wstring());
	}

	// Now actually change stored passwords
	{
		auto recentServers = CRecentServerList::GetMostRecentServers();
		for (auto& site : recentServers) {
			if (!forget) {
				loginManager.AskDecryptor(site.credentials.encrypted_, true, false);
				site.credentials.Unprotect(loginManager.GetDecryptor(site.credentials.encrypted_), true);
			}
			site.credentials.Protect();
		}
		CRecentServerList::SetMostRecentServers(recentServers);
	}

	for (auto state : *CContextManager::Get()->GetAllStates()) {
		auto site = state->GetLastSite();
		auto path = state->GetLastServerPath();
		if (!forget) {
			loginManager.AskDecryptor(site.credentials.encrypted_, true, false);
			site.credentials.Unprotect(loginManager.GetDecryptor(site.credentials.encrypted_), true);
		}
		site.credentials.Protect();
		state->SetLastSite(site, path);
	}

	m_pOptions->RequireCleanup();

	CSiteManager::Rewrite(loginManager, true);

	CContextManager::Get()->NotifyGlobalHandlers(STATECHANGE_REWRITE_CREDENTIALS, std::wstring(), &loginManager);

	return true;
}

bool COptionsPagePasswords::Validate()
{
	bool useMaster = xrc_call(*this, "ID_PASSWORDS_USEMASTERPASSWORD", &wxRadioButton::GetValue);
	if (useMaster) {
		wxString pw = xrc_call(*this, "ID_MASTERPASSWORD", &wxTextCtrl::GetValue);
		wxString repeat = xrc_call(*this, "ID_MASTERPASSWORD_REPEAT", &wxTextCtrl::GetValue);
		if (pw != repeat) {
			return DisplayError(_T("ID_MASTERPASSWORD"), _("The entered passwords are not the same."));
		}

		auto key = fz::public_key::from_base64(fz::to_utf8(m_pOptions->GetOption(OPTION_MASTERPASSWORDENCRYPTOR)));
		if (!key && pw.empty()) {
			return DisplayError(_T("ID_MASTERPASSWORD"), _("You need to enter a master password."));
		}

		if (!pw.empty() && pw.size() < 8) {
			return DisplayError(_T("ID_MASTERPASSWORD"), _("The master password needs to be at least 8 characters long."));
		}
	}
	return true;
}

bool COptionsPagePasswords::CreateControls(wxWindow* parent)
{
	auto const& layout = m_pOwner->layout();

	Create(parent);
	auto boxSizer = new wxStaticBoxSizer(wxVERTICAL, this, _("Passwords"));
	auto box = boxSizer->GetStaticBox();
	auto sizer = layout.createFlex(1);
	sizer->AddGrowableCol(0);
	boxSizer->Add(sizer, 0, wxGROW|wxALL, layout.border);

	sizer->Add(new wxRadioButton(box, XRCID("ID_PASSWORDS_SAVE"), _("Sav&e passwords"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP));
	sizer->Add(new wxRadioButton(box, XRCID("ID_PASSWORDS_NOSAVE"), _("D&o not save passwords")));
	sizer->Add(new wxRadioButton(box, XRCID("ID_PASSWORDS_USEMASTERPASSWORD"), _("Sa&ve passwords protected by a master password")));

	auto changeSizer = layout.createFlex(2);
	changeSizer->AddGrowableCol(1);
	changeSizer->Add(new wxStaticText(box, -1, _("Master password:")), layout.valign);
	auto pw = new wxTextCtrlEx(box, XRCID("ID_MASTERPASSWORD"), wxString(), wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
	changeSizer->Add(pw, layout.valigng);
	changeSizer->Add(new wxStaticText(box, -1, _("Repeat password:")), layout.valign);
	changeSizer->Add(new wxTextCtrlEx(box, XRCID("ID_MASTERPASSWORD_REPEAT"), wxString(), wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD), layout.valigng);

	sizer->Add(changeSizer, 0, wxGROW | wxLEFT, layout.dlgUnits(10));
	sizer->Add(new wxStaticText(box, -1, _("A lost master password cannot be recovered! Please thoroughly memorize your password.")), 0, wxLEFT, layout.dlgUnits(10));

	auto outer = new wxBoxSizer(wxVERTICAL);
	outer->Add(boxSizer, layout.grow);
	SetSizer(outer);

	return true;
}
