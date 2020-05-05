#include <filezilla.h>

#include <libfilezilla/event_loop.hpp>
#include <libfilezilla/format.hpp>
#include <libfilezilla/iputils.hpp>
#include <libfilezilla/translate.hpp>
#include <libfilezilla/util.hpp>
#include "engine_context.h"
#include "netconfwizard.h"
#include "Options.h"
#include "dialogex.h"
#include "filezillaapp.h"
#include "externalipresolver.h"
#include "xrc_helper.h"

DECLARE_EVENT_TYPE(fzEVT_ON_EXTERNAL_IP_ADDRESS, -1)
DEFINE_EVENT_TYPE(fzEVT_ON_EXTERNAL_IP_ADDRESS)

BEGIN_EVENT_TABLE(CNetConfWizard, wxWizard)
EVT_WIZARD_PAGE_CHANGING(wxID_ANY, CNetConfWizard::OnPageChanging)
EVT_WIZARD_PAGE_CHANGED(wxID_ANY, CNetConfWizard::OnPageChanged)
EVT_BUTTON(XRCID("ID_RESTART"), CNetConfWizard::OnRestart)
EVT_WIZARD_FINISHED(wxID_ANY, CNetConfWizard::OnFinish)
EVT_TIMER(wxID_ANY, CNetConfWizard::OnTimer)
EVT_COMMAND(wxID_ANY, fzEVT_ON_EXTERNAL_IP_ADDRESS, CNetConfWizard::OnExternalIPAddress2)
END_EVENT_TABLE()

// Mark some strings used by wx as translatable
#if 0
fztranslate_mark("&Next >");
fztranslate_mark("< &Back");
#endif

CNetConfWizard::CNetConfWizard(wxWindow* parent, COptions* pOptions, CFileZillaEngineContext & engine_context)
	: fz::event_handler(engine_context.GetEventLoop())
	, engine_context_(engine_context)
	, m_parent(parent), m_pOptions(pOptions)
{
	m_timer.SetOwner(this);

	ResetTest();
}

CNetConfWizard::~CNetConfWizard()
{
	remove_handler();

	socket_.reset();
	delete m_pIPResolver;
	listen_socket_.reset();
	data_socket_.reset();
}

bool CNetConfWizard::Load()
{
	if (!Create(m_parent, wxID_ANY, _("Firewall and router configuration wizard"), wxNullBitmap, wxPoint(0, 0))) {
		return false;
	}

	wxSize minPageSize = GetPageAreaSizer()->GetMinSize();

	InitXrc(L"netconfwizard.xrc");
	for (int i = 1; i <= 7; ++i) {
		wxWizardPageSimple* page = new wxWizardPageSimple();
		bool res = wxXmlResource::Get()->LoadPanel(page, this, wxString::Format(_T("NETCONF_PANEL%d"), i));
		if (!res) {
			return false;
		}
		page->Show(false);

		m_pages.push_back(page);
	}
	for (unsigned int i = 0; i < (m_pages.size() - 1); ++i) {
		m_pages[i]->Chain(m_pages[i], m_pages[i + 1]);
	}

	GetPageAreaSizer()->Add(m_pages[0]);

	std::vector<wxWindow*> windows;
	for (unsigned int i = 0; i < m_pages.size(); ++i) {
		windows.push_back(m_pages[i]);
	}
	wxGetApp().GetWrapEngine()->WrapRecursive(windows, 1.7, "Netconf", wxSize(), minPageSize);

	CenterOnParent();

	// Load values

	switch (m_pOptions->GetOptionVal(OPTION_USEPASV))
	{
	default:
	case 1:
		XRCCTRL(*this, "ID_PASSIVE", wxRadioButton)->SetValue(true);
		break;
	case 0:
		XRCCTRL(*this, "ID_ACTIVE", wxRadioButton)->SetValue(true);
		break;
	}

	XRCCTRL(*this, "ID_FALLBACK", wxCheckBox)->SetValue(m_pOptions->GetOptionVal(OPTION_ALLOW_TRANSFERMODEFALLBACK) != 0);

	switch (m_pOptions->GetOptionVal(OPTION_PASVREPLYFALLBACKMODE))
	{
	default:
	case 0:
		XRCCTRL(*this, "ID_PASSIVE_FALLBACK1", wxRadioButton)->SetValue(true);
		break;
	case 1:
		XRCCTRL(*this, "ID_PASSIVE_FALLBACK2", wxRadioButton)->SetValue(true);
		break;
	}
	switch (m_pOptions->GetOptionVal(OPTION_EXTERNALIPMODE))
	{
	default:
	case 0:
		XRCCTRL(*this, "ID_ACTIVEMODE1", wxRadioButton)->SetValue(true);
		break;
	case 1:
		XRCCTRL(*this, "ID_ACTIVEMODE2", wxRadioButton)->SetValue(true);
		break;
	case 2:
		XRCCTRL(*this, "ID_ACTIVEMODE3", wxRadioButton)->SetValue(true);
		break;
	}
	switch (m_pOptions->GetOptionVal(OPTION_LIMITPORTS))
	{
	default:
	case 0:
		XRCCTRL(*this, "ID_ACTIVE_PORTMODE1", wxRadioButton)->SetValue(true);
		break;
	case 1:
		XRCCTRL(*this, "ID_ACTIVE_PORTMODE2", wxRadioButton)->SetValue(true);
		break;
	}
	XRCCTRL(*this, "ID_ACTIVE_PORTMIN", wxTextCtrl)->SetValue(wxString::Format(_T("%d"), m_pOptions->GetOptionVal(OPTION_LIMITPORTS_LOW)));
	XRCCTRL(*this, "ID_ACTIVE_PORTMAX", wxTextCtrl)->SetValue(wxString::Format(_T("%d"), m_pOptions->GetOptionVal(OPTION_LIMITPORTS_HIGH)));
	XRCCTRL(*this, "ID_ACTIVEIP", wxTextCtrl)->SetValue(m_pOptions->GetOption(OPTION_EXTERNALIP));
	XRCCTRL(*this, "ID_ACTIVERESOLVER", wxTextCtrl)->SetValue(m_pOptions->GetOption(OPTION_EXTERNALIPRESOLVER));
	XRCCTRL(*this, "ID_NOEXTERNALONLOCAL", wxCheckBox)->SetValue(m_pOptions->GetOptionVal(OPTION_NOEXTERNALONLOCAL) != 0);

	return true;
}

bool CNetConfWizard::Run()
{
	return RunWizard(m_pages.front());
}

void CNetConfWizard::OnPageChanging(wxWizardEvent& event)
{
	if (event.GetPage() == m_pages[3]) {
		int mode = XRCCTRL(*this, "ID_ACTIVEMODE1", wxRadioButton)->GetValue() ? 0 : (XRCCTRL(*this, "ID_ACTIVEMODE2", wxRadioButton)->GetValue() ? 1 : 2);
		if (mode == 1) {
			wxTextCtrl* control = XRCCTRL(*this, "ID_ACTIVEIP", wxTextCtrl);
			std::wstring ip = control->GetValue().ToStdWstring();
			if (ip.empty()) {
				wxMessageBoxEx(_("Please enter your external IP address"));
				control->SetFocus();
				event.Veto();
				return;
			}
			if (fz::get_address_type(ip) != fz::address_type::ipv4) {
				wxMessageBoxEx(_("You have to enter a valid IPv4 address."));
				control->SetFocus();
				event.Veto();
				return;
			}
		}
		else if (mode == 2) {
			wxTextCtrl* pResolver = XRCCTRL(*this, "ID_ACTIVERESOLVER", wxTextCtrl);
			wxString address = pResolver->GetValue();
			if (address.empty()) {
				wxMessageBoxEx(_("Please enter an URL where to get your external address from"));
				pResolver->SetFocus();
				event.Veto();
				return;
			}
		}
	}
	else if (event.GetPage() == m_pages[4]) {
		int mode = XRCCTRL(*this, "ID_ACTIVE_PORTMODE1", wxRadioButton)->GetValue() ? 0 : 1;
		if (mode) {
			wxTextCtrl* pPortMin = XRCCTRL(*this, "ID_ACTIVE_PORTMIN", wxTextCtrl);
			wxTextCtrl* pPortMax = XRCCTRL(*this, "ID_ACTIVE_PORTMAX", wxTextCtrl);
			wxString portMin = pPortMin->GetValue();
			wxString portMax = pPortMax->GetValue();

			long min = 0, max = 0;
			if (!portMin.ToLong(&min) || !portMax.ToLong(&max) ||
				min < 1024 || max > 65535 || min > max)
			{
				wxMessageBoxEx(_("Please enter a valid portrange."));
				pPortMin->SetFocus();
				event.Veto();
				return;
			}
		}
	}
	else if (event.GetPage() == m_pages[5] && !event.GetDirection()) {
		auto pNext = dynamic_cast<wxButton*>(FindWindow(wxID_FORWARD));
		if (pNext) {
			pNext->SetLabel(m_nextLabelText);
		}
	}
	else if (event.GetPage() == m_pages[5] && event.GetDirection()) {
		if (m_testDidRun) {
			return;
		}

		m_testDidRun = true;

		auto pNext = dynamic_cast<wxButton*>(FindWindow(wxID_FORWARD));
		if (pNext) {
			pNext->Disable();
		}
		auto pPrev = dynamic_cast<wxButton*>(FindWindow(wxID_BACKWARD));
		if (pPrev) {
			pPrev->Disable();
		}
		event.Veto();

		PrintMessage(fz::sprintf(fztranslate("Connecting to %s"), L"probe.filezilla-project.org"), 0);
		socket_ = std::make_unique<fz::socket>(engine_context_.GetThreadPool(), static_cast<fz::event_handler*>(this));
		m_recvBufferPos = 0;

		int res = socket_->connect(fzT("probe.filezilla-project.org"), 21);
		if (res) {
			PrintMessage(fz::sprintf(fztranslate("Connect failed: %s"), fz::socket_error_description(res)), 1);
			CloseSocket();
		}
	}
}

void CNetConfWizard::OnPageChanged(wxWizardEvent& event)
{
	if (event.GetPage() == m_pages[5]) {
		auto pNext = dynamic_cast<wxButton*>(FindWindow(wxID_FORWARD));
		if (pNext) {
			m_nextLabelText = pNext->GetLabel();
			pNext->SetLabel(_("&Test"));
		}
	}
	else if (event.GetPage() == m_pages[6]) {
		auto pPrev = dynamic_cast<wxButton*>(FindWindow(wxID_BACKWARD));
		if (pPrev) {
			pPrev->Disable();
		}
		auto pNext = dynamic_cast<wxButton*>(FindWindow(wxID_FORWARD));
		if (pNext) {
			pNext->SetFocus();
		}
	}
}

void CNetConfWizard::DoOnSocketEvent(fz::socket_event_source* s, fz::socket_event_flag t, int error)
{
	if (s == socket_.get()) {
		if (error) {
			OnClose();
			return;
		}
		switch (t)
		{
		case fz::socket_event_flag::read:
			OnReceive();
			break;
		case fz::socket_event_flag::write:
			OnSend();
			break;
		case fz::socket_event_flag::connection:
			OnConnect();
			break;
		default:
			break;
		}
	}
	else if (s == listen_socket_.get()) {
		if (error) {
			PrintMessage(fztranslate("Listen socket closed"), 1);
			CloseSocket();
			return;
		}
		switch (t) {
		case fz::socket_event_flag::connection:
			OnAccept();
			break;
		default:
			break;
		}
	}
	else if (s == data_socket_.get()) {
		if (error) {
			OnDataClose();
			return;
		}
		switch (t)
		{
		case fz::socket_event_flag::read:
			OnDataReceive();
			break;
		default:
			break;
		}
	}
}


void CNetConfWizard::OnSend()
{
	if (!sendBuffer_) {
		return;
	}

	if (!socket_) {
		return;
	}

	int error;
	int const written = socket_->write(sendBuffer_.get(), static_cast<int>(sendBuffer_.size()), error);
	if (written < 0) {
		if (error != EAGAIN) {
			PrintMessage(fztranslate("Failed to send command."), 1);
			CloseSocket();
		}
		return;
	}
	sendBuffer_.consume(static_cast<size_t>(written));
}

void CNetConfWizard::OnClose()
{
	CloseSocket();
}

void CNetConfWizard::OnConnect()
{
	PrintMessage(fztranslate("Connection established, waiting for welcome message."), 0);
	m_connectSuccessful = true;
}

void CNetConfWizard::OnReceive()
{
	while (true) {
		int error;
		int const read = socket_->read(m_recvBuffer + m_recvBufferPos, NETCONFBUFFERSIZE - m_recvBufferPos, error);
		if (read < 0) {
			if (error != EAGAIN) {
				PrintMessage(fztranslate("Could not receive data from server."), 1);
				CloseSocket();
			}
			return;
		}
		if (!read) {
			PrintMessage(fztranslate("Connection lost"), 1);
			CloseSocket();
			return;
		}

		m_recvBufferPos += read;

		if (m_recvBufferPos < 3) {
			return;
		}

		for (int i = 0; i < m_recvBufferPos - 1; ++i) {
			if (m_recvBuffer[i] == '\n') {
				m_testResult = servererror;
				PrintMessage(fztranslate("Invalid data received"), 1);
				CloseSocket();
				return;
			}
			if (m_recvBuffer[i] != '\r') {
				continue;
			}

			if (m_recvBuffer[i + 1] != '\n') {
				m_testResult = servererror;
				PrintMessage(fztranslate("Invalid data received"), 1);
				CloseSocket();
				return;
			}
			m_recvBuffer[i] = 0;

			if (!*m_recvBuffer) {
				m_testResult = servererror;
				PrintMessage(fztranslate("Invalid data received"), 1);
				CloseSocket();
				return;
			}

			ParseResponse(m_recvBuffer);

			if (!socket_) {
				return;
			}

			memmove(m_recvBuffer, m_recvBuffer + i + 2, m_recvBufferPos - i - 2);
			m_recvBufferPos -= i + 2;
			i = -1;
		}

		if (m_recvBufferPos == 200) {
			m_testResult = servererror;
			PrintMessage(fztranslate("Invalid data received"), 1);
			CloseSocket();
			return;
		}
	}
}

void CNetConfWizard::ParseResponse(const char* line)
{
	if (m_timer.IsRunning()) {
		m_timer.Stop();
	}

	size_t len = strlen(line);
	std::wstring msg = fz::to_wstring_from_utf8(line);
	std::wstring str = fztranslate("Response:");
	str += L" ";
	str += msg;
	PrintMessage(str, 3);

	if (len < 3) {
		m_testResult = servererror;
		PrintMessage(fztranslate("Server sent unexpected reply."), 1);
		CloseSocket();
		return;
	}
	if (line[3] && line[3] != ' ') {
		m_testResult = servererror;
		PrintMessage(fztranslate("Server sent unexpected reply."), 1);
		CloseSocket();
		return;
	}

	if (line[0] == '1') {
		return;
	}

	switch (m_state)
	{
	case 3:
		if (line[0] == '2') {
			break;
		}

		if (line[1] == '0' && line[2] == '1') {
			PrintMessage(fztranslate("Communication tainted by router or firewall"), 1);
			m_testResult = tainted;
			CloseSocket();
			return;
		}
		else if (line[1] == '1' && line[2] == '0') {
			PrintMessage(fztranslate("Wrong external IP address"), 1);
			m_testResult = mismatch;
			CloseSocket();
			return;
		}
		else if (line[1] == '1' && line[2] == '1') {
			PrintMessage(fztranslate("Wrong external IP address"), 1);
			PrintMessage(fztranslate("Communication tainted by router or firewall"), 1);
			m_testResult = mismatchandtainted;
			CloseSocket();
			return;
		}
		else {
			m_testResult = servererror;
			PrintMessage(fztranslate("Server sent unexpected reply."), 1);
			CloseSocket();
			return;
		}
		break;
	case 4:
		if (line[0] != '2') {
			m_testResult = servererror;
			PrintMessage(fztranslate("Server sent unexpected reply."), 1);
			CloseSocket();
			return;
		}
		else {
			const char* p = line + len;
			while (*(--p) != ' ') {
				if (*p < '0' || *p > '9') {
					m_testResult = servererror;
					PrintMessage(fztranslate("Server sent unexpected reply."), 1);
					CloseSocket();
					return;
				}
			}
			m_data = 0;
			while (*++p) {
				m_data = m_data * 10 + *p - '0';
			}
		}
		break;
	case 5:
		if (line[0] == '2') {
			break;
		}

		if (line[0] == '5' && line[1] == '0' && (line[2] == '1' || line[2] == '2')) {
			m_testResult = tainted;
			PrintMessage(fztranslate("PORT command tainted by router or firewall."), 1);
			CloseSocket();
			return;
		}

		m_testResult = servererror;
		PrintMessage(fztranslate("Server sent unexpected reply."), 1);
		CloseSocket();
		return;
	case 6:
		if (line[0] != '2' && line[0] != '3') {
			m_testResult = servererror;
			PrintMessage(fztranslate("Server sent unexpected reply."), 1);
			CloseSocket();
			return;
		}
		if (data_socket_) {
			if (gotListReply) {
				m_testResult = servererror;
				PrintMessage(fztranslate("Server sent unexpected reply."), 1);
				CloseSocket();
			}
			gotListReply = true;
			return;
		}
		break;
	default:
		if (line[0] != '2' && line[0] != '3') {
			m_testResult = servererror;
			PrintMessage(fztranslate("Server sent unexpected reply."), 1);
			CloseSocket();
			return;
		}
		break;
	}

	++m_state;

	SendNextCommand();
}

void CNetConfWizard::PrintMessage(std::wstring const& msg, int)
{
	XRCCTRL(*this, "ID_RESULTS", wxTextCtrl)->AppendText(msg + L"\n");
}

void CNetConfWizard::CloseSocket()
{
	if (!socket_) {
		return;
	}

	PrintMessage(fztranslate("Connection closed"), 0);

	auto pNext = dynamic_cast<wxButton*>(FindWindow(wxID_FORWARD));
	if (pNext) {
		pNext->Enable();
		pNext->SetLabel(m_nextLabelText);
	}

	wxString text[5];
	if (!m_connectSuccessful) {
		text[0] = _("Connection with the test server failed.");
		text[1] = _("Please check on https://filezilla-project.org/probe.php that the server is running and carefully check your settings again.");
		text[2] = _("If the problem persists, some router and/or firewall keeps blocking FileZilla.");
	}
	else {
		switch (m_testResult)
		{
		case unknown:
			text[0] = _("Connection with server got closed prematurely.");
			text[1] = _("Please ensure you have a stable internet connection and carefully check your settings again.");
			text[2] = _("If the problem persists, some router and/or firewall keeps interrupting the connection.");
			text[3] = _("See also: https://wiki.filezilla-project.org/Network_Configuration");
			break;
		case successful:
			PrintMessage(fztranslate("Test finished successfully"), 0);
			text[0] = _("Congratulations, your configuration seems to be working.");
			text[1] = _("You should have no problems connecting to other servers, file transfers should work properly.");
			text[2] = _("If you keep having problems with a specific server, the server itself or a remote router or firewall might be misconfigured. In this case try to toggle passive mode and contact the server administrator for help.");
			text[3] = _("Please run this wizard again should you change your network environment or in case you suddenly encounter problems with servers that did work previously.");
			text[4] = _("Click on Finish to save your configuration.");
			break;
		case servererror:
			text[0] = _("The server sent an unexpected or unrecognized reply.");
			text[1] = _("This means that some router and/or firewall is still interfering with FileZilla.");
			text[2] = _("Re-run the wizard and carefully check your settings and configure all routers and firewalls accordingly.");
			text[3] = _("See also: https://wiki.filezilla-project.org/Network_Configuration");
			break;
		case tainted:
			text[0] = _("Active mode FTP test failed. FileZilla knows the correct external IP address, but your router or firewall has misleadingly modified the sent address.");
			text[1] = _("Please update your firewall and make sure your router is using the latest available firmware. Furthermore, your router has to be configured properly. You will have to use manual port forwarding. Don't run your router in the so called 'DMZ mode' or 'game mode'. Things like protocol inspection or protocol specific 'fixups' have to be disabled");
			text[2] = _("If this problem stays, please contact your router manufacturer.");
			text[3] = _("Unless this problem gets fixed, active mode FTP will not work and passive mode has to be used.");
			if (XRCCTRL(*this, "ID_ACTIVE", wxRadioButton)->GetValue()) {
				XRCCTRL(*this, "ID_PASSIVE", wxRadioButton)->SetValue(true);
				text[3] += _T(" ");
				text[3] += _("Passive mode has been set as default transfer mode.");
			}
			break;
		case mismatchandtainted:
			text[0] = _("Active mode FTP test failed. FileZilla does not know the correct external IP address. In addition to that, your router has modified the sent address.");
			text[1] = _("Please enter your external IP address on the active mode page of this wizard. In case you have a dynamic address or don't know your external address, use the external resolver option.");
			text[2] = _("Please make sure your router is using the latest available firmware. Furthermore, your router has to be configured properly. You will have to use manual port forwarding. Don't run your router in the so called 'DMZ mode' or 'game mode'.");
			text[3] = _("If your router keeps changing the IP address, please contact your router manufacturer.");
			text[4] = _("Unless these problems get fixed, active mode FTP will not work and passive mode has to be used.");
			if (XRCCTRL(*this, "ID_ACTIVE", wxRadioButton)->GetValue()) {
				XRCCTRL(*this, "ID_PASSIVE", wxRadioButton)->SetValue(true);
				text[4] += _T(" ");
				text[4] += _("Passive mode has been set as default transfer mode.");
			}
			break;
		case mismatch:
			text[0] = _("Active mode FTP test failed. FileZilla does not know the correct external IP address.");
			text[1] = _("Please enter your external IP address on the active mode page of this wizard. In case you have a dynamic address or don't know your external address, use the external resolver option.");
			text[2] = _("Unless these problems get fixed, active mode FTP will not work and passive mode has to be used.");
			if (XRCCTRL(*this, "ID_ACTIVE", wxRadioButton)->GetValue()) {
				XRCCTRL(*this, "ID_PASSIVE", wxRadioButton)->SetValue(true);
				text[2] += _T(" ");
				text[2] += _("Passive mode has been set as default transfer mode.");
			}
			break;
		case externalfailed:
			text[0] = _("Failed to retrieve the external IP address.");
			text[1] = _("Please make sure FileZilla is allowed to establish outgoing connections and make sure you typed the address of the address resolver correctly.");
			text[2] = wxString::Format(_("The address you entered was: %s"), XRCCTRL(*this, "ID_ACTIVERESOLVER", wxTextCtrl)->GetValue());
			break;
		case datatainted:
			text[0] = _("Transferred data got tainted.");
			text[1] = _("You likely have a router or firewall which erroneously modified the transferred data.");
			text[2] = _("Please disable settings like 'DMZ mode' or 'Game mode' on your router.");
			text[3] = _("If this problem persists, please contact your router or firewall manufacturer for a solution.");
			break;
		}
	}
	for (unsigned int i = 0; i < 5; ++i) {
		wxString name = wxString::Format(_T("ID_SUMMARY%d"), i + 1);
		int id = wxXmlResource::GetXRCID(name);
		auto ctrl = dynamic_cast<wxStaticText*>(FindWindowById(id, this));
		if (ctrl) {
			ctrl->SetLabel(text[i]);
		}
	}
	m_pages[6]->GetSizer()->Layout();
	m_pages[6]->GetSizer()->Fit(m_pages[6]);
	wxGetApp().GetWrapEngine()->WrapRecursive(m_pages[6], m_pages[6]->GetSizer(), wxGetApp().GetWrapEngine()->GetWidthFromCache("Netconf"));

	// Focus one so enter key hits finish and not the restart button by default
	XRCCTRL(*this, "ID_SUMMARY1", wxStaticText)->SetFocus();

	socket_.reset();

	sendBuffer_.clear();

	listen_socket_.reset();
	data_socket_.reset();

	if (m_timer.IsRunning()) {
		m_timer.Stop();
	}
}

bool CNetConfWizard::Send(std::wstring const& cmd)
{
	wxASSERT(!sendBuffer_);

	if (!socket_) {
		return false;
	}

	PrintMessage(cmd, 2);

	sendBuffer_.append(fz::to_utf8(cmd));
	sendBuffer_.append("\r\n");

	m_timer.Start(15000, true);
	OnSend();

	return socket_ != 0;
}

std::wstring CNetConfWizard::GetExternalIPAddress()
{
	std::wstring ret;

	wxASSERT(socket_);

	int mode = XRCCTRL(*this, "ID_ACTIVEMODE1", wxRadioButton)->GetValue() ? 0 : (XRCCTRL(*this, "ID_ACTIVEMODE2", wxRadioButton)->GetValue() ? 1 : 2);
	if (!mode) {
		ret = fz::to_wstring_from_utf8(socket_->local_ip());
		if (ret.empty()) {
			PrintMessage(fztranslate("Failed to retrieve local IP address, aborting."), 1);
			CloseSocket();
		}
	}
	else if (mode == 1) {
		wxTextCtrl* control = XRCCTRL(*this, "ID_ACTIVEIP", wxTextCtrl);
		ret = control->GetValue().ToStdWstring();
	}
	else if (mode == 2) {
		if (!m_pIPResolver) {
			wxTextCtrl* pResolver = XRCCTRL(*this, "ID_ACTIVERESOLVER", wxTextCtrl);
			std::wstring address = pResolver->GetValue().ToStdWstring();

			PrintMessage(fz::sprintf(fztranslate("Retrieving external IP address from %s"), address), 0);

			m_pIPResolver = new CExternalIPResolver(engine_context_.GetThreadPool(), *this);
			m_pIPResolver->GetExternalIP(address, fz::address_type::ipv4, true);
			if (!m_pIPResolver->Done()) {
				return ret;
			}
		}
		if (m_pIPResolver->Successful()) {
			ret = fz::to_wstring_from_utf8(m_pIPResolver->GetIP());
		}
		else {
			PrintMessage(fztranslate("Failed to retrieve external IP address, aborting."), 1);

			m_testResult = externalfailed;
			CloseSocket();
		}
		delete m_pIPResolver;
		m_pIPResolver = 0;
	}

	return ret;
}

void CNetConfWizard::OnExternalIPAddress2(wxCommandEvent&)
{
	if (!m_pIPResolver) {
		return;
	}

	if (m_state != 3) {
		return;
	}

	if (!m_pIPResolver->Done()) {
		return;
	}

	SendNextCommand();
}

void CNetConfWizard::SendNextCommand()
{
	switch (m_state)
	{
	case 1:
		if (!Send(L"USER " + fz::to_wstring_from_utf8(PACKAGE_NAME))) {
			return;
		}
		break;
	case 2:
		if (!Send(L"PASS " + fz::to_wstring_from_utf8(PACKAGE_VERSION))) {
			return;
		}
		break;
	case 3:
		{
			PrintMessage(fztranslate("Checking for correct external IP address"), 0);
			std::wstring ip = GetExternalIPAddress();
			if (ip.empty()) {
				return;
			}
			if (!fz::get_ipv6_long_form(ip).empty()) {
				PrintMessage(fztranslate("You appear to be using an IPv6-only host. This wizard does not support this environment."), 1);
				CloseSocket();
				return;
			}
			m_externalIP = ip;

			std::wstring hexIP = ip;
			for (unsigned int i = 0; i < hexIP.size(); ++i) {
				wchar_t & c = hexIP[i];
				if (c == '.') {
					c = '-';
				}
				else {
					c = c - '0' + 'a';
				}
			}

			if (!Send(L"IP " + ip + L" " + hexIP)) {
				return;
			}

		}
		break;
	case 4:
		{
			int port = CreateListenSocket();
			if (!port) {
				PrintMessage(fz::sprintf(fztranslate("Failed to create listen socket on port %d, aborting."), port), 1);
				CloseSocket();
				return;
			}
			m_listenPort = port;
			Send(fz::sprintf(L"PREP %d", port));
			break;
		}
	case 5:
		{
			std::wstring cmd = fz::sprintf(L"PORT %s,%d,%d", m_externalIP, m_listenPort / 256, m_listenPort % 256);
			fz::replace_substrings(cmd, L".", L",");
			Send(cmd);
		}
		break;
	case 6:
		Send(L"LIST");
		break;
	case 7:
		m_testResult = successful;
		Send(L"QUIT");
		break;
	case 8:
		CloseSocket();
		break;
	}
}

void CNetConfWizard::OnRestart(wxCommandEvent&)
{
	ResetTest();
	ShowPage(m_pages[0], false);
}

void CNetConfWizard::ResetTest()
{
	if (m_timer.IsRunning()) {
		m_timer.Stop();
	}

	m_state = 0;
	m_connectSuccessful = false;

	m_testDidRun = false;
	m_testResult = unknown;
	m_recvBufferPos = 0;
	gotListReply = false;

	if (!m_pages.empty()) {
		XRCCTRL(*this, "ID_RESULTS", wxTextCtrl)->SetLabel(_T(""));
	}
}

void CNetConfWizard::OnFinish(wxWizardEvent&)
{
	if (m_testResult != successful) {
		if (wxMessageBoxEx(_("The test did not succeed. Do you really want to save the settings?"), _("Save settings?"), wxYES_NO | wxICON_QUESTION) != wxYES) {
			return;
		}
	}

	m_pOptions->SetOption(OPTION_USEPASV, XRCCTRL(*this, "ID_PASSIVE", wxRadioButton)->GetValue() ? 1 : 0);
	m_pOptions->SetOption(OPTION_ALLOW_TRANSFERMODEFALLBACK, XRCCTRL(*this, "ID_FALLBACK", wxCheckBox)->GetValue() ? 1 : 0);

	m_pOptions->SetOption(OPTION_PASVREPLYFALLBACKMODE, XRCCTRL(*this, "ID_PASSIVE_FALLBACK1", wxRadioButton)->GetValue() ? 0 : 1);

	if (XRCCTRL(*this, "ID_ACTIVEMODE1", wxRadioButton)->GetValue()) {
		m_pOptions->SetOption(OPTION_EXTERNALIPMODE, 0);
	}
	else {
		m_pOptions->SetOption(OPTION_EXTERNALIPMODE, XRCCTRL(*this, "ID_ACTIVEMODE2", wxRadioButton)->GetValue() ? 1 : 2);
	}

	m_pOptions->SetOption(OPTION_LIMITPORTS, XRCCTRL(*this, "ID_ACTIVE_PORTMODE1", wxRadioButton)->GetValue() ? 0 : 1);

	long tmp;
	XRCCTRL(*this, "ID_ACTIVE_PORTMIN", wxTextCtrl)->GetValue().ToLong(&tmp); m_pOptions->SetOption(OPTION_LIMITPORTS_LOW, tmp);
	XRCCTRL(*this, "ID_ACTIVE_PORTMAX", wxTextCtrl)->GetValue().ToLong(&tmp); m_pOptions->SetOption(OPTION_LIMITPORTS_HIGH, tmp);

	m_pOptions->SetOption(OPTION_EXTERNALIP, XRCCTRL(*this, "ID_ACTIVEIP", wxTextCtrl)->GetValue().ToStdWstring());
	m_pOptions->SetOption(OPTION_EXTERNALIPRESOLVER, XRCCTRL(*this, "ID_ACTIVERESOLVER", wxTextCtrl)->GetValue().ToStdWstring());
	m_pOptions->SetOption(OPTION_NOEXTERNALONLOCAL, XRCCTRL(*this, "ID_NOEXTERNALONLOCAL", wxCheckBox)->GetValue());
}

int CNetConfWizard::CreateListenSocket()
{
	if (listen_socket_) {
		return 0;
	}

	if (XRCCTRL(*this, "ID_ACTIVE_PORTMODE1", wxRadioButton)->GetValue()) {
		return CreateListenSocket(0);
	}
	else {
		long low;
		long high;
		XRCCTRL(*this, "ID_ACTIVE_PORTMIN", wxTextCtrl)->GetValue().ToLong(&low);
		XRCCTRL(*this, "ID_ACTIVE_PORTMAX", wxTextCtrl)->GetValue().ToLong(&high);

		int mid = fz::random_number(low, high);
		wxASSERT(mid >= low && mid <= high);

		for (int port = mid; port <= high; ++port) {
			if (CreateListenSocket(port)) {
				return port;
			}
		}
		for (int port = low; port < mid; ++port) {
			if (CreateListenSocket(port)) {
				return port;
			}
		}

		return 0;
	}
}

int CNetConfWizard::CreateListenSocket(unsigned int port)
{
	listen_socket_ = std::make_unique<fz::listen_socket>(engine_context_.GetThreadPool(), static_cast<fz::event_handler*>(this));
	int res = listen_socket_->listen(socket_ ? socket_->address_family() : fz::address_type::unknown, port);

	if (res < 0) {
		listen_socket_.reset();
		return 0;
	}

	if (port) {
		return port;
	}

	// Get port number from socket
	int error;
	res = listen_socket_->local_port(error);
	if (res <= 0) {
		listen_socket_.reset();
		return 0;
	}
	return res;
}

void CNetConfWizard::OnAccept()
{
	if (!socket_ || !listen_socket_) {
		return;
	}
	if (data_socket_) {
		return;
	}

	int error;
	data_socket_ = listen_socket_->accept(error);
	if (!data_socket_) {
		return;
	}
	data_socket_->set_event_handler(this);

	std::string peerAddr = socket_->peer_ip();
	std::string dataPeerAddr = data_socket_->peer_ip();
	if (peerAddr.empty()) {
		data_socket_.reset();
		PrintMessage(fztranslate("Failed to get peer address of control connection, connection closed."), 1);
		CloseSocket();
		return;
	}
	if (dataPeerAddr.empty()) {
		data_socket_.reset();
		PrintMessage(fztranslate("Failed to get peer address of data connection, connection closed."), 1);
		CloseSocket();
		return;
	}
	if (peerAddr != dataPeerAddr) {
		data_socket_.reset();
		PrintMessage(fztranslate("Warning, ignoring data connection from wrong IP."), 0);
		return;
	}
	listen_socket_.reset();
}

void CNetConfWizard::OnDataReceive()
{
	char buffer[100];
	int error;
	int const read = data_socket_->read(buffer, 99, error);
	if (!read) {
		PrintMessage(fztranslate("Data socket closed too early."), 1);
		CloseSocket();
		return;
	}
	if (read < 0) {
		if (error != EAGAIN) {
			PrintMessage(fztranslate("Could not read from data socket."), 1);
			CloseSocket();
		}
		return;
	}
	buffer[read] = 0;

	int data = 0;
	const char* p = buffer;
	while (*p && *p != ' ') {
		if (*p < '0' || *p > '9') {
			m_testResult = datatainted;
			PrintMessage(fztranslate("Received data tainted"), 1);
			CloseSocket();
			return;
		}
		data = data * 10 + *p++ - '0';
	}
	if (data != m_data) {
		m_testResult = datatainted;
		PrintMessage(fztranslate("Received data tainted"), 1);
		CloseSocket();
		return;
	}
	++p;
	if (p - buffer != read - 4) {
		PrintMessage(fztranslate("Failed to receive data"), 1);
		CloseSocket();
		return;
	}

	uint32_t ip = 0;
	for (auto const& c : m_externalIP) {
		if (c == '.') {
			ip *= 256;
		}
		else {
			ip = ip - (ip % 256) + (ip % 256) * 10 + c - '0';
		}
	}

	ip = wxUINT32_SWAP_ON_LE(ip);
	if (memcmp(&ip, p, 4)) {
		m_testResult = datatainted;
		PrintMessage(fztranslate("Received data tainted"), 1);
		CloseSocket();
		return;
	}

	data_socket_.reset();

	if (gotListReply) {
		++m_state;
		SendNextCommand();
	}
}

void CNetConfWizard::OnDataClose()
{
	OnDataReceive();
	if (data_socket_) {
		PrintMessage(fztranslate("Data socket closed too early."), 0);
		CloseSocket();
		return;
	}
	data_socket_.reset();

	if (gotListReply) {
		++m_state;
		SendNextCommand();
	}
}

void CNetConfWizard::OnTimer(wxTimerEvent& event)
{
	if (event.GetId() != m_timer.GetId()) {
		return;
	}

	PrintMessage(fztranslate("Connection timed out."), 0);
	CloseSocket();
}

void CNetConfWizard::operator()(fz::event_base const& ev)
{
	fz::dispatch<fz::socket_event, CExternalIPResolveEvent>(ev, this
		, &CNetConfWizard::OnSocketEvent
		, &CNetConfWizard::OnExternalIPAddress);
}

void CNetConfWizard::OnExternalIPAddress()
{
	QueueEvent(new wxCommandEvent(fzEVT_ON_EXTERNAL_IP_ADDRESS));
}

void CNetConfWizard::OnSocketEvent(fz::socket_event_source* s, fz::socket_event_flag t, int error)
{
	if (!s) {
		return;
	}

	CallAfter([=]{DoOnSocketEvent(s, t, error);});
}
