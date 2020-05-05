#include <filezilla.h>
#include "StatusView.h"
#include "Options.h"
#include "state.h"

#include <libfilezilla/util.hpp>

#include <wx/dcclient.h>
#include <wx/menu.h>

#define MAX_LINECOUNT 1000
#define LINECOUNT_REMOVAL 10

BEGIN_EVENT_TABLE(CStatusView, wxNavigationEnabled<wxWindow>)
EVT_SIZE(CStatusView::OnSize)
EVT_MENU(XRCID("ID_CLEARALL"), CStatusView::OnClear)
EVT_MENU(XRCID("ID_COPYTOCLIPBOARD"), CStatusView::OnCopy)
END_EVENT_TABLE()

class CFastTextCtrl final : public wxNavigationEnabled<wxTextCtrl>
{
public:
	CFastTextCtrl(wxWindow* parent)
	{
		Create(parent, -1, wxString(), wxDefaultPosition, wxDefaultSize,
			wxNO_BORDER | wxVSCROLL | wxTE_MULTILINE |
			wxTE_READONLY | wxTE_RICH | wxTE_RICH2 | wxTE_NOHIDESEL |
			wxTAB_TRAVERSAL);

		SetBackgroundStyle(wxBG_STYLE_SYSTEM);
	}
#ifdef __WXMSW__
	// wxTextCtrl::Remove is somewhat slow, this is a faster version
	virtual void Remove(long from, long to)
	{
		DoSetSelection(from, to, false);

		m_updatesCount = -2; // suppress any update event
		::SendMessage((HWND)GetHandle(), EM_REPLACESEL, 0, (LPARAM)_T(""));
	}

	void AppendText(std::wstring const& text, int lineCount, const CHARFORMAT2& cf)
	{
		HWND hwnd = (HWND)GetHWND();

		CHARRANGE range;
		range.cpMin = GetLastPosition();
		range.cpMax = range.cpMin;
		::SendMessage(hwnd, EM_EXSETSEL, 0, (LPARAM)&range);
		::SendMessage(hwnd, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
		m_updatesCount = -2; // suppress any update event
		::SendMessage(hwnd, EM_REPLACESEL, 0, reinterpret_cast<LPARAM>(text.c_str()));
		::SendMessage(hwnd, EM_LINESCROLL, (WPARAM)0, (LPARAM)lineCount);
	}
#endif

#ifndef __WXMAC__
	void SetDefaultColor(const wxColour& color)
	{
		m_defaultStyle.SetTextColour(color);
	}
#endif

	DECLARE_EVENT_TABLE()

	void OnText(wxCommandEvent&)
	{
		// Do nothing here.
		// Having this event handler prevents the event from propagating up the
		// window hierarchy which saves a few CPU cycles.
	}

#ifdef __WXMAC__
	void OnChar(wxKeyEvent& event)
	{
		if (event.GetKeyCode() != WXK_TAB) {
			event.Skip();
			return;
		}

		HandleAsNavigationKey(event);
	}
#endif
};

BEGIN_EVENT_TABLE(CFastTextCtrl, wxNavigationEnabled<wxTextCtrl>)
	EVT_TEXT(wxID_ANY, CFastTextCtrl::OnText)
#ifdef __WXMAC__
	EVT_CHAR_HOOK(CFastTextCtrl::OnChar)
#endif
END_EVENT_TABLE()


CStatusView::CStatusView(wxWindow* parent, wxWindowID id)
{
	Create(parent, id, wxDefaultPosition, wxDefaultSize, wxSUNKEN_BORDER);
	m_pTextCtrl = new CFastTextCtrl(this);

#ifdef __WXMAC__
	m_pTextCtrl->SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));
#else
	m_pTextCtrl->SetFont(GetFont());
#endif

	m_pTextCtrl->Connect(wxID_ANY, wxEVT_CONTEXT_MENU, wxContextMenuEventHandler(CStatusView::OnContextMenu), 0, this);
#ifdef __WXMSW__
	::SendMessage((HWND)m_pTextCtrl->GetHandle(), EM_SETOLECALLBACK, 0, 0);
#endif

	InitDefAttr();

	m_shown = IsShown();

	SetBackgroundStyle(wxBG_STYLE_SYSTEM);

	RegisterOption(OPTION_MESSAGELOG_TIMESTAMP);
}

CStatusView::~CStatusView()
{
}

void CStatusView::OnSize(wxSizeEvent &)
{
	if (m_pTextCtrl) {
		wxSize s = GetClientSize();
		m_pTextCtrl->SetSize(0, 0, s.GetWidth(), s.GetHeight());
	}
}

void CStatusView::AddToLog(CLogmsgNotification && notification)
{
	AddToLog(notification.msgType, std::move(notification.msg), std::move(notification.time_));
}

void CStatusView::AddToLog(logmsg::type messagetype, std::wstring && message, fz::datetime const& time)
{
	if (!m_shown) {
		if (m_hiddenLines.size() >= MAX_LINECOUNT) {
			auto it = m_hiddenLines.begin();
			it->messagetype = messagetype;
			it->message = message;
			it->time = time;
			m_hiddenLines.splice(m_hiddenLines.end(), m_hiddenLines, it );
		}
		else {
			t_line line;
			line.messagetype = messagetype;
			line.message = message;
			line.time = time;
			m_hiddenLines.push_back(line);
		}
		return;
	}

	size_t const messageLength = message.size();

	// This does not clear storage
	m_formattedMessage.clear();

	if (m_nLineCount) {
#ifdef __WXMSW__
		m_formattedMessage = _T("\r\n");
#else
		m_formattedMessage = _T("\n");
#endif
	}

	if (m_nLineCount >= MAX_LINECOUNT) {
#ifndef __WXGTK__
		m_pTextCtrl->Freeze();
#endif //__WXGTK__
		int oldLength = 0;
		auto it = m_lineLengths.begin();
		for (int i = 0; i < LINECOUNT_REMOVAL; ++i) {
			oldLength += *(it++) + 1;
		}
		m_unusedLineLengths.splice(m_unusedLineLengths.end(), m_lineLengths, m_lineLengths.begin(), it);
		m_pTextCtrl->Remove(0, oldLength);
	}
#ifdef __WXMAC__
	if (m_pTextCtrl->GetInsertionPoint() != m_pTextCtrl->GetLastPosition()) {
		m_pTextCtrl->SetInsertionPointEnd();
	}
#endif

	uint64_t const cache_index = fz::bitscan(messagetype);

	size_t lineLength = m_attributeCache[cache_index].len + messageLength;

	if (m_showTimestamps) {
		if (time != m_lastTime) {
			m_lastTime = time;
#ifndef __WXMAC__
			m_lastTimeString = time.format(_T("%H:%M:%S\t"), fz::datetime::local);
#else
			// Tabs on OS X cannot be freely positioned
			m_lastTimeString = time.format(_T("%H:%M:%S "), fz::datetime::local);
#endif
		}
		m_formattedMessage += m_lastTimeString;
		lineLength += m_lastTimeString.size();
	}

#ifdef __WXMAC__
	m_pTextCtrl->SetDefaultStyle(m_attributeCache[cache_index].attr);
#elif __WXGTK__
	m_pTextCtrl->SetDefaultColor(m_attributeCache[cache_index].attr.GetTextColour());
#endif

	m_formattedMessage += m_attributeCache[cache_index].prefix;

	if (m_rtl) {
		// Unicode control characters that control reading direction
		const wxChar LTR_MARK = 0x200e;
		//const wxChar RTL_MARK = 0x200f;
		const wxChar LTR_EMBED = 0x202A;
		//const wxChar RTL_EMBED = 0x202B;
		//const wxChar POP = 0x202c;
		//const wxChar LTR_OVERRIDE = 0x202D;
		//const wxChar RTL_OVERRIDE = 0x202E;

		if (messagetype == logmsg::command || messagetype == logmsg::reply || messagetype >= logmsg::debug_warning) {
			// Commands, responses and debug message contain English text,
			// set LTR reading order for them.
			m_formattedMessage += LTR_MARK;
			m_formattedMessage += LTR_EMBED;
			lineLength += 2;
		}
	}

	m_formattedMessage += message;
#if defined(__WXGTK__)
	// AppendText always calls SetInsertionPointEnd, which is very expensive.
	// This check however is negligible.
	if (m_pTextCtrl->GetInsertionPoint() != m_pTextCtrl->GetLastPosition()) {
		m_pTextCtrl->AppendText(m_formattedMessage);
	}
	else {
		m_pTextCtrl->WriteText(m_formattedMessage);
	}
	#ifdef __WXGTK3__
		// Some smooth scrolling oddities prevent auto-scrolling. Manuall tell it to scroll.
		m_pTextCtrl->ShowPosition(m_pTextCtrl->GetInsertionPoint());
	#endif
#elif defined(__WXMAC__)
	m_pTextCtrl->WriteText(m_formattedMessage);
#else
	m_pTextCtrl->AppendText(m_formattedMessage, m_nLineCount, m_attributeCache[cache_index].cf);
#endif

	if (m_nLineCount >= MAX_LINECOUNT) {
		m_nLineCount -= LINECOUNT_REMOVAL - 1;
#ifndef __WXGTK__
		m_pTextCtrl->Thaw();
#endif
	}
	else {
		m_nLineCount++;
	}
	if (m_unusedLineLengths.empty()) {
		m_lineLengths.push_back(lineLength);
	}
	else {
		m_unusedLineLengths.front() = lineLength;
		m_lineLengths.splice(m_lineLengths.end(), m_unusedLineLengths, m_unusedLineLengths.begin());
	}
}

void CStatusView::InitDefAttr()
{
	m_showTimestamps = COptions::Get()->GetOptionVal(OPTION_MESSAGELOG_TIMESTAMP) != 0;
	m_lastTime = fz::datetime::now();
	m_lastTimeString = m_lastTime.format(_T("%H:%M:%S\t"), fz::datetime::local);

	// Measure withs of all types
	wxClientDC dc(this);

	int timestampWidth = 0;
	if (m_showTimestamps) {
		wxCoord width = 0;
		wxCoord height = 0;
#ifndef __WXMAC__
		dc.GetTextExtent(_T("88:88:88 "), &width, &height);
#else
		dc.GetTextExtent(_T("88:88:88 "), &width, &height);
#endif
		timestampWidth = width;
	}

	wxCoord width = 0;
	wxCoord height = 0;
	dc.GetTextExtent(_("Error:") + _T(" "), &width, &height);
	int maxPrefixWidth = width;
	dc.GetTextExtent(_("Command:") + _T(" "), &width, &height);
	if (width > maxPrefixWidth)
		maxPrefixWidth = width;
	dc.GetTextExtent(_("Response:") + _T(" "), &width, &height);
	if (width > maxPrefixWidth)
		maxPrefixWidth = width;
	dc.GetTextExtent(_("Trace:") + _T(" "), &width, &height);
	if (width > maxPrefixWidth)
		maxPrefixWidth = width;
	dc.GetTextExtent(_("Listing:") + _T(" "), &width, &height);
	if (width > maxPrefixWidth)
		maxPrefixWidth = width;
	dc.GetTextExtent(_("Status:") + _T(" "), &width, &height);
	if (width > maxPrefixWidth)
		maxPrefixWidth = width;

#ifdef __WXMAC__
	wxCoord spaceWidth;
	dc.GetTextExtent(_T(" "), &spaceWidth, &height);
#endif

	dc.SetMapMode(wxMM_LOMETRIC);

	int maxWidth = dc.DeviceToLogicalX(maxPrefixWidth) + 20;
	if (timestampWidth != 0) {
		timestampWidth = dc.DeviceToLogicalX(timestampWidth) + 20;
		maxWidth += timestampWidth;
	}
	wxArrayInt array;
#ifndef __WXMAC__
	if (timestampWidth != 0)
		array.Add(timestampWidth);
#endif
	array.Add(maxWidth);
	wxTextAttr defAttr;
	defAttr.SetTabs(array);
	defAttr.SetLeftIndent(0, maxWidth);
	m_pTextCtrl->SetDefaultStyle(defAttr);
#ifdef __WXMSW__
	m_pTextCtrl->SetStyle(0, 0, defAttr);
#endif

	const wxColour background = wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOX);
	const bool is_dark = background.Red() + background.Green() + background.Blue() < 384;

#ifdef __WXMSW__
	// Select something for EM_GETCHARFORMAT to work
	long oldSelectionFrom{-1};
	long oldSelectionTo{-1};
	m_pTextCtrl->GetSelection(&oldSelectionFrom, &oldSelectionTo);
	m_pTextCtrl->SetSelection(m_pTextCtrl->GetInsertionPoint(), m_pTextCtrl->GetInsertionPoint());
#endif

	for (size_t i = 0; i < sizeof(logmsg::type) * 8; ++i) {
		t_attributeCache& entry = m_attributeCache[i];
#ifndef __WXMAC__
		entry.attr = defAttr;
#endif
		switch (1ull << i) {
		case logmsg::error:
			entry.prefix = _("Error:").ToStdWstring();
			entry.attr.SetTextColour(wxColour(255, 0, 0));
			break;
		case logmsg::command:
			entry.prefix = _("Command:").ToStdWstring();
			if (is_dark) {
				entry.attr.SetTextColour(wxColour(128, 128, 255));
			}
			else {
				entry.attr.SetTextColour(wxColour(0, 0, 128));
			}
			break;
		case logmsg::reply:
			entry.prefix = _("Response:").ToStdWstring();
			if (is_dark) {
				entry.attr.SetTextColour(wxColour(128, 255, 128));
			}
			else {
				entry.attr.SetTextColour(wxColour(0, 128, 0));
			}
			break;
		case logmsg::debug_warning:
		case logmsg::debug_info:
		case logmsg::debug_verbose:
		case logmsg::debug_debug:
			entry.prefix = _("Trace:").ToStdWstring();
			if (is_dark) {
				entry.attr.SetTextColour(wxColour(255, 128, 255));
			}
			else {
				entry.attr.SetTextColour(wxColour(128, 0, 128));
			}
			break;
		case logmsg::listing:
			entry.prefix = _("Listing:").ToStdWstring();
			if (is_dark) {
				entry.attr.SetTextColour(wxColour(128, 255, 255));
			}
			else {
				entry.attr.SetTextColour(wxColour(0, 128, 128));
			}
			break;
		default:
			entry.prefix = _("Status:").ToStdWstring();
			entry.attr.SetTextColour(wxSystemSettings::GetColour(wxSYS_COLOUR_LISTBOXTEXT));
			break;
		}

#ifdef __WXMAC__
		// Fill with blanks to approach best size
		dc.GetTextExtent(entry.prefix, &width, &height);
		wxASSERT(width <= maxPrefixWidth);
		wxCoord spaces = (maxPrefixWidth - width) / spaceWidth;
		entry.prefix += std::wstring(spaces, ' ');
#endif
		entry.prefix += _T("\t");
		entry.len = entry.prefix.size();

#ifdef __WXMSW__
		m_pTextCtrl->SetStyle(m_pTextCtrl->GetInsertionPoint(), m_pTextCtrl->GetInsertionPoint(), entry.attr);
		entry.cf.cbSize = sizeof(CHARFORMAT2);
		::SendMessage((HWND)m_pTextCtrl->GetHWND(), EM_GETCHARFORMAT, SCF_SELECTION, (LPARAM)&entry.cf);
#endif
	}

	m_rtl = wxTheApp->GetLayoutDirection() == wxLayout_RightToLeft;

#ifdef __WXMSW__
	if (oldSelectionFrom != -1 && oldSelectionTo != -1) {
		m_pTextCtrl->SetSelection(oldSelectionFrom, oldSelectionTo);
	}
#endif
}

void CStatusView::OnContextMenu(wxContextMenuEvent&)
{
	wxMenu menu;
	menu.Append(XRCID("ID_MENU_SERVER_CMD"), _("&Enter custom command..."));

	menu.AppendSeparator();
	menu.Append(XRCID("ID_SHOW_DETAILED_LOG"), _("&Show detailed log"), wxString(), wxITEM_CHECK);
	menu.Append(XRCID("ID_COPYTOCLIPBOARD"), _("&Copy to clipboard"));
	menu.Append(XRCID("ID_CLEARALL"), _("C&lear all"));

	menu.Check(XRCID("ID_SHOW_DETAILED_LOG"), COptions::Get()->GetOptionVal(OPTION_LOGGING_SHOW_DETAILED_LOGS) != 0);

	CState* pState = CContextManager::Get()->GetCurrentContext();
	if (pState) {
		auto pItem = menu.FindItem(XRCID("ID_MENU_SERVER_CMD"));
		Site const& site = pState->GetSite();
		if (!site || CServer::ProtocolHasFeature(site.server.GetProtocol(), ProtocolFeature::EnterCommand)) {
			pItem->Enable(true);
		}
		else {
			pItem->Enable(false);
		}
	}

	PopupMenu(&menu);

	COptions::Get()->SetOption(OPTION_LOGGING_SHOW_DETAILED_LOGS, menu.IsChecked(XRCID("ID_SHOW_DETAILED_LOG")) ? 1 : 0);
}

void CStatusView::OnClear(wxCommandEvent&)
{
	if (m_pTextCtrl) {
		m_pTextCtrl->Clear();
	}
	m_nLineCount = 0;
	m_lineLengths.clear();
}

void CStatusView::OnCopy(wxCommandEvent&)
{
	if (!m_pTextCtrl) {
		return;
	}

	long from, to;
	m_pTextCtrl->GetSelection(&from, &to);
	if (from != to) {
		m_pTextCtrl->Copy();
	}
	else {
		m_pTextCtrl->Freeze();
		m_pTextCtrl->SetSelection(-1, -1);
		m_pTextCtrl->Copy();
		m_pTextCtrl->SetSelection(from, to);
		m_pTextCtrl->Thaw();
	}
}

void CStatusView::SetFocus()
{
	m_pTextCtrl->SetFocus();
}

bool CStatusView::Show(bool show)
{
	m_shown = show;

	if (show && m_pTextCtrl) {
		if (m_hiddenLines.size() >= MAX_LINECOUNT) {
			m_pTextCtrl->Clear();
			m_nLineCount = 0;
			m_unusedLineLengths.splice(m_unusedLineLengths.end(), m_lineLengths, m_lineLengths.begin(), m_lineLengths.end());
		}

		for (auto & line : m_hiddenLines) {
			AddToLog(line.messagetype, std::move(line.message), line.time);
		}
		m_hiddenLines.clear();
	}

	return wxWindow::Show(show);
}

void CStatusView::OnOptionsChanged(changed_options_t const&)
{
	InitDefAttr();
}
