#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_DATEFORMATTING_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_DATEFORMATTING_HEADER

class COptionsPageDateFormatting final : public COptionsPage
{
public:
	virtual wxString GetResourceName() const override { return _T("ID_SETTINGS_DATEFORMATTING"); }
	virtual bool LoadPage() override;
	virtual bool SavePage() override;
	virtual bool Validate() override;

protected:

	void SetCtrlState();

	DECLARE_EVENT_TABLE()
	void OnRadioChanged(wxCommandEvent& event);
};

#endif
