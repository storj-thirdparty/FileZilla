#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_TRANSFER_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_TRANSFER_HEADER

class COptionsPageTransfer final : public COptionsPage
{
public:
	virtual bool LoadPage() override;
	virtual bool SavePage() override;
	virtual bool Validate() override;

protected:
	virtual bool CreateControls(wxWindow* parent) override;
};

#endif
