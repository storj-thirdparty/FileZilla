#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_CONNECTION_FTP_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_CONNECTION_FTP_HEADER

class COptionsPageConnectionFTP final : public COptionsPage
{
public:
	virtual wxString GetResourceName() const override { return _T("ID_SETTINGS_CONNECTION_FTP"); }
	virtual bool LoadPage() override;
	virtual bool SavePage() override;
};

#endif
