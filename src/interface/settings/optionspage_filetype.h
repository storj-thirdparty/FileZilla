#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_FILETYPE_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_FILETYPE_HEADER

class COptionsPageFiletype final : public COptionsPage
{
public:
	virtual wxString GetResourceName() const override { return _T("ID_SETTINGS_FILETYPE"); }
	virtual bool LoadPage() override;
	virtual bool SavePage() override;
	virtual bool Validate() override;

protected:
	void SetCtrlState();

	DECLARE_EVENT_TABLE()
	void OnRemove(wxCommandEvent& event);
	void OnAdd(wxCommandEvent& event);
	void OnSelChanged(wxListEvent& event);
	void OnTextChanged(wxCommandEvent& event);
};

#endif
