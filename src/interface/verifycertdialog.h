#ifndef FILEZILLA_INTERFACE_VERIFYCERTDIALOG_HEADER
#define FILEZILLA_INTERFACE_VERIFYCERTDIALOG_HEADER

#include "dialogex.h"
#include "xmlfunctions.h"

#include <list>
#include <set>

class CertStore final
{
public:
	CertStore();

	bool IsTrusted(fz::tls_session_info const& info);
	void SetTrusted(fz::tls_session_info const& info, bool permanent, bool trustAllHostnames);

	void SetInsecure(std::string const& host, unsigned int port, bool permanent);

	bool IsInsecure(std::string const& host, unsigned int port, bool permanentOnly = false);

	bool HasCertificate(std::string const& host, unsigned int port);

private:
	struct t_certData {
		std::string host;
		bool trustSans{};
		unsigned int port{};
		std::vector<uint8_t> data;
	};

	bool IsTrusted(std::string const& host, unsigned int port, std::vector<uint8_t> const& data, bool permanentOnly, bool allowSans);
	bool DoIsTrusted(std::string const& host, unsigned int port, std::vector<uint8_t> const& data, std::list<t_certData> const& trustedCerts, bool allowSans);

	void LoadTrustedCerts();

	std::list<t_certData> trustedCerts_;
	std::list<t_certData> sessionTrustedCerts_;
	std::set<std::tuple<std::string, unsigned int>> insecureHosts_;
	std::set<std::tuple<std::string, unsigned int>> sessionInsecureHosts_;

	CXmlFile m_xmlFile;
};

class CVerifyCertDialog final : protected wxDialogEx
{
public:
	static void ShowVerificationDialog(CertStore & certStore, CCertificateNotification& notification);

	static void DisplayCertificate(CCertificateNotification const& notification);

private:
	bool CreateVerificationDialog(CCertificateNotification const& notification, bool displayOnly);

	CVerifyCertDialog() = default;

	bool DisplayAlgorithm(int controlId, std::string const& name, bool insecure);

	bool DisplayCert(fz::x509_certificate const& cert);

	void ParseDN(wxWindow* parent, std::wstring const& dn, wxSizer* pSizer);
	void ParseDN_by_prefix(wxWindow* parent, std::vector<std::pair<std::wstring, std::wstring>>& tokens, std::wstring const& prefix, wxString const& name, wxSizer* pSizer);

	std::vector<fz::x509_certificate> m_certificates;
	wxSizer* m_pSubjectSizer{};
	wxSizer* m_pIssuerSizer{};
	int line_height_{};

	void OnCertificateChoice(wxCommandEvent& event);

	bool warning_{};
	bool sanTrustAllowed_{};
};

void ConfirmInsecureConection(wxWindow* parent, CertStore & certStore, CInsecureConnectionNotification & notification);

#endif
