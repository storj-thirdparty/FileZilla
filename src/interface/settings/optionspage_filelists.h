#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_FILELISTS_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_FILELISTS_HEADER

class COptionsPageFilelists final : public COptionsPage
{
public:
	virtual wxString GetResourceName() const override { return _T("ID_SETTINGS_FILELISTS"); }
	virtual bool LoadPage() override;
	virtual bool SavePage() override;
	virtual bool Validate() override;
};

#endif
