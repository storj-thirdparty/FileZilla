#include <filezilla.h>
#include "filezillaapp.h"

#include "state.h"
#include "list_search_panel.h"

#include "textctrlex.h"
#include "themeprovider.h"

#include <wx/bmpbuttn.h>
#include <wx/dcclient.h>
#include <wx/menu.h>

wxWindowID const ID_SEARCH_TEXT = wxWindow::NewControlId();
wxWindowID const ID_OPTIONS_MENU_BUTTON = wxWindow::NewControlId();
wxWindowID const ID_CLOSE_BUTTON = wxWindow::NewControlId();
wxWindowID const ID_CASE_INSENSITIVE = wxWindow::NewControlId();
wxWindowID const ID_USE_REGEX = wxWindow::NewControlId();
wxWindowID const ID_INVERT_FILTER = wxWindow::NewControlId();

BEGIN_EVENT_TABLE(CListSearchPanel, wxWindow)
EVT_PAINT(CListSearchPanel::OnPaint)
EVT_TEXT(ID_SEARCH_TEXT, CListSearchPanel::OnText)
EVT_BUTTON(ID_OPTIONS_MENU_BUTTON, CListSearchPanel::OnOptions)
EVT_BUTTON(ID_CLOSE_BUTTON, CListSearchPanel::OnClose)
EVT_MENU(ID_CASE_INSENSITIVE, CListSearchPanel::OnCaseInsensitive)
EVT_MENU(ID_USE_REGEX, CListSearchPanel::OnUseRegex)
EVT_MENU(ID_INVERT_FILTER, CListSearchPanel::OnInvertFilter)
END_EVENT_TABLE()

CListSearchPanel::CListSearchPanel(wxWindow* parent, wxWindow* pListView, CState* pState, bool local)
	: m_listView(pListView)
	, m_pState(pState)
	, m_local(local)
{
	Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize);
	Hide();
	SetBackgroundStyle(wxBG_STYLE_SYSTEM);

	// sizer
	wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);

	// label
	auto label = new wxStaticText(this, wxID_ANY, _("Quick Search:"));
	sizer->Add(label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 5);

	// edit
	m_textCtrl = new wxTextCtrlEx(this, ID_SEARCH_TEXT, wxString(), wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	sizer->Add(m_textCtrl, 1, wxEXPAND | wxALL, 5);
	int const editHeight = m_textCtrl->GetSize().GetHeight();

	// options button
	wxBitmap dropdownBmp = CThemeProvider::Get()->CreateBitmap(L"ART_DROPDOWN", wxART_OTHER, CThemeProvider::GetIconSize(iconSizeTiny));
	wxSize bs(editHeight, editHeight);
	m_optionsButton = new wxBitmapButton(this, ID_OPTIONS_MENU_BUTTON, dropdownBmp, wxDefaultPosition, bs);
	m_optionsButton->SetToolTip(_("Options"));
	sizer->Add(m_optionsButton, 0, wxTOP | wxBOTTOM | wxRIGHT, 5);

	// close button
	wxBitmap closeBmp = CThemeProvider::Get()->CreateBitmap(L"ART_CLOSE", wxART_OTHER, CThemeProvider::GetIconSize(iconSizeTiny));
	auto closeButton = new wxBitmapButton(this, ID_CLOSE_BUTTON, closeBmp, wxDefaultPosition, bs);
	closeButton->SetToolTip(_("Close"));
	sizer->Add(closeButton, 0, wxTOP | wxBOTTOM | wxRIGHT, 5);

	SetSizerAndFit(sizer);

	Connect(wxEVT_CHAR_HOOK, wxKeyEventHandler(CListSearchPanel::OnKeyDown), 0, this);
}

bool CListSearchPanel::Show(bool show)
{
	bool ret = wxWindow::Show(show);

	wxSizeEvent evt;
	GetParent()->ProcessWindowEvent(evt);

	if (show) {
		m_textCtrl->SetFocus();
	}

	return ret;
}

void CListSearchPanel::ApplyFilter()
{
	if (m_text.empty()) {
		return;
	}

	CFilter filter;
	filter.filterFiles = true;
	filter.filterDirs = true;
	filter.matchType = m_invertFilter ? CFilter::all : CFilter::not_all;
	filter.matchCase = !m_caseInsensitive;

	CFilterCondition condition;
	if (!condition.set(filter_name, m_text.ToStdWstring(), m_useRegex ? 4 : 0, !m_caseInsensitive)) {
		return;
	}
	filter.filters.push_back(condition);

	CStateFilterManager& filterManager = m_pState->GetStateFilterManager();

	if (m_local) {
		filterManager.SetLocalFilter(filter);
	}
	else {
		filterManager.SetRemoteFilter(filter);
	}
	
	m_pState->NotifyHandlers(STATECHANGE_APPLYFILTER);
}

void CListSearchPanel::ResetFilter()
{
	CStateFilterManager& filterManager = m_pState->GetStateFilterManager();
	
	if (m_local) {
		filterManager.SetLocalFilter(CFilter());
	}
	else {
		filterManager.SetRemoteFilter(CFilter());
	}

	m_pState->NotifyHandlers(STATECHANGE_APPLYFILTER);
}

void CListSearchPanel::Close()
{
	Hide();

	ResetFilter();

	m_textCtrl->ChangeValue(wxString());

	if (m_listView) {
		m_listView->SetFocus();
	}
}

void CListSearchPanel::OnClose(wxCommandEvent&)
{
	Close();
}

void CListSearchPanel::OnPaint(wxPaintEvent&)
{
	wxPaintDC dc(this);

	wxSize const s = GetClientSize();

	dc.DrawLine(wxPoint(0, 0), wxPoint(s.GetWidth(), 0));
}

void CListSearchPanel::OnText(wxCommandEvent&)
{
	wxString text = m_textCtrl->GetValue();

	if (text != m_text) {
		m_text = text;

		if (text.IsEmpty()) {
			ResetFilter();
		}
		else {
			ApplyFilter();
		}
	}
}

void CListSearchPanel::OnOptions(wxCommandEvent&)
{
	if (!m_optionsMenu) {
		m_optionsMenu = new wxMenu;
		wxMenuItem* item = m_optionsMenu->AppendCheckItem(ID_CASE_INSENSITIVE, _("Case Insensitive"));
		item->Check();
		m_optionsMenu->AppendCheckItem(ID_USE_REGEX, _("Use Regular Expressions"));
		m_optionsMenu->AppendCheckItem(ID_INVERT_FILTER, _("Invert Filter"));
	}

	PopupMenu(m_optionsMenu, m_optionsButton->GetPosition());
}

void CListSearchPanel::OnCaseInsensitive(wxCommandEvent&)
{
	m_caseInsensitive = m_optionsMenu->IsChecked(ID_CASE_INSENSITIVE);
	ApplyFilter();
}

void CListSearchPanel::OnUseRegex(wxCommandEvent&)
{
	m_useRegex = m_optionsMenu->IsChecked(ID_USE_REGEX);
	ApplyFilter();
}

void CListSearchPanel::OnInvertFilter(wxCommandEvent&)
{
	m_invertFilter = m_optionsMenu->IsChecked(ID_INVERT_FILTER);
	ApplyFilter();
}

void CListSearchPanel::OnKeyDown(wxKeyEvent& event)
{
	if (event.GetKeyCode() == WXK_ESCAPE) {
		Close();
	}
	else if (event.GetKeyCode() == 'F' && event.GetModifiers() == wxMOD_CMD) {
		Show(true);
	}
	if (event.GetKeyCode() == WXK_DOWN) {
		wxCommandEvent evt;
		OnOptions(evt);
	}
	else {
		event.Skip();
	}
}
