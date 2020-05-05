#ifndef FILEZILLA_INTERFACE_OPTIONSPAG_PASSWORDS_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAG_PASSWORDS_HEADER

#include "optionspage.h"

class COptionsPagePasswords final : public COptionsPage
{
public:
	virtual bool LoadPage() override;
	virtual bool SavePage() override;
	virtual bool Validate() override;

protected:
	virtual bool CreateControls(wxWindow* parent) override;
};

#endif
