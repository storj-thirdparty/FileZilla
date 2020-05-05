#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_CONNECTION_ACTIVE_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_CONNECTION_ACTIVE_HEADER

class COptionsPageConnectionActive final : public COptionsPage
{
public:
	virtual wxString GetResourceName() const override { return _T("ID_SETTINGS_CONNECTION_ACTIVE"); }
	virtual bool LoadPage() override;
	virtual bool SavePage() override;
	virtual bool Validate() override;

protected:
	virtual void SetCtrlState();

	DECLARE_EVENT_TABLE()
	void OnRadioOrCheckEvent(wxCommandEvent& event);
};

#endif
