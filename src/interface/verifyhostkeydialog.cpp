#include <filezilla.h>
#include "verifyhostkeydialog.h"
#include "dialogex.h"
#include "ipcmutex.h"

#include <libfilezilla/format.hpp>

std::vector<CVerifyHostkeyDialog::t_keyData> CVerifyHostkeyDialog::m_sessionTrustedKeys;

void CVerifyHostkeyDialog::ShowVerificationDialog(wxWindow* parent, CHostKeyNotification& notification)
{
	wxDialogEx dlg;
	bool loaded;
	if (notification.GetRequestID() == reqId_hostkey) {
		loaded = dlg.Load(parent, _T("ID_HOSTKEY"));
	}
	else {
		loaded = dlg.Load(parent, _T("ID_HOSTKEYCHANGED"));
	}
	if (!loaded) {
		notification.m_trust = false;
		notification.m_alwaysTrust = false;
		wxBell();
		return;
	}

	dlg.WrapText(&dlg, XRCID("ID_DESC"), 400);

	std::wstring const host = fz::sprintf(L"%s:%d", notification.GetHost(), notification.GetPort());
	dlg.SetChildLabel(XRCID("ID_HOST"), host);

	if (!notification.hostKeyAlgorithm.empty()) {
		dlg.SetChildLabel(XRCID("ID_HOSTKEYALGO"), notification.hostKeyAlgorithm);
	}
	std::wstring const fingerprints = fz::sprintf(L"SHA256: %s\nMD5: %s", notification.hostKeyFingerprintSHA256, notification.hostKeyFingerprintMD5);
	dlg.SetChildLabel(XRCID("ID_FINGERPRINT"), fingerprints);

	dlg.GetSizer()->Fit(&dlg);
	dlg.GetSizer()->SetSizeHints(&dlg);

	int res = dlg.ShowModal();

	if (res == wxID_OK) {
		notification.m_trust = true;
		notification.m_alwaysTrust = XRCCTRL(dlg, "ID_ALWAYS", wxCheckBox)->GetValue();

		t_keyData data;
		data.host = host;
		data.fingerprint = notification.hostKeyFingerprintSHA256;
		m_sessionTrustedKeys.push_back(data);
		return;
	}

	notification.m_trust = false;
	notification.m_alwaysTrust = false;
}

bool CVerifyHostkeyDialog::IsTrusted(CHostKeyNotification const& notification)
{
	std::wstring const host = fz::sprintf(L"%s:%d", notification.GetHost(), notification.GetPort());

	for (auto const& trusted : m_sessionTrustedKeys) {
		if (trusted.host == host && trusted.fingerprint == notification.hostKeyFingerprintSHA256) {
			return true;
		}
	}

	return false;
}
