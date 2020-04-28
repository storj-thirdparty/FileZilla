#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_EDIT_ASSOCIATIONS_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_EDIT_ASSOCIATIONS_HEADER

class COptionsPageEditAssociations final : public COptionsPage
{
public:
	virtual wxString GetResourceName() const override { return _T("ID_SETTINGS_EDIT_ASSOCIATIONS"); }
	virtual bool LoadPage() override;
	virtual bool SavePage() override;
	virtual bool Validate() override;
};

#endif
