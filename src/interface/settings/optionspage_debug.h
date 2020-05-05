#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_DEBUG_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_DEBUG_HEADER

class COptionsPageDebug final : public COptionsPage
{
public:
	virtual wxString GetResourceName() const override { return _T("ID_SETTINGS_DEBUG"); }
	virtual bool LoadPage() override;
	virtual bool SavePage() override;
	virtual bool Validate() override;
};

#endif
