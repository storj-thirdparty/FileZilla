#ifndef FILEZILLA_INTERFACE_CHMODDIALOG_HEADER
#define FILEZILLA_INTERFACE_CHMODDIALOG_HEADER

#include "dialogex.h"

class ChmodData
{
public:
	int GetApplyType() const { return applyType_; }

	// Converts permission string into a series of chars
	// The permissions parameter has to be able to hold at least
	// 9 characters.
	// Example:
	//   drwxr--r-- gets converted into 222211211
	//   0644 gets converted into 221211211
	//   foo (0273) gets converted into 121222122
	static bool ConvertPermissions(std::wstring const& rwx, char* permissions);

	std::wstring GetPermissions(const char* previousPermissions, bool dir);

	int applyType_{};
	std::wstring numeric_;
	char permissions_[9]{};

private:
	static bool DoConvertPermissions(std::wstring const& rwx, char* permissions);
};

class CChmodDialog final : public wxDialogEx
{
public:
	CChmodDialog(ChmodData & data);

	bool Create(wxWindow* parent, int fileCount, int dirCount,
				const wxString& name, const char permissions[9]);

	bool Recursive() const ;

protected:

	DECLARE_EVENT_TABLE()
	void OnOK(wxCommandEvent&);
	void OnCancel(wxCommandEvent&);
	void OnRecurseChanged(wxCommandEvent&);

	void OnCheckboxClick(wxCommandEvent&);
	void OnNumericChanged(wxCommandEvent&);

	ChmodData & data_;

	wxCheckBox* m_checkBoxes[9];

	bool m_noUserTextChange{};
	wxString oldNumeric;
	bool lastChangedNumeric{};

	bool m_recursive{};
};

#endif
