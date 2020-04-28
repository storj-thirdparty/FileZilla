#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_TRANSFER_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_TRANSFER_HEADER

class COptionsPageTransfer final : public COptionsPage
{
public:
	virtual wxString GetResourceName() const override { return _T("ID_SETTINGS_TRANSFER"); }
	virtual bool LoadPage() override;
	virtual bool SavePage() override;
	virtual bool Validate() override;

protected:
	DECLARE_EVENT_TABLE()
	void OnToggleSpeedLimitEnable(wxCommandEvent& event);
};

#endif
