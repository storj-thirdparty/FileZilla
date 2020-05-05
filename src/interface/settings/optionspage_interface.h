#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_INTERFACE_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_INTERFACE_HEADER

#include "optionspage.h"

class COptionsPageInterface final : public COptionsPage
{
public:
	virtual bool LoadPage() override;
	virtual bool SavePage() override;

protected:
	virtual bool CreateControls(wxWindow* parent) override;

private:
	DECLARE_EVENT_TABLE()
	void OnLayoutChange(wxCommandEvent& event);
};

#endif
