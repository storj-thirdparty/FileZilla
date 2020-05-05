#include <filezilla.h>
#include "../Options.h"
#include "settingsdialog.h"
#include "optionspage_interface.h"
#include "../Mainfrm.h"
#include "../power_management.h"
#include "../xrc_helper.h"
#include <libfilezilla/util.hpp>

#include <wx/statbox.h>

BEGIN_EVENT_TABLE(COptionsPageInterface, COptionsPage)
EVT_CHECKBOX(XRCID("ID_FILEPANESWAP"), COptionsPageInterface::OnLayoutChange)
EVT_CHOICE(XRCID("ID_FILEPANELAYOUT"), COptionsPageInterface::OnLayoutChange)
EVT_CHOICE(XRCID("ID_MESSAGELOGPOS"), COptionsPageInterface::OnLayoutChange)
END_EVENT_TABLE()

bool COptionsPageInterface::LoadPage()
{
	bool failure = false;

	SetCheckFromOption(XRCID("ID_FILEPANESWAP"), OPTION_FILEPANE_SWAP, failure);
	SetChoice(XRCID("ID_FILEPANELAYOUT"), m_pOptions->GetOptionVal(OPTION_FILEPANE_LAYOUT), failure);

	SetChoice(XRCID("ID_MESSAGELOGPOS"), m_pOptions->GetOptionVal(OPTION_MESSAGELOG_POSITION), failure);

#ifndef __WXMAC__
	SetCheckFromOption(XRCID("ID_MINIMIZE_TRAY"), OPTION_MINIMIZE_TRAY, failure);
#endif

	SetCheckFromOption(XRCID("ID_PREVENT_IDLESLEEP"), OPTION_PREVENT_IDLESLEEP, failure);

	SetCheckFromOption(XRCID("ID_SPEED_DISPLAY"), OPTION_SPEED_DISPLAY, failure);

	if (!CPowerManagement::IsSupported()) {
		XRCCTRL(*this, "ID_PREVENT_IDLESLEEP", wxCheckBox)->Hide();
	}

	int const startupAction = m_pOptions->GetOptionVal(OPTION_STARTUP_ACTION);
	switch (startupAction) {
	default:
		xrc_call(*this, "ID_INTERFACE_STARTUP_NORMAL", &wxRadioButton::SetValue, true);
		break;
	case 1:
		xrc_call(*this, "ID_INTERFACE_STARTUP_SITEMANAGER", &wxRadioButton::SetValue, true);
		break;
	case 2:
		xrc_call(*this, "ID_INTERFACE_STARTUP_RESTORE", &wxRadioButton::SetValue, true);
		break;
	}

	int action = m_pOptions->GetOptionVal(OPTION_ALREADYCONNECTED_CHOICE);
	if (action & 2) {
		action = 1 + (action & 1);
	}
	else {
		action = 0;
	}
	SetChoice(XRCID("ID_NEWCONN_ACTION"), action, failure);

	m_pOwner->RememberOldValue(OPTION_MESSAGELOG_POSITION);
	m_pOwner->RememberOldValue(OPTION_FILEPANE_LAYOUT);
	m_pOwner->RememberOldValue(OPTION_FILEPANE_SWAP);

	return !failure;
}

bool COptionsPageInterface::SavePage()
{
	SetOptionFromCheck(XRCID("ID_FILEPANESWAP"), OPTION_FILEPANE_SWAP);
	m_pOptions->SetOption(OPTION_FILEPANE_LAYOUT, GetChoice(XRCID("ID_FILEPANELAYOUT")));

	m_pOptions->SetOption(OPTION_MESSAGELOG_POSITION, GetChoice(XRCID("ID_MESSAGELOGPOS")));

#ifndef __WXMAC__
	SetOptionFromCheck(XRCID("ID_MINIMIZE_TRAY"), OPTION_MINIMIZE_TRAY);
#endif

	SetOptionFromCheck(XRCID("ID_PREVENT_IDLESLEEP"), OPTION_PREVENT_IDLESLEEP);

	SetOptionFromCheck(XRCID("ID_SPEED_DISPLAY"), OPTION_SPEED_DISPLAY);

	int startupAction = 0;
	if (xrc_call(*this, "ID_INTERFACE_STARTUP_SITEMANAGER", &wxRadioButton::GetValue)) {
		startupAction = 1;
	}
	else if (xrc_call(*this, "ID_INTERFACE_STARTUP_RESTORE", &wxRadioButton::GetValue)) {
		startupAction = 2;
	}
	m_pOptions->SetOption(OPTION_STARTUP_ACTION, startupAction);

	int action = GetChoice(XRCID("ID_NEWCONN_ACTION"));
	if (!action) {
		action = m_pOptions->GetOptionVal(OPTION_ALREADYCONNECTED_CHOICE) & 1;
	}
	else {
		action += 1;
	}
	m_pOptions->SetOption(OPTION_ALREADYCONNECTED_CHOICE, action);

	return true;
}

void COptionsPageInterface::OnLayoutChange(wxCommandEvent&)
{
	m_pOptions->SetOption(OPTION_FILEPANE_LAYOUT, GetChoice(XRCID("ID_FILEPANELAYOUT")));
	m_pOptions->SetOption(OPTION_FILEPANE_SWAP, GetCheck(XRCID("ID_FILEPANESWAP")) ? 1 : 0);
	m_pOptions->SetOption(OPTION_MESSAGELOG_POSITION, GetChoice(XRCID("ID_MESSAGELOGPOS")));
}

bool COptionsPageInterface::CreateControls(wxWindow* parent)
{
	auto const& layout = m_pOwner->layout();

	Create(parent);
	auto outer = new wxBoxSizer(wxVERTICAL);

	auto boxSizer = new wxStaticBoxSizer(wxVERTICAL, this, _("Layout"));
	outer->Add(boxSizer, layout.grow);
	auto box = boxSizer->GetStaticBox();

	auto layoutSizer = layout.createFlex(1);
	boxSizer->Add(layoutSizer, 0, wxALL, layout.border);
	auto innerlayoutSizer = layout.createFlex(2);
	layoutSizer->Add(innerlayoutSizer);
	innerlayoutSizer->Add(new wxStaticText(box, -1, _("&Layout of file and directory panes:")), layout.valign);
	auto choice = new wxChoice(box, XRCID("ID_FILEPANELAYOUT"));
	choice->Append(_("Classic"));
	choice->Append(_("Explorer"));
	choice->Append(_("Widescreen"));
	choice->Append(_("Blackboard"));
	innerlayoutSizer->Add(choice, layout.valign);
	innerlayoutSizer->Add(new wxStaticText(box, -1, _("Message log positio&n:")), layout.valign);
	choice = new wxChoice(box, XRCID("ID_MESSAGELOGPOS"));
	choice->Append(_("Above the file lists"));
	choice->Append(_("Next to the transfer queue"));
	choice->Append(_("As tab in the transfer queue pane"));
	innerlayoutSizer->Add(choice, layout.valign);
	layoutSizer->Add(new wxCheckBox(box, XRCID("ID_FILEPANESWAP"), _("&Swap local and remote panes")));

	boxSizer = new wxStaticBoxSizer(wxVERTICAL, this, _("Behaviour"));
	outer->Add(boxSizer, layout.grow);
	box = boxSizer->GetStaticBox();

	auto behaviour = layout.createFlex(1);
	boxSizer->Add(behaviour, 0, wxALL, layout.border);
#ifndef __WXMAC__
	behaviour->Add(new wxCheckBox(box, XRCID("ID_MINIMIZE_TRAY"), _("&Minimize to tray")));
#endif
	behaviour->Add(new wxCheckBox(box, XRCID("ID_PREVENT_IDLESLEEP"), _("P&revent system from entering idle sleep during transfers and other operations")));
	behaviour->AddSpacer(0);
	behaviour->Add(new wxStaticText(box, -1, _("On startup of FileZilla:")));
	behaviour->Add(new wxRadioButton(box, XRCID("ID_INTERFACE_STARTUP_NORMAL"), _("S&tart normally"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP));
	behaviour->Add(new wxRadioButton(box, XRCID("ID_INTERFACE_STARTUP_SITEMANAGER"), _("S&how the Site Manager on startup")));
	behaviour->Add(new wxRadioButton(box, XRCID("ID_INTERFACE_STARTUP_RESTORE"), _("Restore ta&bs and reconnect")));
	behaviour->AddSpacer(0);
	behaviour->Add(new wxStaticText(box, -1, _("When st&arting a new connection while already connected:")));
	choice = new wxChoice(box, XRCID("ID_NEWCONN_ACTION"));
	choice->Append(_("Ask for action"));
	choice->Append(_("Connect in new tab"));
	choice->Append(_("Connect in current tab"));
	behaviour->Add(choice);

	boxSizer = new wxStaticBoxSizer(wxVERTICAL, this, _("Transfer Queue"));
	outer->Add(boxSizer, layout.grow);
	box = boxSizer->GetStaticBox();
	boxSizer->Add(new wxCheckBox(box, XRCID("ID_SPEED_DISPLAY"), _("&Display momentary transfer speed instead of average speed")), 0, wxALL, layout.border);

	SetSizer(outer);

	return true;
}
