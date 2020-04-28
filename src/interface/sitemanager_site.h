#ifndef FILEZILLA_INTERFACE_SITEMANAGER_SITE_HEADER
#define FILEZILLA_INTERFACE_SITEMANAGER_SITE_HEADER

#include <wx/notebook.h>

class Site;
class CSiteManagerDialog;
class CSiteManagerSite : public wxNotebook
{
public:
	CSiteManagerSite(CSiteManagerDialog & sitemanager);

	bool Load(wxWindow * parent);

	bool Verify(bool predefined);

	void UpdateSite(Site &site);
	void SetSite(Site const& site, bool predefined);

private:
	void InitProtocols();
	void SetProtocol(ServerProtocol protocol);
	ServerProtocol GetProtocol() const;

	LogonType GetLogonType() const;
	
	void SetControlVisibility(ServerProtocol protocol, LogonType type);
	
	
	void SetLogonTypeCtrlState();

	void UpdateHostFromDefaults(ServerProtocol const protocol);

	void SetExtraParameters(CServer const& server);
	void UpdateExtraParameters(CServer & server);

	DECLARE_EVENT_TABLE()
	void OnProtocolSelChanged(wxCommandEvent& event);
	void OnLogontypeSelChanged(wxCommandEvent& event);
	void OnCharsetChange(wxCommandEvent& event);
	void OnLimitMultipleConnectionsChanged(wxCommandEvent& event);
	void OnRemoteDirBrowse(wxCommandEvent& event);
	void OnKeyFileBrowse(wxCommandEvent&);
	void OnGenerateEncryptionKey(wxCommandEvent&);

	CSiteManagerDialog & sitemanager_;

	std::map<ServerProtocol, int> mainProtocolListIndex_;

	wxNotebookPage *m_pCharsetPage{};
	wxNotebookPage *m_pS3Page{};
	wxString m_charsetPageText;
	size_t m_totalPages = -1;

	ServerProtocol previousProtocol_{UNKNOWN};

	std::vector<std::pair<wxStaticText*, wxTextCtrl*>> extraParameters_[ParameterSection::section_count];

	bool predefined_{};
};

#endif
