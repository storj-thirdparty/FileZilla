#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_CONNECTION_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_CONNECTION_HEADER

class COptionsPageConnection final : public COptionsPage
{
public:
	virtual wxString GetResourceName() const override { return _T("ID_SETTINGS_CONNECTION"); }
	virtual bool LoadPage() override;
	virtual bool SavePage() override;
	virtual bool Validate() override;

protected:
	DECLARE_EVENT_TABLE()
	void OnWizard(wxCommandEvent& event);
};

#endif
