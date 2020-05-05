#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_UPDATECHECK_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_UPDATECHECK_HEADER

#if FZ_MANUALUPDATECHECK && FZ_AUTOUPDATECHECK

class COptionsPageUpdateCheck final : public COptionsPage
{
public:
	virtual wxString GetResourceName() const override { return _T("ID_SETTINGS_UPDATECHECK"); }
	virtual bool LoadPage() override;
	virtual bool SavePage() override;
	virtual bool Validate() override;

protected:
	void OnRunUpdateCheck(wxCommandEvent&);

	DECLARE_EVENT_TABLE()
};

#endif

#endif
