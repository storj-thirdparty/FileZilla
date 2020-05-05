#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_FILEEXISTS_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_FILEEXISTS_HEADER

class COptionsPageFileExists final : public COptionsPage
{
public:
	virtual wxString GetResourceName() const override { return _T("ID_SETTINGS_FILEEXISTS"); }
	virtual bool LoadPage() override;
	virtual bool SavePage() override;
	virtual bool Validate() override;
};

#endif
