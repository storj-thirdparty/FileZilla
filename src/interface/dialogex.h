#ifndef FILEZILLA_INTERFACE_DIALOGEX_HEADER
#define FILEZILLA_INTERFACE_DIALOGEX_HEADER

#include "wrapengine.h"

class wxGridBagSizer;
struct DialogLayout final
{
public:
	int gap{};
	int border{};

	int dlgUnits(int num) const;

	static wxSizerFlags const grow;
	static wxSizerFlags const halign;
	static wxSizerFlags const valign;
	static wxSizerFlags const valigng;

	wxFlexGridSizer* createMain(wxWindow* parent, int cols, int rows = 0) const;
	wxFlexGridSizer* createFlex(int cols, int rows = 0) const;
	wxGridBagSizer* createGridBag(int cols, int rows = 0) const;
	wxStdDialogButtonSizer* createButtonSizer(wxWindow* parent, wxSizer * sizer, bool hline) const;

	DialogLayout(wxTopLevelWindow * parent);

	void gbNewRow(wxGridBagSizer * gb) const;
	wxSizerItem* gbAddRow(wxGridBagSizer * gb, wxWindow* wnd, wxSizerFlags const& flags = wxSizerFlags()) const;
	wxSizerItem* gbAdd(wxGridBagSizer * gb, wxWindow* wnd, wxSizerFlags const& flags = wxSizerFlags()) const;
	wxSizerItem* gbAdd(wxGridBagSizer* gb, wxSizer* sizer, wxSizerFlags const& flags = wxSizerFlags()) const;

protected:
	wxTopLevelWindow * parent_;
};

class wxDialogEx : public wxDialog, public CWrapEngine
{
public:
	bool Load(wxWindow *pParent, const wxString& name);

	bool SetChildLabel(int id, const wxString& label, unsigned long maxLength = 0);
	bool SetChildLabel(char const* id, const wxString& label, unsigned long maxLength = 0);
	wxString GetChildLabel(int id);

	virtual int ShowModal();

	bool ReplaceControl(wxWindow* old, wxWindow* wnd);

	static bool CanShowPopupDialog();

	DialogLayout const& layout();

protected:
	virtual void InitDialog();

	DECLARE_EVENT_TABLE()
	virtual void OnChar(wxKeyEvent& event);
	void OnMenuEvent(wxCommandEvent& event);

#ifdef __WXMAC__
	virtual bool ProcessEvent(wxEvent& event);
#endif

	static int m_shown_dialogs;

	std::unique_ptr<DialogLayout> layout_;
};

std::wstring LabelEscape(std::wstring const& label);

#endif
