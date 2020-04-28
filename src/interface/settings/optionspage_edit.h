#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_EDIT_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_EDIT_HEADER

class COptionsPageEdit final : public COptionsPage
{
public:
	virtual wxString GetResourceName() const override { return _T("ID_SETTINGS_EDIT"); }
	virtual bool LoadPage() override;
	virtual bool SavePage() override;
	virtual bool Validate() override;

protected:

	void SetCtrlState();

	DECLARE_EVENT_TABLE()
	void OnBrowseEditor(wxCommandEvent& event);
	void OnRadioButton(wxCommandEvent& event);
};

#endif
