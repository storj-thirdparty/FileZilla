#include <filezilla.h>
#include "led.h"
#include "filezillaapp.h"
#include "themeprovider.h"

#include <wx/dcclient.h>

DEFINE_EVENT_TYPE(fzEVT_UPDATE_LED_TOOLTIP)

BEGIN_EVENT_TABLE(CLed, wxWindow)
	EVT_PAINT(CLed::OnPaint)
	EVT_TIMER(wxID_ANY, CLed::OnTimer)
	EVT_ENTER_WINDOW(CLed::OnEnterWindow)
END_EVENT_TABLE()

#define LED_OFF 1
#define LED_ON 0

CLed::CLed(wxWindow *parent, unsigned int index)
	: m_index(index ? 1 : 0)
{
#if defined(__WXGTK20__) && !defined(__WXGTK3__)
	SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);
#endif

	wxSize const& size = CThemeProvider::GetIconSize(iconSizeTiny);
	Create(parent, -1, wxDefaultPosition, size);

	m_ledState = LED_OFF;

	m_timer.SetOwner(this);

	wxBitmap bmp = CThemeProvider::Get()->CreateBitmap(L"ART_LEDS", wxART_OTHER, size * 2);
	if (bmp.IsOk()) {
		m_leds[0] = bmp.GetSubBitmap(wxRect(0, index * size.y, size.x, size.y));
		m_leds[1] = bmp.GetSubBitmap(wxRect(size.x, index * size.y, size.x, size.y));
		m_loaded = true;
	}
}

void CLed::OnPaint(wxPaintEvent&)
{
	wxPaintDC dc(this);

	if (!m_loaded) {
		return;
	}

	dc.DrawBitmap(m_leds[m_ledState], 0, 0, true);
}

void CLed::Set()
{
	if (m_ledState != LED_ON) {
		m_ledState = LED_ON;
		Refresh();
	}
}

void CLed::Unset()
{
	if (m_ledState != LED_OFF) {
		m_ledState = LED_OFF;
		Refresh();
	}
}

void CLed::OnTimer(wxTimerEvent& event)
{
	if (!m_timer.IsRunning()) {
		return;
	}

	if (event.GetId() != m_timer.GetId()) {
		return;
	}

	if (!CFileZillaEngine::IsActive(static_cast<CFileZillaEngine::_direction>(m_index))) {
		Unset();
		m_timer.Stop();
	}
}

void CLed::OnEnterWindow(wxMouseEvent&)
{
	wxCommandEvent requestUpdateEvent(fzEVT_UPDATE_LED_TOOLTIP, GetId());
	requestUpdateEvent.SetEventObject(this);
	GetEventHandler()->ProcessEvent(requestUpdateEvent);
}

void CLed::Ping()
{
	if (!m_loaded) {
		return;
	}

	if (m_timer.IsRunning()) {
		return;
	}

	Set();
	m_timer.Start(100);
}
