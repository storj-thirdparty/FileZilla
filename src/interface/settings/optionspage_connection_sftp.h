#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_CONNECTION_SFTP_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_CONNECTION_SFTP_HEADER

#include <wx/process.h>

#include <memory>

class CFZPuttyGenInterface;
class COptionsPageConnectionSFTP final : public COptionsPage
{
public:
	COptionsPageConnectionSFTP();
	virtual ~COptionsPageConnectionSFTP();
	virtual wxString GetResourceName() const override { return _T("ID_SETTINGS_CONNECTION_SFTP"); }
	virtual bool LoadPage() override;
	virtual bool SavePage() override;

protected:
	std::unique_ptr<CFZPuttyGenInterface> m_pFzpg;

	bool AddKey(std::wstring keyFile, bool silent);
	bool KeyFileExists(std::wstring const& keyFile);

	void SetCtrlState();

	DECLARE_EVENT_TABLE()
	void OnAdd(wxCommandEvent& event);
	void OnRemove(wxCommandEvent& event);
	void OnSelChanged(wxListEvent& event);
};

#endif
