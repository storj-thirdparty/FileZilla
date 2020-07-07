#include <filezilla.h>
#include "filezillaapp.h"
#include "verifycertdialog.h"
#include "dialogex.h"
#include "ipcmutex.h"
#include "Options.h"
#include "timeformatting.h"
#include "themeprovider.h"
#include "xrc_helper.h"

#include <libfilezilla/iputils.hpp>

#include <wx/gbsizer.h>
#include <wx/scrolwin.h>
#include <wx/statbox.h>

CertStore::CertStore()
	: m_xmlFile(wxGetApp().GetSettingsFile(L"trustedcerts"))
{
}

bool CertStore::IsTrusted(fz::tls_session_info const& info)
{
	if (info.get_algorithm_warnings() != 0) {
		// These certs are never trusted.
		return false;
	}

	LoadTrustedCerts();

	fz::x509_certificate cert = info.get_certificates()[0];

	return IsTrusted(info.get_host(), info.get_port(), cert.get_raw_data(), false, !info.mismatched_hostname());
}

bool CertStore::IsInsecure(std::string const& host, unsigned int port, bool permanentOnly)
{
	auto const t = std::make_tuple(host, port);
	if (!permanentOnly && sessionInsecureHosts_.find(t) != sessionInsecureHosts_.cend()) {
		return true;
	}

	LoadTrustedCerts();

	if (insecureHosts_.find(t) != insecureHosts_.cend()) {
		return true;
	}

	return false;
}

bool CertStore::HasCertificate(std::string const& host, unsigned int port)
{
	for (auto const& cert : sessionTrustedCerts_) {
		if (cert.host == host && cert.port == port) {
			return true;
		}
	}

	LoadTrustedCerts();

	for (auto const& cert : trustedCerts_) {
		if (cert.host == host && cert.port == port) {
			return true;
		}
	}

	return false;
}

bool CertStore::DoIsTrusted(std::string const& host, unsigned int port, std::vector<uint8_t> const& data, std::list<CertStore::t_certData> const& trustedCerts, bool allowSans)
{
	if (!data.size()) {
		return false;
	}

	bool const dnsname = fz::get_address_type(host) == fz::address_type::unknown;

	for (auto const& cert : trustedCerts) {
		if (port != cert.port) {
			continue;
		}

		if (cert.data != data) {
			continue;
		}

		if (host != cert.host) {
			if (!dnsname || !allowSans || !cert.trustSans) {
				continue;
			}
		}

		return true;
	}

	return false;
}

bool CertStore::IsTrusted(std::string const& host, unsigned int port, std::vector<uint8_t> const& data, bool permanentOnly, bool allowSans)
{
	bool trusted = DoIsTrusted(host, port, data, trustedCerts_, allowSans);
	if (!trusted && !permanentOnly) {
		trusted = DoIsTrusted(host, port, data, sessionTrustedCerts_, allowSans);
	}

	return trusted;
}

void CertStore::LoadTrustedCerts()
{
	CReentrantInterProcessMutexLocker mutex(MUTEX_TRUSTEDCERTS);
	if (!m_xmlFile.Modified()) {
		return;
	}

	auto root = m_xmlFile.Load();
	if (!root) {
		return;
	}

	insecureHosts_.clear();
	trustedCerts_.clear();

	pugi::xml_node element;

	bool modified = false;
	if ((element = root.child("TrustedCerts"))) {

		auto const processEntry = [&](pugi::xml_node const& cert)
		{
			std::wstring value = GetTextElement(cert, "Data");

			t_certData data;
			data.data = fz::hex_decode(value);
			if (data.data.empty()) {
				return false;
			}

			data.host = cert.child_value("Host");
			data.port = GetTextElementInt(cert, "Port");
			if (data.host.empty() || data.port < 1 || data.port > 65535) {
				return false;
			}

			fz::datetime const now = fz::datetime::now();
			int64_t activationTime = GetTextElementInt(cert, "ActivationTime", 0);
			if (activationTime == 0 || activationTime > now.get_time_t()) {
				return false;
			}

			int64_t expirationTime = GetTextElementInt(cert, "ExpirationTime", 0);
			if (expirationTime == 0 || expirationTime < now.get_time_t()) {
				return false;
			}

			data.trustSans = GetTextElementBool(cert, "TrustSANs");

			// Weed out duplicates
			if (IsTrusted(data.host, data.port, data.data, true, false)) {
				return false;
			}

			trustedCerts_.emplace_back(std::move(data));

			return true;
		};

		auto cert = element.child("Certificate");
		while (cert) {

			auto nextCert = cert.next_sibling("Certificate");
			if (!processEntry(cert)) {
				modified = true;
				element.remove_child(cert);
			}
			cert = nextCert;
		}
	}

	if ((element = root.child("InsecureHosts"))) {

		auto const processEntry = [&](pugi::xml_node const& node)
		{
			std::string host = node.child_value();
			unsigned int port = node.attribute("Port").as_uint();
			if (host.empty() || port < 1 || port > 65535) {
				return false;
			}

			for (auto const& cert : trustedCerts_) {
				// A host can't be both trusted and insecure
				if (cert.host == host && cert.port == port) {
					return false;
				}
			}

			insecureHosts_.emplace(std::make_tuple(host, port));

			return true;
		};

		auto host = element.child("Host");
		while (host) {

			auto nextHost = host.next_sibling("Host");
			if (!processEntry(host)) {
				modified = true;
				element.remove_child(host);
			}
			host = nextHost;
		}
	}

	if (modified) {
		m_xmlFile.Save(false);
	}
}

void CertStore::SetInsecure(std::string const& host, unsigned int port, bool permanent)
{
	// A host can't be both trusted and insecure
	sessionTrustedCerts_.erase(
		std::remove_if(sessionTrustedCerts_.begin(), sessionTrustedCerts_.end(), [&host, &port](t_certData const& cert) { return cert.host == host && cert.port == port; }),
		sessionTrustedCerts_.end()
	);

	if (!permanent) {
		sessionInsecureHosts_.emplace(std::make_tuple(host, port));
		return;
	}

	CReentrantInterProcessMutexLocker mutex(MUTEX_TRUSTEDCERTS);
	LoadTrustedCerts();

	if (IsInsecure(host, port, true)) {
		return;
	}

	if (COptions::Get()->GetOptionVal(OPTION_DEFAULT_KIOSKMODE) != 2) {
		auto root = m_xmlFile.GetElement();
		if (root) {
			auto certs = root.child("TrustedCerts");

			// Purge certificates for this host
			auto const processEntry = [&host, &port](pugi::xml_node const& cert)
			{
				return host != cert.child_value("Host") || port != GetTextElementInt(cert, "Port");
			};

			auto cert = certs.child("Certificate");
			while (cert) {
				auto nextCert = cert.next_sibling("Certificate");
				if (!processEntry(cert)) {
					certs.remove_child(cert);
				}
				cert = nextCert;
			}

			auto insecureHosts = root.child("InsecureHosts");
			if (!insecureHosts) {
				insecureHosts = root.append_child("InsecureHosts");
			}

			// Remember host as insecure
			auto xhost = insecureHosts.append_child("Host");
			xhost.append_attribute("Port").set_value(port);
			xhost.text().set(fz::to_utf8(host).c_str());

			m_xmlFile.Save(true);
		}
	}

	// A host can't be both trusted and insecure
	trustedCerts_.erase(
		std::remove_if(trustedCerts_.begin(), trustedCerts_.end(), [&host, &port](t_certData const& cert) { return cert.host == host && cert.port == port; }),
		trustedCerts_.end()
	);

	insecureHosts_.emplace(std::make_tuple(host, port));
}

void CertStore::SetTrusted(fz::tls_session_info const& info, bool permanent, bool trustAllHostnames)
{
	const fz::x509_certificate certificate = info.get_certificates()[0];

	t_certData cert;
	cert.host = info.get_host();
	cert.port = info.get_port();
	cert.data = certificate.get_raw_data();

	if (trustAllHostnames) {
		cert.trustSans = true;
	}

	// A host can't be both trusted and insecure
	sessionInsecureHosts_.erase(std::make_tuple(cert.host, cert.port));

	if (!permanent) {
		t_certData cert;
		sessionTrustedCerts_.emplace_back(std::move(cert));

		return;
	}

	CReentrantInterProcessMutexLocker mutex(MUTEX_TRUSTEDCERTS);
	LoadTrustedCerts();

	if (IsTrusted(cert.host, cert.port, cert.data, true, false)) {
		return;
	}

	if (COptions::Get()->GetOptionVal(OPTION_DEFAULT_KIOSKMODE) != 2) {
		auto root = m_xmlFile.GetElement();
		if (root) {
			auto certs = root.child("TrustedCerts");
			if (!certs) {
				certs = root.append_child("TrustedCerts");
			}

			auto xCert = certs.append_child("Certificate");
			AddTextElementUtf8(xCert, "Data", fz::hex_encode<std::string>(cert.data));
			AddTextElement(xCert, "ActivationTime", static_cast<int64_t>(certificate.get_activation_time().get_time_t()));
			AddTextElement(xCert, "ExpirationTime", static_cast<int64_t>(certificate.get_expiration_time().get_time_t()));
			AddTextElement(xCert, "Host", cert.host);
			AddTextElement(xCert, "Port", cert.port);
			AddTextElement(xCert, "TrustSANs", cert.trustSans ? L"1" : L"0");

			// Purge insecure host
			auto const processEntry = [&cert](pugi::xml_node const& xhost)
			{
				return cert.host != GetTextElement(xhost) || cert.port != xhost.attribute("Port").as_uint();
			};

			auto insecureHosts = root.child("InsecureHosts");
			auto xhost = insecureHosts.child("Host");
			while (xhost) {

				auto nextHost = xhost.next_sibling("Host");
				if (!processEntry(xhost)) {
					insecureHosts.remove_child(xhost);
				}
				xhost = nextHost;
			}

			m_xmlFile.Save(true);
		}
	}

	// A host can't be both trusted and insecure
	insecureHosts_.erase(std::make_tuple(cert.host, cert.port));

	trustedCerts_.emplace_back(std::move(cert));
}


struct CVerifyCertDialog::impl final
{
	std::vector<fz::x509_certificate> certificates_;

	wxCheckBox* san_trust_{};
	wxCheckBox* always_{};

	wxStaticText* validity_{};
	wxStaticText* serial_{};
	wxStaticText* pubkey_algo_{};
	wxStaticText* signature_algo_{};
	wxStaticText* fingerprint_sha1_{};
	wxStaticText* fingerprint_sha256_{};

	wxScrolledWindow* certPanel_{};
	wxFlexGridSizer* certSizer_{};

	wxFlexGridSizer* subjectSizer_{};
	wxFlexGridSizer* issuerSizer_{};
};

CVerifyCertDialog::CVerifyCertDialog()
	: impl_(std::make_unique<impl>())
{
}

CVerifyCertDialog::~CVerifyCertDialog()
{
}

bool CVerifyCertDialog::DisplayCert(fz::x509_certificate const& cert)
{
	std::wstring const sha256 = fz::to_wstring_from_utf8(cert.get_fingerprint_sha256());
	impl_->fingerprint_sha256_->SetLabel(sha256.substr(0, sha256.size() / 2 + 1) + L"\n" + sha256.substr(sha256.size() / 2 + 1));
	impl_->fingerprint_sha1_->SetLabel(fz::to_wstring_from_utf8(cert.get_fingerprint_sha1()));

	bool valid_date{};
	wxString label;
	if (!cert.get_activation_time() || !cert.get_expiration_time()) {
		label = _("Invalid date");
	}
	else {
		// @translator: Placeholders will be filled with dates
		label = wxString::Format(_("From %s to %s"), CTimeFormat::Format(cert.get_activation_time()), CTimeFormat::Format(cert.get_expiration_time()));

		if (cert.get_activation_time() > fz::datetime::now()) {
			label += L" - ";
			label += _("Not yet valid!");
		}
		else if (cert.get_expiration_time() < fz::datetime::now()) {
			label += L" - ";
			label += _("Expired!");
		}
		else {
			valid_date = true;
		}
	}
	impl_->validity_->SetLabel(label);
	impl_->validity_->SetForegroundColour(valid_date ? wxColour() : wxColour(255, 0, 0));


	if (!cert.get_serial().empty()) {
		impl_->serial_->SetLabel(LabelEscape(fz::to_wstring_from_utf8(cert.get_serial())));
	}
	else {
		impl_->serial_->SetLabel(_("None"));
	}


	// @translator: Example: RSA with 2048 bits
	impl_->pubkey_algo_->SetLabel(wxString::Format(_("%s with %d bits"), fz::to_wstring_from_utf8(cert.get_pubkey_algorithm()), cert.get_pubkey_bits()));
	impl_->signature_algo_->SetLabel(fz::to_wstring_from_utf8(cert.get_signature_algorithm()));

	auto recalc = [this](wxWindow* panel, wxSizer* sizer) {
		sizer->Fit(panel);
		wxSize min = sizer->CalcMin();
		int const maxHeight = (line_height_ + layout().dlgUnits(1)) * 20;
		if (min.y >= maxHeight) {
			min.y = maxHeight;
			min.x += wxSystemSettings::GetMetric(wxSYS_VSCROLL_X);
		}

		// Add extra safety margin to prevent squishing on OS X.
		min.x += 2;

		panel->SetMinSize(min);
	};

	impl_->certPanel_->Freeze();
	ParseDN(impl_->certPanel_, fz::to_wstring_from_utf8(cert.get_subject()), impl_->subjectSizer_);
	
	auto const& altNames = cert.get_alt_subject_names();
	if (!altNames.empty()) {
		wxString str;
		for (auto const& altName : altNames) {
			str += LabelEscape(fz::to_wstring_from_utf8(altName.name)) + L"\n";
		}
		str.RemoveLast();
		impl_->subjectSizer_->Add(new wxStaticText(impl_->certPanel_, wxID_ANY, wxPLURAL("Alternative name:", "Alternative names:", altNames.size())));
		impl_->subjectSizer_->Add(new wxStaticText(impl_->certPanel_, wxID_ANY, str));
	}

	if (cert.self_signed()) {
		impl_->issuerSizer_->Clear(true);
		impl_->issuerSizer_->Add(new wxStaticText(impl_->certPanel_, -1, _("Same as subject, certificate is self-signed")));
	}
	else {
		ParseDN(impl_->certPanel_, fz::to_wstring_from_utf8(cert.get_issuer()), impl_->issuerSizer_);
	}

	recalc(impl_->certPanel_, impl_->certSizer_);
	impl_->certPanel_->Thaw();

	return valid_date;
}

void CVerifyCertDialog::AddAlgorithm(wxWindow* parent, wxGridBagSizer* sizer, std::string const& name, bool insecure)
{
	wxString wname = fz::to_wstring_from_utf8(name);
	if (insecure) {
		wname += L" - ";
		wname += _("Insecure algorithm!");
	}

	auto * text = new wxStaticText(parent, -1, LabelEscape(wname.ToStdWstring()));
	layout().gbAdd(sizer, text);

	if (insecure) {
		text->SetForegroundColour(wxColour(255, 0, 0));
	}
}

void CVerifyCertDialog::ShowVerificationDialog(CertStore & certStore, CCertificateNotification& notification)
{
	CVerifyCertDialog dlg;
	if (!dlg.CreateVerificationDialog(notification, false)) {
		return;
	}

	int res = dlg.ShowModal();
	if (res == wxID_OK) {
		notification.trusted_ = true;

		if (!notification.info_.get_algorithm_warnings()) {
			bool trustSANs = dlg.sanTrustAllowed_ && dlg.impl_->san_trust_->GetValue();
			bool permanent = !dlg.warning_ && dlg.impl_->always_ && dlg.impl_->always_->GetValue();
			certStore.SetTrusted(notification.info_, permanent, trustSANs);
		}
	}
	else {
		notification.trusted_ = false;
	}
}

void CVerifyCertDialog::DisplayCertificate(CCertificateNotification const& notification)
{
	CVerifyCertDialog dlg;
	if (dlg.CreateVerificationDialog(notification, true)) {
		dlg.ShowModal();
	}
}


bool CVerifyCertDialog::CreateVerificationDialog(CCertificateNotification const& notification, bool displayOnly)
{
	fz::tls_session_info const& info = notification.info_;

	auto& lay = layout();
	if (!Create(m_parent, -1, displayOnly ? _("Certificate details") : _("Unknown certificate"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER)) {
		return false;
	}
	impl_ = std::make_unique<impl>();

	auto outer = lay.createMain(this, 2);
	outer->AddGrowableCol(1);
	outer->AddGrowableRow(0);

	wxBitmap bmp = CThemeProvider::Get()->CreateBitmap(L"ART_LOCK", wxART_OTHER, CThemeProvider::GetIconSize(iconSizeNormal));
	auto icon = new wxStaticBitmap(this, -1, bmp);
	outer->Add(icon);

	auto main = lay.createFlex(1);
	main->AddGrowableCol(0);
	outer->Add(main, lay.grow);

	if (!displayOnly) {
		auto label1 = _("The server's certificate is unknown. Please carefully examine the certificate to make sure the server can be trusted.");
		WrapText(this, label1, 600);
		main->Add(new wxStaticText(this, -1, label1));
		
		auto label2 =_("Compare the displayed fingerprint with the certificate fingerprint you have received from your server adminstrator or server hosting provider.");
		WrapText(this, label2, 600);
		main->Add(new wxStaticText(this, -1, label2));
	}

	impl_->certificates_ = info.get_certificates();
	if (impl_->certificates_.size() > 1) {
		auto row = lay.createFlex(2);
		main->Add(row);
		row->Add(new wxStaticText(this, -1, _("&Certificate in chain:")), lay.valign);
		auto choice = new wxChoice(this, -1);

		row->Add(choice, lay.valign);

		if (impl_->certificates_[0].self_signed()) {
			choice->Append(L"0 (" + _("Self-signed server certificate") + L")");
		}
		else {
			choice->Append(L"0 (" + _("Server certificate") + L")");
		}
		for (unsigned int i = 1; i < impl_->certificates_.size(); ++i) {
			if (impl_->certificates_[i].self_signed()) {
				choice->Append(wxString::Format(L"%d", i) + L" (" + _("Root certificate") + L")");
			}
			else {
				choice->Append(wxString::Format(L"%d", i) + L" (" + _("Intermediate certificate") + L")");
			}
		}
		choice->SetSelection(0);

		choice->Bind(wxEVT_CHOICE, [this](auto const& ev) { OnCertificateChoice(ev); });
	}

	{
		main->AddGrowableRow(main->GetEffectiveRowsCount());
		auto row = lay.createGrid(1);
		main->Add(row, lay.grow);

		auto [box, boxsizer] = lay.createStatBox(row, _("Certificate"), 1);
		boxsizer->AddGrowableCol(0);
		boxsizer->AddGrowableRow(0);
		impl_->certPanel_ = new wxScrolledWindow(box, -1, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
		impl_->certPanel_->SetScrollRate(0, lay.dlgUnits(8));
		boxsizer->Add(impl_->certPanel_, lay.grow);
		impl_->certSizer_ = lay.createFlex(1);
		impl_->certSizer_->SetVGap(lay.dlgUnits(2));
		impl_->certPanel_->SetSizer(impl_->certSizer_);

		{
			auto heading = new wxStaticText(impl_->certPanel_, -1, _("Overview"));
			heading->SetFont(heading->GetFont().Bold());
			impl_->certSizer_->Add(heading);
			auto inner = lay.createFlex(2);
			inner->SetVGap(lay.dlgUnits(1));
			impl_->certSizer_->Add(inner, 0, wxLEFT, lay.indent);

			inner->Add(new wxStaticText(impl_->certPanel_, -1, _("Fingerprint (SHA-256):")));
			impl_->fingerprint_sha256_ = new wxStaticText(impl_->certPanel_, -1, wxString());
			inner->Add(impl_->fingerprint_sha256_);
			inner->Add(new wxStaticText(impl_->certPanel_, -1, _("Fingerprint (SHA-1):")), lay.valign);
			impl_->fingerprint_sha1_ = new wxStaticText(impl_->certPanel_, -1, wxString());
			inner->Add(impl_->fingerprint_sha1_, lay.valign);
			// @translator: Period as in a span of time with a start and end date
			inner->Add(new wxStaticText(impl_->certPanel_, -1, _("Validity period:")), lay.valign);
			impl_->validity_ = new wxStaticText(impl_->certPanel_, -1, wxString());
			inner->Add(impl_->validity_, lay.valign);
		}
		impl_->certSizer_->AddSpacer(0);
		{
			auto heading = new wxStaticText(impl_->certPanel_, -1, _("Subject"));
			heading->SetFont(heading->GetFont().Bold());
			impl_->certSizer_->Add(heading);
			auto inner = lay.createFlex(2);
			impl_->certSizer_->Add(inner, 0, wxLEFT, lay.indent);

			impl_->subjectSizer_ = lay.createFlex(2);
			impl_->subjectSizer_->SetVGap(lay.dlgUnits(1));
			impl_->certSizer_->Add(impl_->subjectSizer_, 0, wxLEFT, lay.indent);
		}
		impl_->certSizer_->AddSpacer(0);
		{
			auto heading = new wxStaticText(impl_->certPanel_, -1, _("Issuer"));
			heading->SetFont(heading->GetFont().Bold());
			impl_->certSizer_->Add(heading);
			auto inner = lay.createFlex(2);
			impl_->certSizer_->Add(inner, 0, wxLEFT, lay.indent);

			impl_->issuerSizer_ = lay.createFlex(2);
			impl_->issuerSizer_->SetVGap(lay.dlgUnits(1));
			impl_->certSizer_->Add(impl_->issuerSizer_, 0, wxLEFT, lay.indent);
		}
		impl_->certSizer_->AddSpacer(0);
		{
			auto heading = new wxStaticText(impl_->certPanel_, -1, _("Details"));
			heading->SetFont(heading->GetFont().Bold());
			impl_->certSizer_->Add(heading);
			auto inner = lay.createFlex(2);
			impl_->certSizer_->Add(inner, 0, wxLEFT, lay.indent);

			inner->Add(new wxStaticText(impl_->certPanel_, -1, _("Serial:")), lay.valign);
			impl_->serial_ = new wxStaticText(impl_->certPanel_, -1, wxString());
			inner->Add(impl_->serial_, lay.valign);
			inner->Add(new wxStaticText(impl_->certPanel_, -1, _("Public key algorithm:")), lay.valign);
			impl_->pubkey_algo_ = new wxStaticText(impl_->certPanel_, -1, wxString());
			inner->Add(impl_->pubkey_algo_, lay.valign);
			inner->Add(new wxStaticText(impl_->certPanel_, -1, _("Signature algorithm:")), lay.valign);
			impl_->signature_algo_ = new wxStaticText(impl_->certPanel_, -1, wxString());
			inner->Add(impl_->signature_algo_, lay.valign);

		}
	}
	{
		auto [box, inner] = lay.createStatBox(main, _("Session details"), 1);
		auto gb = lay.createGridBag(4);
		inner->Add(gb);
		lay.gbNewRow(gb);
		lay.gbAdd(gb, new wxStaticText(box, -1, _("Host:")));
		gb->SetVGap(lay.dlgUnits(1));
		
		wxStaticText* host = new wxStaticText(box, -1, wxString());
		if (info.mismatched_hostname()) {
			host->SetLabel(wxString::Format(_("%s:%d - Hostname does not match certificate"), LabelEscape(fz::to_wstring_from_utf8(info.get_host())), info.get_port()));
			host->SetForegroundColour(wxColour(255, 0, 0));
		}
		else {
			host->SetLabel(wxString::Format(L"%s:%d", LabelEscape(fz::to_wstring_from_utf8(info.get_host())), info.get_port()));
		}
		lay.gbAdd(gb, host);
		gb->SetItemSpan(1, wxGBSpan(1, 3));

		lay.gbNewRow(gb);
		lay.gbAdd(gb, new wxStaticText(box, -1, _("Protocol:")));
		AddAlgorithm(box, gb, info.get_protocol(), (info.get_algorithm_warnings() & fz::tls_session_info::tlsver) != 0);

		lay.gbAdd(gb, new wxStaticText(box, -1, _("Cipher:")));
		AddAlgorithm(box, gb, info.get_session_cipher(), (info.get_algorithm_warnings() & fz::tls_session_info::cipher) != 0);
		lay.gbNewRow(gb);
		lay.gbAdd(gb, new wxStaticText(box, -1, _("Key exchange:")));
		AddAlgorithm(box, gb, info.get_key_exchange(), (info.get_algorithm_warnings() & fz::tls_session_info::kex) != 0);
		lay.gbAdd(gb, new wxStaticText(box, -1, _("Mac:")));
		AddAlgorithm(box, gb, info.get_session_mac(), (info.get_algorithm_warnings() & fz::tls_session_info::mac) != 0);
	}


	if (!displayOnly) {
		main->Add(new wxStaticText(this, -1, _("Trust the server certificate and carry on connecting?")));
		if (COptions::Get()->GetOptionVal(OPTION_DEFAULT_KIOSKMODE) != 2) {
			impl_->always_ = new wxCheckBox(this, -1, _("&Always trust this certificate in future sessions."));
			main->Add(impl_->always_);
		}
		impl_->san_trust_ = new wxCheckBox(this, -1, _("&Trust this certificate on the listed alternative hostnames."));
		main->Add(impl_->san_trust_);
	}

	auto buttons = lay.createButtonSizer(this, main, false);
	auto ok = new wxButton(this, wxID_OK, _("OK"));
	ok->SetDefault();
	buttons->AddButton(ok);
	if (!displayOnly) {
		auto cancel = new wxButton(this, wxID_CANCEL, _("Cancel"));
		buttons->AddButton(cancel);
	}
	buttons->Realize();

	line_height_ = impl_->validity_->GetSize().y;

	warning_ = false;

	wxSize minSize(0, 0);
	for (unsigned int i = 0; i < impl_->certificates_.size(); ++i) {
		if (!DisplayCert(impl_->certificates_[i])) {
			warning_ = true;
		}
		Layout();
		GetSizer()->Fit(this);
		minSize.IncTo(GetSizer()->GetMinSize());
	}
	GetSizer()->SetMinSize(minSize);

	DisplayCert(impl_->certificates_[0]);

	if (info.get_algorithm_warnings() != 0) {
		warning_ = true;
	}

	if (warning_) {
		icon->SetBitmap(wxArtProvider::GetBitmap(wxART_WARNING));
		if (impl_->always_) {
			impl_->always_->Enable(false);
		}
	}

	if (!displayOnly) {
		bool const dnsname = fz::get_address_type(info.get_host()) == fz::address_type::unknown;
		sanTrustAllowed_ = !warning_ && dnsname && !info.mismatched_hostname();
		impl_->san_trust_->Enable(sanTrustAllowed_);

		if (sanTrustAllowed_ && info.system_trust()) {
			if (impl_->always_) {
				impl_->always_->SetValue(true);
			}
			impl_->san_trust_->SetValue(true);
		}
	}

	GetSizer()->Fit(this);
	GetSizer()->SetSizeHints(this);

	return true;
}

namespace {
std::vector<std::pair<std::wstring, std::wstring>> dn_split(std::wstring const& dn)
{
	std::vector<std::pair<std::wstring, std::wstring>> ret;

	std::wstring type;
	std::wstring value;

	int escaping{};
	bool phase{};

	for (auto const& c : dn) {
		auto& out = phase ? value : type;
		if (escaping) {
			if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
				--escaping;
			}
			else {
				escaping = 0;
			}
			out += c;
		}
		else if (!phase && c == '=') {
			phase = true;
		}
		else if (c == '+' || c == ',') {
			if (!type.empty() && !value.empty()) {
				ret.emplace_back(type, value);
			}
			type.clear();
			value.clear();
			phase = false;
		}
		else if (c == '\\') {
			out += c;
			escaping = 2;
		}
		else {
			out += c;
		}
	}

	if (!type.empty() && !value.empty()) {
		ret.emplace_back(type, value);
	}

	return ret;
}
}


void CVerifyCertDialog::ParseDN(wxWindow* parent, std::wstring const& dn, wxSizer* pSizer)
{
	pSizer->Clear(true);

	auto tokens = dn_split(dn);

	ParseDN_by_prefix(parent, tokens, L"CN", _("Common name:"), pSizer);
	ParseDN_by_prefix(parent, tokens, L"O", _("Organization:"), pSizer);
	ParseDN_by_prefix(parent, tokens, L"businessCategory", _("Business category:"), pSizer);
	ParseDN_by_prefix(parent, tokens, L"OU", _("Unit:"), pSizer);
	ParseDN_by_prefix(parent, tokens, L"title", _("Title:"), pSizer);
	ParseDN_by_prefix(parent, tokens, L"C", _("Country:"), pSizer);
	ParseDN_by_prefix(parent, tokens, L"ST", _("State or province:"), pSizer);
	ParseDN_by_prefix(parent, tokens, L"L", _("Locality:"), pSizer);
	ParseDN_by_prefix(parent, tokens, L"postalCode", _("Postal code:"), pSizer);
	ParseDN_by_prefix(parent, tokens, L"street", _("Street:"), pSizer);
	ParseDN_by_prefix(parent, tokens, L"EMAIL", _("E-Mail:"), pSizer);
	ParseDN_by_prefix(parent, tokens, L"serialNumber", _("Serial number:"), pSizer);
	ParseDN_by_prefix(parent, tokens, L"telephoneNumber", _("Telephone number:"), pSizer);
	ParseDN_by_prefix(parent, tokens, L"name", _("Name:"), pSizer);
	ParseDN_by_prefix(parent, tokens, L"jurisdictionOfIncorporationCountryName", _("Jurisdiction country:"), pSizer);
	ParseDN_by_prefix(parent, tokens, L"jurisdictionOfIncorporationStateOrProvinceName", _("Jurisdiction state or province:"), pSizer);
	ParseDN_by_prefix(parent, tokens, L"jurisdictionOfIncorporationLocalityName", _("Jurisdiction locality:"), pSizer);

	if (!tokens.empty()) {
		std::wstring other;
		for (auto const& pair : tokens) {
			if (!other.empty()) {
				other += ',';
			}
			other += pair.first;
			other += '=';
			other += pair.second;
		}

		pSizer->Add(new wxStaticText(parent, wxID_ANY, _("Other:")));
		pSizer->Add(new wxStaticText(parent, wxID_ANY, LabelEscape(other)));
	}
}

void CVerifyCertDialog::ParseDN_by_prefix(wxWindow* parent, std::vector<std::pair<std::wstring, std::wstring>> & tokens, std::wstring const& prefix, wxString const& name, wxSizer* pSizer)
{
	std::wstring value;

	for (auto it = tokens.cbegin(); it != tokens.cend(); ) {
		auto& pair = *it;
		if (!fz::equal_insensitive_ascii(pair.first, prefix)) {
			++it;
			continue;
		}

		if (!value.empty()) {
			value += '\n';
		}
		value += pair.second;

		it = tokens.erase(it);
	}

	if (!value.empty()) {
		pSizer->Add(new wxStaticText(parent, wxID_ANY, name));
		pSizer->Add(new wxStaticText(parent, wxID_ANY, LabelEscape(value)));
	}
}

void CVerifyCertDialog::OnCertificateChoice(wxCommandEvent const& event)
{
	int sel = event.GetSelection();
	if (sel < 0 || static_cast<unsigned int>(sel) > impl_->certificates_.size()) {
		return;
	}
	DisplayCert(impl_->certificates_[sel]);

	Layout();
	GetSizer()->Fit(this);
	Refresh();
}


void ConfirmInsecureConection(wxWindow* parent, CertStore & certStore, CInsecureConnectionNotification & notification)
{
	wxDialogEx dlg;

	auto const protocol = notification.server_.GetProtocol();

	wxString name;
	switch (protocol) {
	case FTP:
	case INSECURE_FTP:
		name = L"FTP";
		break;
	case INSECURE_WEBDAV:
		name = L"WebDAV";
		break;
	default:
		name = CServer::GetProtocolName(protocol);
		break;
	}
	dlg.Create(parent, wxID_ANY, wxString::Format(_("Insecure %s connection"), name));

	auto const& lay = dlg.layout();
	auto outer = new wxBoxSizer(wxVERTICAL);
	dlg.SetSizer(outer);

	auto main = lay.createFlex(1);
	outer->Add(main, 0, wxALL, lay.border);

	bool const warning = certStore.HasCertificate(fz::to_utf8(notification.server_.GetHost()), notification.server_.GetPort());

	if (warning) {
		if (protocol == FTP) {
			main->Add(new wxStaticText(&dlg, -1, _("Warning! You have previously connected to this server using FTP over TLS, yet the server has now rejected FTP over TLS.")));
			main->Add(new wxStaticText(&dlg, -1, _("This may be the result of a downgrade attack, only continue after you have spoken to the server administrator or server hosting provider.")));
		}
		else {
			main->Add(new wxStaticText(&dlg, -1, _("Warning! You have previously connected to this server using TLS.")));
		}
	}
	else {
		if (protocol == FTP) {
			main->Add(new wxStaticText(&dlg, -1, _("This server does not support FTP over TLS.")));
		}
		else {
			main->Add(new wxStaticText(&dlg, -1, wxString::Format(_("Using plain %s is insecure."), name)));
		}
	}
	main->Add(new wxStaticText(&dlg, -1, _("If you continue, your password and files will be sent in clear over the internet.")));


	auto flex = lay.createFlex(2);
	main->Add(flex, 0, wxALL, lay.border);
	flex->Add(new wxStaticText(&dlg, -1, _("Host:")), lay.valign);
	flex->Add(new wxStaticText(&dlg, -1, LabelEscape(notification.server_.GetHost())), lay.valign);
	flex->Add(new wxStaticText(&dlg, -1, _("Port:")), lay.valign);
	flex->Add(new wxStaticText(&dlg, -1, fz::to_wstring(notification.server_.GetPort())), lay.valign);

	auto always = new wxCheckBox(&dlg, -1, wxString::Format(_("&Always allow insecure plain %s for this server."), name));
	main->Add(always);

	auto buttons = lay.createButtonSizer(&dlg, main, true);

	auto ok = new wxButton(&dlg, wxID_OK, _("&OK"));
	if (!warning) {
		ok->SetFocus();
		ok->SetDefault();
	}
	buttons->AddButton(ok);

	auto cancel = new wxButton(&dlg, wxID_CANCEL, _("Cancel"));
	if (warning) {
		cancel->SetFocus();
		cancel->SetDefault();
	}
	buttons->AddButton(cancel);

	auto onButton = [&dlg](wxEvent & evt) {dlg.EndModal(evt.GetId()); };
	ok->Bind(wxEVT_BUTTON, onButton);
	cancel->Bind(wxEVT_BUTTON, onButton);

	buttons->Realize();

	dlg.WrapRecursive(&dlg, 2);
	dlg.Layout();

	dlg.GetSizer()->Fit(&dlg);



	bool allow = dlg.ShowModal() == wxID_OK;
	if (allow) {
		notification.allow_ = true;

		certStore.SetInsecure(fz::to_utf8(notification.server_.GetHost()), notification.server_.GetPort(), always->GetValue());
	}
}
