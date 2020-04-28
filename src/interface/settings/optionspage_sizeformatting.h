#ifndef FILEZILLA_INTERFACE_OPTIONSPAGE_SIZEFORMATTING_HEADER
#define FILEZILLA_INTERFACE_OPTIONSPAGE_SIZEFORMATTING_HEADER

#include "../sizeformatting.h"

class COptionsPageSizeFormatting final : public COptionsPage
{
public:
	virtual wxString GetResourceName() const override { return _T("ID_SETTINGS_SIZEFORMATTING"); }
	virtual bool LoadPage() override;
	virtual bool SavePage() override;
	virtual bool Validate() override;

	void UpdateControls();
	void UpdateExamples();

	CSizeFormat::_format GetFormat() const;

	DECLARE_EVENT_TABLE()
	void OnRadio(wxCommandEvent& event);
	void OnCheck(wxCommandEvent& event);
	void OnSpin(wxSpinEvent& event);

	wxString FormatSize(int64_t size);
};

#endif
