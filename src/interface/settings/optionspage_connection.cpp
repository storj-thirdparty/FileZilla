#include <filezilla.h>
#include "settingsdialog.h"
#include "optionspage.h"
#include "optionspage_connection.h"

#include "../Options.h"
#include "../textctrlex.h"

#include <wx/statbox.h>

struct COptionsPageConnection::impl
{
	wxTextCtrlEx* timeout_{};
	wxTextCtrlEx* tries_{};
	wxTextCtrlEx* delay_{};
};

COptionsPageConnection::COptionsPageConnection()
	: impl_(std::make_unique<impl>())
{
}

COptionsPageConnection::~COptionsPageConnection()
{
}

bool COptionsPageConnection::CreateControls(wxWindow* parent)
{
	auto const& lay = m_pOwner->layout();

	Create(parent);
	auto main = lay.createFlex(1);
	main->AddGrowableCol(0);
	SetSizer(main);

	{
		auto [box, inner] = lay.createStatBox(main, _("Timeout"), 1);
		auto row = lay.createFlex(3);
		inner->Add(row);
		row->Add(new wxStaticText(box, -1, _("Time&out in seconds:")), lay.valign);
		impl_->timeout_ = new wxTextCtrlEx(box, -1, wxString(), wxDefaultPosition, wxSize(lay.dlgUnits(22), -1));
		impl_->timeout_->SetMaxLength(4);
		row->Add(impl_->timeout_, lay.valign);
		row->Add(new wxStaticText(box, -1, _("(10-9999, 0 to disable)")), lay.valign);
		inner->Add(new wxStaticText(box, -1, _("If no data is sent or received during an operation for longer than the specified time, the connection will be closed and FileZilla will try to reconnect.")));
	}
	{
		auto [box, inner] = lay.createStatBox(main, _("Reconnection settings"), 1);
		auto rows = lay.createFlex(3);
		inner->Add(rows);
		rows->Add(new wxStaticText(box, -1, _("&Maximum number of retries:")), lay.valign);
		impl_->tries_ = new wxTextCtrlEx(box, -1, wxString(), wxDefaultPosition, wxSize(lay.dlgUnits(22), -1));
		impl_->tries_->SetMaxLength(2);
		rows->Add(impl_->tries_, lay.valign);
		rows->Add(new wxStaticText(box, -1, _("(0-99)")), lay.valign);
		rows->Add(new wxStaticText(box, -1, _("&Delay between failed login attempts:")), lay.valign);
		impl_->delay_ = new wxTextCtrlEx(box, -1, wxString(), wxDefaultPosition, wxSize(lay.dlgUnits(22), -1));
		impl_->delay_->SetMaxLength(3);
		rows->Add(impl_->delay_, lay.valign);
		rows->Add(new wxStaticText(box, -1, _("(0-999 seconds)")), lay.valign);

		inner->Add(new wxStaticText(box, -1, _("Please note that some servers might ban you if you try to reconnect too often or in too short intervals.")));
	}
	return true;
}

bool COptionsPageConnection::LoadPage()
{
	impl_->timeout_->ChangeValue(fz::to_wstring(m_pOptions->GetOptionVal(OPTION_TIMEOUT)));
	impl_->tries_->ChangeValue(fz::to_wstring(m_pOptions->GetOptionVal(OPTION_RECONNECTCOUNT)));
	impl_->delay_->ChangeValue(fz::to_wstring(m_pOptions->GetOptionVal(OPTION_RECONNECTDELAY)));
	return true;
}

bool COptionsPageConnection::SavePage()
{
	m_pOptions->SetOption(OPTION_TIMEOUT, impl_->timeout_->GetValue().ToStdWstring());
	m_pOptions->SetOption(OPTION_RECONNECTCOUNT, impl_->tries_->GetValue().ToStdWstring());
	m_pOptions->SetOption(OPTION_RECONNECTDELAY, impl_->delay_->GetValue().ToStdWstring());
	return true;
}

bool COptionsPageConnection::Validate()
{
	auto const timeout = fz::to_integral<int>(impl_->timeout_->GetValue().ToStdWstring(), -1);
	if (timeout != 0 && (timeout < 10 || timeout > 9999)) {
		return DisplayError(impl_->timeout_, _("Please enter a timeout between 10 and 9999 seconds or 0 to disable timeouts."));
	}

	auto const retries = fz::to_integral<int>(impl_->tries_->GetValue().ToStdWstring(), -1);
	if (retries < 0 || retries > 99) {
		return DisplayError(impl_->tries_, _("Number of retries has to be between 0 and 99."));
	}

	auto const delay = fz::to_integral<int>(impl_->delay_->GetValue().ToStdWstring(), -1);
	if (delay < 0 || delay > 999) {
		return DisplayError(impl_->delay_, _("Delay between failed connection attempts has to be between 1 and 999 seconds."));
	}

	return true;
}
