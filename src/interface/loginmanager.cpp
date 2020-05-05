#include <filezilla.h>
#include "loginmanager.h"

#include "dialogex.h"
#include "filezillaapp.h"
#include "Options.h"
#include "textctrlex.h"
#include "xrc_helper.h"

#include <algorithm>

CLoginManager CLoginManager::m_theLoginManager;

std::list<CLoginManager::t_passwordcache>::iterator CLoginManager::FindItem(CServer const& server, std::wstring const& challenge)
{
	return std::find_if(m_passwordCache.begin(), m_passwordCache.end(), [&](t_passwordcache const& item)
		{
			return item.host == server.GetHost() && item.port == server.GetPort() && item.user == server.GetUser() && item.challenge == challenge;
		}
	);
}

bool CLoginManager::GetPassword(Site & site, bool silent)
{
	bool const needsUser = ProtocolHasUser(site.server.GetProtocol()) && site.server.GetUser().empty() && (site.credentials.logonType_ == LogonType::ask || site.credentials.logonType_ == LogonType::interactive);

	if (site.credentials.logonType_ != LogonType::ask && !site.credentials.encrypted_ && !needsUser) {
		return true;
	}

	if (site.credentials.encrypted_) {
		auto priv = decryptors_.find(site.credentials.encrypted_);
		if (priv != decryptors_.end() && priv->second) {
			return site.credentials.Unprotect(priv->second);
		}

		if (!silent) {
			return DisplayDialogForEncrypted(site);
		}
	}
	else {
		auto it = FindItem(site.server, std::wstring());
		if (it != m_passwordCache.end()) {
			site.credentials.SetPass(it->password);
			return true;
		}

		if (!silent) {
			return DisplayDialog(site, std::wstring(), true);
		}
	}

	return false;
}


bool CLoginManager::GetPassword(Site & site, bool silent, std::wstring const& challenge, bool canRemember)
{
	if (canRemember) {
		auto it = FindItem(site.server, challenge);
		if (it != m_passwordCache.end()) {
			site.credentials.SetPass(it->password);
			return true;
		}
	}
	if (silent) {
		return false;
	}

	return DisplayDialog(site, challenge, canRemember);
}

bool CLoginManager::DisplayDialogForEncrypted(Site & site)
{
	assert(site.credentials.encrypted_);

	wxDialogEx pwdDlg;
	if (!pwdDlg.Create(wxGetApp().GetTopWindow(), -1, _("Enter master password"))) {
		return false;
	}
	auto & lay = pwdDlg.layout();
	auto * main = lay.createMain(&pwdDlg, 1);

	main->Add(new wxStaticText(&pwdDlg, -1, _("Please enter your master password to decrypt the password for this server:")));

	auto* inner = lay.createFlex(2);
	main->Add(inner);
	
	std::wstring const& name = site.GetName();
	if (!name.empty()) {
		inner->Add(new wxStaticText(&pwdDlg, -1, _("Name:")));
		inner->Add(new wxStaticText(&pwdDlg, -1, LabelEscape(name)));
	}

	inner->Add(new wxStaticText(&pwdDlg, -1, _("Host:")));
	inner->Add(new wxStaticText(&pwdDlg, -1, LabelEscape(site.Format(ServerFormat::with_optional_port))));

	inner->Add(new wxStaticText(&pwdDlg, -1, _("User:")));
	inner->Add(new wxStaticText(&pwdDlg, -1, LabelEscape(site.server.GetUser())));

	inner = lay.createFlex(2);
	main->Add(inner);

	inner->Add(new wxStaticText(&pwdDlg, -1, _("Key identifier:")));
	inner->Add(new wxStaticText(&pwdDlg, -1, fz::to_wstring(site.credentials.encrypted_.to_base64().substr(0, 8))));

	inner->Add(new wxStaticText(&pwdDlg, -1, _("Master &Password:")), lay.valign);

	auto* password = new wxTextCtrlEx(&pwdDlg, -1, wxString(), wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
	password->SetMinSize(wxSize(150, -1));
	password->SetFocus();
	inner->Add(password, lay.valign);

	auto* remember = new wxCheckBox(&pwdDlg, -1, _("&Remember master password until FileZilla is closed"));
	remember->SetValue(true);
	main->Add(remember);

	auto* buttons = lay.createButtonSizer(&pwdDlg, main, true);

	auto ok = new wxButton(&pwdDlg, wxID_OK, _("&OK"));
	ok->SetDefault();
	buttons->AddButton(ok);

	auto cancel = new wxButton(&pwdDlg, wxID_CANCEL, _("Cancel"));
	buttons->AddButton(cancel);

	buttons->Realize();
	
	pwdDlg.GetSizer()->Fit(&pwdDlg);
	pwdDlg.GetSizer()->SetSizeHints(&pwdDlg);

	while (true) {
		if (pwdDlg.ShowModal() != wxID_OK) {
			return false;
		}

		auto pass = fz::to_utf8(password->GetValue().ToStdWstring());
		auto key = fz::private_key::from_password(pass, site.credentials.encrypted_.salt_);

		if (key.pubkey() != site.credentials.encrypted_) {
			wxMessageBoxEx(_("Wrong master password entered, it cannot be used to decrypt this item."), _("Invalid input"), wxICON_EXCLAMATION);
			continue;
		}

		if (!site.credentials.Unprotect(key)) {
			wxMessageBoxEx(_("Failed to decrypt server password."), _("Invalid input"), wxICON_EXCLAMATION);
			continue;
		}

		if (remember->IsChecked()) {
			Remember(key);
		}
		break;
	}

	return true;
}

bool CLoginManager::DisplayDialog(Site & site, std::wstring const& challenge, bool canRemember)
{
	assert(!site.credentials.encrypted_);

	wxString title;
	wxString header;
	if (site.server.GetUser().empty()) {
		if (site.credentials.logonType_ == LogonType::interactive) {
			title = _("Enter username");
			header = _("Please enter a username for this server:");

			canRemember = false;
		}
		else {
			title = _("Enter username and password");
			header = _("Please enter username and password for this server:");
		}
	}
	else {
		title = _("Enter password");
		header = _("Please enter a password for this server:");
	}

	wxDialogEx pwdDlg;
	if (!pwdDlg.Create(wxGetApp().GetTopWindow(), -1, title)) {
		return false;
	}
	auto& lay = pwdDlg.layout();
	auto* main = lay.createMain(&pwdDlg, 1);
	
	main->Add(new wxStaticText(&pwdDlg, -1, header));

	auto* inner = lay.createFlex(2);
	main->Add(inner);

	std::wstring const& name = site.GetName();
	if (!name.empty()) {
		inner->Add(new wxStaticText(&pwdDlg, -1, _("Name:")));
		inner->Add(new wxStaticText(&pwdDlg, -1, LabelEscape(name)));
	}

	if (site.server.GetProtocol() == STORJ)
		inner->Add(new wxStaticText(&pwdDlg, -1, _("Satellite:")));
	else
		inner->Add(new wxStaticText(&pwdDlg, -1, _("Host:")));
	inner->Add(new wxStaticText(&pwdDlg, -1, LabelEscape(site.Format(ServerFormat::with_optional_port))));

	if (!site.server.GetUser().empty()) {
		if (site.server.GetProtocol() == STORJ)
			inner->Add(new wxStaticText(&pwdDlg, -1, _("API Key:")));
		else
			inner->Add(new wxStaticText(&pwdDlg, -1, _("User:")));
		inner->Add(new wxStaticText(&pwdDlg, -1, LabelEscape(site.server.GetUser())));
	}

	if (!challenge.empty()) {
		std::wstring displayChallenge = LabelEscape(fz::trimmed(challenge));
#ifdef FZ_WINDOWS
		fz::replace_substrings(displayChallenge, L"\n", L"\r\n");
#endif
		main->AddSpacer(0);
		main->Add(new wxStaticText(&pwdDlg, -1, _("Challenge:")));
		auto* challengeText = new wxTextCtrlEx(&pwdDlg, -1, displayChallenge, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY);
		challengeText->SetMinSize(wxSize(lay.dlgUnits(160), lay.dlgUnits(50)));
		challengeText->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));
		main->Add(challengeText, lay.grow);
	}

	main->AddSpacer(0);

	inner = lay.createFlex(2);
	main->Add(inner);

	wxTextCtrl* newUser{};
	if (site.server.GetUser().empty()) {
		inner->Add(new wxStaticText(&pwdDlg, -1, _("&User:")), lay.valign);
		newUser = new wxTextCtrlEx(&pwdDlg, -1, wxString());
		newUser->SetMinSize(wxSize(150, -1));
		newUser->SetFocus();
		inner->Add(newUser, lay.valign);
	}

	wxTextCtrl* password{};
	if (!site.server.GetUser().empty() || site.credentials.logonType_ != LogonType::interactive) {
		if (site.server.GetProtocol() == STORJ)
			inner->Add(new wxStaticText(&pwdDlg, -1, _("Encryption &key:")), lay.valign);
		else
			inner->Add(new wxStaticText(&pwdDlg, -1, _("&Password:")), lay.valign);
		password = new wxTextCtrlEx(&pwdDlg, -1, wxString(), wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
		password->SetMinSize(wxSize(150, -1));
		if (!newUser) {
			password->SetFocus();
		}
		inner->Add(password, lay.valign);

	}

	wxCheckBox* remember{};
	if (canRemember) {
		remember = new wxCheckBox(&pwdDlg, -1, _("&Remember password until FileZilla is closed"));
		remember->SetValue(true);
		main->Add(remember);
	}

	auto* buttons = lay.createButtonSizer(&pwdDlg, main, true);

	auto ok = new wxButton(&pwdDlg, wxID_OK, _("&OK"));
	ok->SetDefault();
	buttons->AddButton(ok);

	auto cancel = new wxButton(&pwdDlg, wxID_CANCEL, _("Cancel"));
	buttons->AddButton(cancel);

	buttons->Realize();

	pwdDlg.GetSizer()->Fit(&pwdDlg);
	pwdDlg.GetSizer()->SetSizeHints(&pwdDlg);

	while (true) {
		if (pwdDlg.ShowModal() != wxID_OK) {
			return false;
		}

		if (newUser) {
			auto user = newUser->GetValue().ToStdWstring();
			if (user.empty()) {
				wxMessageBoxEx(_("No username given."), _("Invalid input"), wxICON_EXCLAMATION);
				continue;
			}
			site.server.SetUser(user);
		}

/* FIXME?
	if (site.server.GetProtocol() == STORJ) {
		std::wstring encryptionKey = XRCCTRL(pwdDlg, "ID_ENCRYPTIONKEY", wxTextCtrl)->GetValue().ToStdWstring();
		if (encryptionKey.empty()) {
			wxMessageBoxEx(_("No encryption key given."), _("Invalid input"), wxICON_EXCLAMATION);
			continue;
		}
	}
*/
		if (password) {
			std::wstring pass = password->GetValue().ToStdWstring();
			if (site.server.GetProtocol() == STORJ) {
				std::wstring encryptionKey = xrc_call(pwdDlg, "ID_ENCRYPTIONKEY", &wxTextCtrl::GetValue).ToStdWstring();
				pass += L"|" + encryptionKey;
			}
			site.credentials.SetPass(pass);
		}

		if (remember && remember->IsChecked()) {
			RememberPassword(site, challenge);
		}
		break;
	}

	return true;
}

void CLoginManager::CachedPasswordFailed(CServer const& server, std::wstring const& challenge)
{
	auto it = FindItem(server, challenge);
	if (it != m_passwordCache.end()) {
		m_passwordCache.erase(it);
	}
}

void CLoginManager::RememberPassword(Site & site, std::wstring const& challenge)
{
	if (site.credentials.logonType_ == LogonType::anonymous) {
		return;
	}

	auto it = FindItem(site.server, challenge);
	if (it != m_passwordCache.end()) {
		it->password = site.credentials.GetPass();
	}
	else {
		t_passwordcache entry;
		entry.host = site.server.GetHost();
		entry.port = site.server.GetPort();
		entry.user = site.server.GetUser();
		entry.password = site.credentials.GetPass();
		entry.challenge = challenge;
		m_passwordCache.push_back(entry);
	}
}

bool CLoginManager::AskDecryptor(fz::public_key const& pub, bool allowForgotten, bool allowCancel)
{
	if (this == &CLoginManager::Get()) {
		return false;
	}

	if (!pub) {
		return false;
	}

	if (decryptors_.find(pub) != decryptors_.cend()) {
		return true;
	}

	wxDialogEx pwdDlg;
	if (!pwdDlg.Load(wxGetApp().GetTopWindow(), _T("ID_ENTEROLDMASTERPASSWORD"))) {
		return false;
	}
	xrc_call(pwdDlg, "ID_KEY_IDENTIFIER", &wxStaticText::SetLabel, pub.to_base64().substr(0, 8));

	if (!allowForgotten) {
		xrc_call(pwdDlg, "ID_FORGOT", &wxControl::Hide);
	}
	if (!allowCancel) {
		wxASSERT(allowForgotten);
		xrc_call(pwdDlg, "wxID_CANCEL", &wxControl::Disable);
	}

	while (true) {
		if (pwdDlg.ShowModal() != wxID_OK) {
			if (allowCancel) {
				return false;
			}
			continue;
		}

		bool const forgot = xrc_call(pwdDlg, "ID_FORGOT", &wxCheckBox::GetValue);
		if (!forgot) {
			auto pass = fz::to_utf8(xrc_call(pwdDlg, "ID_PASSWORD", &wxTextCtrl::GetValue).ToStdWstring());
			auto key = fz::private_key::from_password(pass, pub.salt_);

			if (key.pubkey() != pub) {
				wxMessageBoxEx(_("Wrong master password entered, it cannot be used to decrypt the stored passwords."), _("Invalid input"), wxICON_EXCLAMATION);
				continue;
			}
			decryptors_[pub] = key;
		}
		else {
			decryptors_[pub] = fz::private_key();
		}
		break;
	}

	return true;
}

fz::private_key CLoginManager::GetDecryptor(fz::public_key const& pub)
{
	auto it = decryptors_.find(pub);
	if (it != decryptors_.cend()) {
		return it->second;
	}

	return fz::private_key();
}

void CLoginManager::Remember(const fz::private_key &key)
{
	decryptors_[key.pubkey()] = key;
}
