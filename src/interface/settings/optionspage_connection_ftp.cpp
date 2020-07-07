#include <filezilla.h>
#include "../Options.h"
#include "settingsdialog.h"
#include "optionspage.h"
#include "optionspage_connection_ftp.h"
#include "../netconfwizard.h"

#include <wx/statbox.h>

struct COptionsPageConnectionFTP::impl final
{
	wxRadioButton* passive_{};
	wxRadioButton* active_{};
	wxCheckBox* fallback_{};
	wxCheckBox* keepalive_{};
};

COptionsPageConnectionFTP::COptionsPageConnectionFTP()
	: impl_(std::make_unique<impl>())
{
}

COptionsPageConnectionFTP::~COptionsPageConnectionFTP()
{
}

bool COptionsPageConnectionFTP::CreateControls(wxWindow* parent)
{
	auto const& lay = m_pOwner->layout();

	Create(parent);
	auto main = lay.createFlex(1);
	main->AddGrowableCol(0);
	SetSizer(main);

	{
		auto [box, inner] = lay.createStatBox(main, _("Overview"), 1);
		inner->Add(new wxStaticText(box, -1, _("For more detailed information about what these options do, please run the network configuration wizard.")));
		auto run = new wxButton(box, -1, _("&Run configuration wizard now..."));
		inner->Add(run);
		run->Bind(wxEVT_BUTTON, [this](wxCommandEvent const&) {
			CNetConfWizard wizard(GetParent(), m_pOptions, m_pOwner->GetEngineContext());
			if (!wizard.Load()) {
				return;
			}
			if (wizard.Run()) {
				ReloadSettings();
			}
		});
	}
	{
		auto [box, inner] = lay.createStatBox(main, _("Transfer Mode"), 1);
		impl_->passive_ = new wxRadioButton(box, -1, _("Pa&ssive (recommended)"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
		inner->Add(impl_->passive_);
		impl_->active_ = new wxRadioButton(box, -1, _("&Active"));
		inner->Add(impl_->active_);
		impl_->fallback_ = new wxCheckBox(box, -1, _("Allow &fall back to other transfer mode on failure"));
		inner->Add(impl_->fallback_);
		inner->Add(new wxStaticText(box, -1, _("If you have problems to retrieve directory listings or to transfer files, try to change the default transfer mode.")));
	}
	{
		auto [box, inner] = lay.createStatBox(main, _("FTP Keep-alive"), 1);
		impl_->keepalive_ = new wxCheckBox(box, -1, _("Send FTP &keep-alive commands"));
		inner->Add(impl_->keepalive_);
		inner->Add(new wxStaticText(box, -1, _("A proper server does not require this. Contact the server administrator if you need this.")));
	}
	return true;
}

bool COptionsPageConnectionFTP::LoadPage()
{
	const bool use_pasv = m_pOptions->GetOptionVal(OPTION_USEPASV) != 0;
	impl_->passive_->SetValue(use_pasv);
	impl_->active_->SetValue(!use_pasv);
	impl_->fallback_->SetValue(m_pOptions->GetOptionVal(OPTION_ALLOW_TRANSFERMODEFALLBACK) != 0);
	impl_->keepalive_->SetValue(m_pOptions->GetOptionVal(OPTION_FTP_SENDKEEPALIVE) != 0);
	return true;
}

bool COptionsPageConnectionFTP::SavePage()
{
	m_pOptions->SetOption(OPTION_USEPASV, impl_->passive_->GetValue() ? 1 : 0);
	m_pOptions->SetOption(OPTION_ALLOW_TRANSFERMODEFALLBACK, impl_->fallback_ ? 1 : 0);
	m_pOptions->SetOption(OPTION_FTP_SENDKEEPALIVE, impl_->keepalive_ ? 1 : 0);
	return true;
}
