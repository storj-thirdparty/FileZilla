#include <filezilla.h>
#include "../Options.h"
#include "../sizeformatting.h"
#include "settingsdialog.h"
#include "optionspage.h"
#include "optionspage_transfer.h"
#include "textctrlex.h"
#include "wxext/spinctrlex.h"

#include <wx/statbox.h>

bool COptionsPageTransfer::CreateControls(wxWindow* parent)
{
	auto const& lay = m_pOwner->layout();

	Create(parent);
	auto main = lay.createFlex(1);
	main->AddGrowableCol(0);
	SetSizer(main);

	{
		auto [box, inner] = lay.createStatBox(main, _("Concurrent transfers"), 3);
		inner->Add(new wxStaticText(box, -1, _("Maximum simultaneous &transfers:")), lay.valign);
		auto spin = new wxSpinCtrlEx(box, XRCID("ID_NUMTRANSFERS"), wxString(), wxDefaultPosition, wxSize(lay.dlgUnits(26), -1));
		spin->SetRange(1, 10);
		spin->SetMaxLength(2);
		inner->Add(spin, lay.valign);
		inner->Add(new wxStaticText(box, -1, _("(1-10)")), lay.valign);
		inner->Add(new wxStaticText(box, -1, _("Limit for concurrent &downloads:")), lay.valign);
		spin = new wxSpinCtrlEx(box, XRCID("ID_NUMDOWNLOADS"), wxString(), wxDefaultPosition, wxSize(lay.dlgUnits(26), -1));
		spin->SetRange(0, 10);
		spin->SetMaxLength(2);
		inner->Add(spin, lay.valign);
		inner->Add(new wxStaticText(box, -1, _("(0 for no limit)")), lay.valign);
		inner->Add(new wxStaticText(box, -1, _("Limit for concurrent &uploads:")), lay.valign);
		spin = new wxSpinCtrlEx(box, XRCID("ID_NUMUPLOADS"), wxString(), wxDefaultPosition, wxSize(lay.dlgUnits(26), -1));
		spin->SetRange(0, 10);
		spin->SetMaxLength(2);
		inner->Add(spin, lay.valign);
		inner->Add(new wxStaticText(box, -1, _("(0 for no limit)")), lay.valign);
	}

	{
		auto [box, inner] = lay.createStatBox(main, _("Speed limits"), 1);

		auto enable = new wxCheckBox(box, XRCID("ID_ENABLE_SPEEDLIMITS"), _("&Enable speed limits"));
		inner->Add(enable);

		auto innermost = lay.createFlex(2);
		inner->Add(innermost);
		innermost->Add(new wxStaticText(box, -1, _("Download &limit:")), lay.valign);
		auto row = lay.createFlex(2);
		innermost->Add(row, lay.valign);
		auto dllimit = new wxTextCtrlEx(box, XRCID("ID_DOWNLOADLIMIT"), wxString(), wxDefaultPosition, wxSize(lay.dlgUnits(40), -1));
		dllimit->SetMaxLength(9);
		row->Add(dllimit, lay.valign);
		row->Add(new wxStaticText(box, -1, wxString::Format(_("(in %s/s)"), CSizeFormat::GetUnitWithBase(CSizeFormat::kilo, 1024))), lay.valign);

		innermost->Add(new wxStaticText(box, -1, _("Upload &limit:")), lay.valign);
		row = lay.createFlex(2);
		innermost->Add(row, lay.valign);
		auto ullimit = new wxTextCtrlEx(box, XRCID("ID_UPLOADLIMIT"), wxString(), wxDefaultPosition, wxSize(lay.dlgUnits(40), -1));
		ullimit->SetMaxLength(9);
		row->Add(ullimit, lay.valign);
		row->Add(new wxStaticText(box, -1, wxString::Format(_("(in %s/s)"), CSizeFormat::GetUnitWithBase(CSizeFormat::kilo, 1024))), lay.valign);

		innermost->Add(new wxStaticText(box, -1, _("&Burst tolerance:")), lay.valign);
		auto choice = new wxChoice(box, XRCID("ID_BURSTTOLERANCE"));
		choice->AppendString(_("Normal"));
		choice->AppendString(_("High"));
		choice->AppendString(_("Very high"));
		innermost->Add(choice, lay.valign);

		enable->Bind(wxEVT_CHECKBOX, [dllimit, ullimit, choice](wxCommandEvent const& ev) {
			dllimit->Enable(ev.IsChecked());
			ullimit->Enable(ev.IsChecked());
			choice->Enable(ev.IsChecked());
		});
	}
	
	{
		auto [box, inner] = lay.createStatBox(main, _("Filter invalid characters in filenames"), 1);
		inner->Add(new wxCheckBox(box, XRCID("ID_ENABLE_REPLACE"), _("Enable invalid character &filtering")));
		inner->Add(new wxStaticText(box, -1, _("When enabled, characters that are not supported by the local operating system in filenames are replaced if downloading such a file.")));
		auto innermost = lay.createFlex(2);
		inner->Add(innermost);
		innermost->Add(new wxStaticText(box, -1, _("&Replace invalid characters with:")), lay.valign);
		auto replace = new wxTextCtrlEx(box, XRCID("ID_REPLACE"), wxString(), wxDefaultPosition, wxSize(lay.dlgUnits(10), -1));
		replace->SetMaxLength(1);
		innermost->Add(replace, lay.valign);
#ifdef __WXMSW__
		wxString invalid = _T("\\ / : * ? \" < > |");
		wxString filtered = wxString::Format(_("The following characters will be replaced: %s"), invalid);
#else
		wxString invalid = _T("/");
		wxString filtered = wxString::Format(_("The following character will be replaced: %s"), invalid);
#endif
		inner->Add(new wxStaticText(box, -1, filtered));
	}

	{
		auto [box, inner] = lay.createStatBox(main, _("Preallocation"), 1);
		inner->Add(new wxCheckBox(box, XRCID("ID_PREALLOCATE"), _("Pre&allocate space before downloading")));
	}

	GetSizer()->Fit(this);

	return true;
}

bool COptionsPageTransfer::LoadPage()
{
	bool failure = false;

	bool enable_speedlimits = m_pOptions->GetOptionVal(OPTION_SPEEDLIMIT_ENABLE) != 0;
	SetCheck(XRCID("ID_ENABLE_SPEEDLIMITS"), enable_speedlimits, failure);

	wxTextCtrl* pTextCtrl = XRCCTRL(*this, "ID_DOWNLOADLIMIT", wxTextCtrl);
	if (!pTextCtrl) {
		return false;
	}
	pTextCtrl->ChangeValue(m_pOptions->GetOption(OPTION_SPEEDLIMIT_INBOUND));
	pTextCtrl->Enable(enable_speedlimits);

	pTextCtrl = XRCCTRL(*this, "ID_UPLOADLIMIT", wxTextCtrl);
	if (!pTextCtrl) {
		return false;
	}
	pTextCtrl->ChangeValue(m_pOptions->GetOption(OPTION_SPEEDLIMIT_OUTBOUND));
	pTextCtrl->Enable(enable_speedlimits);

	XRCCTRL(*this, "ID_NUMTRANSFERS", wxSpinCtrl)->SetValue(m_pOptions->GetOptionVal(OPTION_NUMTRANSFERS));
	XRCCTRL(*this, "ID_NUMDOWNLOADS", wxSpinCtrl)->SetValue(m_pOptions->GetOptionVal(OPTION_CONCURRENTDOWNLOADLIMIT));
	XRCCTRL(*this, "ID_NUMUPLOADS", wxSpinCtrl)->SetValue(m_pOptions->GetOptionVal(OPTION_CONCURRENTUPLOADLIMIT));

	SetChoice(XRCID("ID_BURSTTOLERANCE"), m_pOptions->GetOptionVal(OPTION_SPEEDLIMIT_BURSTTOLERANCE), failure);
	XRCCTRL(*this, "ID_BURSTTOLERANCE", wxChoice)->Enable(enable_speedlimits);

	pTextCtrl = XRCCTRL(*this, "ID_REPLACE", wxTextCtrl);
	pTextCtrl->ChangeValue(m_pOptions->GetOption(OPTION_INVALID_CHAR_REPLACE));

	SetCheckFromOption(XRCID("ID_ENABLE_REPLACE"), OPTION_INVALID_CHAR_REPLACE_ENABLE, failure);

	SetCheckFromOption(XRCID("ID_PREALLOCATE"), OPTION_PREALLOCATE_SPACE, failure);

	return !failure;
}

bool COptionsPageTransfer::SavePage()
{
	SetOptionFromCheck(XRCID("ID_ENABLE_SPEEDLIMITS"), OPTION_SPEEDLIMIT_ENABLE);

	m_pOptions->SetOption(OPTION_NUMTRANSFERS,				XRCCTRL(*this, "ID_NUMTRANSFERS", wxSpinCtrl)->GetValue());
	m_pOptions->SetOption(OPTION_CONCURRENTDOWNLOADLIMIT,	XRCCTRL(*this, "ID_NUMDOWNLOADS", wxSpinCtrl)->GetValue());
	m_pOptions->SetOption(OPTION_CONCURRENTUPLOADLIMIT,		XRCCTRL(*this, "ID_NUMUPLOADS", wxSpinCtrl)->GetValue());

	SetOptionFromText(XRCID("ID_DOWNLOADLIMIT"), OPTION_SPEEDLIMIT_INBOUND);
	SetOptionFromText(XRCID("ID_UPLOADLIMIT"), OPTION_SPEEDLIMIT_OUTBOUND);
	m_pOptions->SetOption(OPTION_SPEEDLIMIT_BURSTTOLERANCE, GetChoice(XRCID("ID_BURSTTOLERANCE")));
	SetOptionFromText(XRCID("ID_REPLACE"), OPTION_INVALID_CHAR_REPLACE);
	SetOptionFromCheck(XRCID("ID_ENABLE_REPLACE"), OPTION_INVALID_CHAR_REPLACE_ENABLE);

	SetOptionFromCheck(XRCID("ID_PREALLOCATE"), OPTION_PREALLOCATE_SPACE);

	return true;
}

bool COptionsPageTransfer::Validate()
{
	long tmp;
	wxTextCtrl* pCtrl;
	wxSpinCtrl* pSpinCtrl;
	int spinValue;

	pSpinCtrl = XRCCTRL(*this, "ID_NUMTRANSFERS", wxSpinCtrl);
	spinValue = pSpinCtrl->GetValue();
	if (spinValue < 1 || spinValue > 10) {
		return DisplayError(pSpinCtrl, _("Please enter a number between 1 and 10 for the number of concurrent transfers."));
	}

	pSpinCtrl = XRCCTRL(*this, "ID_NUMDOWNLOADS", wxSpinCtrl);
	spinValue = pSpinCtrl->GetValue();
	if (spinValue < 0 || spinValue > 10) {
		return DisplayError(pSpinCtrl, _("Please enter a number between 0 and 10 for the number of concurrent downloads."));
	}

	pSpinCtrl = XRCCTRL(*this, "ID_NUMUPLOADS", wxSpinCtrl);
	spinValue = pSpinCtrl->GetValue();
	if (spinValue < 0 || spinValue > 10) {
		return DisplayError(pSpinCtrl, _("Please enter a number between 0 and 10 for the number of concurrent uploads."));
	}

	pCtrl = XRCCTRL(*this, "ID_DOWNLOADLIMIT", wxTextCtrl);
	if (!pCtrl->GetValue().ToLong(&tmp) || (tmp < 0)) {
		const wxString unit = CSizeFormat::GetUnitWithBase(CSizeFormat::kilo, 1024);
		return DisplayError(pCtrl, wxString::Format(_("Please enter a download speed limit greater or equal to 0 %s/s."), unit));
	}

	pCtrl = XRCCTRL(*this, "ID_UPLOADLIMIT", wxTextCtrl);
	if (!pCtrl->GetValue().ToLong(&tmp) || (tmp < 0)) {
		const wxString unit = CSizeFormat::GetUnitWithBase(CSizeFormat::kilo, 1024);
		return DisplayError(pCtrl, wxString::Format(_("Please enter an upload speed limit greater or equal to 0 %s/s."), unit));
	}

	pCtrl = XRCCTRL(*this, "ID_REPLACE", wxTextCtrl);
	wxString replace = pCtrl->GetValue();
#ifdef __WXMSW__
	if (replace == _T("\\") ||
		replace == _T("/") ||
		replace == _T(":") ||
		replace == _T("*") ||
		replace == _T("?") ||
		replace == _T("\"") ||
		replace == _T("<") ||
		replace == _T(">") ||
		replace == _T("|"))
#else
	if (replace == _T("/"))
#endif
	{
		return DisplayError(pCtrl, _("You cannot replace an invalid character with another invalid character. Please enter a character that is allowed in filenames."));
	}

	return true;
}
