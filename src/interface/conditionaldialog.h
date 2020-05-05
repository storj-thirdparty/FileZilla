#ifndef FILEZILLA_INTERFACE_CONDITIONALDIALOG_HEADER
#define FILEZILLA_INTERFACE_CONDITIONALDIALOG_HEADER

#include <wx/dialog.h>

class CConditionalDialog final : public wxDialog
{
public:
	enum Modes
	{
		ok,
		yesno
	};

	enum DialogType
	{
		rawcommand_quote,
		viewhidden,
		confirmexit,
		sitemanager_confirmdelete,
		confirmexit_edit, // Confirm closing FileZilla while files are still being edited
		confirm_preserve_timestamps,
		compare_treeviewmismatch,
		compare_changesorting,
		many_selected_for_edit,
		quick_search
	};

	CConditionalDialog(wxWindow* parent, DialogType type, Modes mode, bool checked = false);

	void AddText(const wxString &text);

	bool Run();

protected:
	DialogType m_type;

	wxSizer* m_pTextSizer;

	DECLARE_EVENT_TABLE()
	void OnButton(wxCommandEvent& event);
};

#endif
