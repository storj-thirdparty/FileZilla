#ifndef FILEZILLA_INTERFACE_TOOLBAR_HEADER
#define FILEZILLA_INTERFACE_TOOLBAR_HEADER

#include <option_change_event_handler.h>
#include "state.h"

#include <wx/toolbar.h>

class CMainFrame;

class CToolBar final : public wxToolBar, public CGlobalStateEventHandler, public COptionChangeEventHandler
{
public:
	CToolBar() = default;
	virtual ~CToolBar();

	void UpdateToolbarState();

	static CToolBar* Load(CMainFrame* pMainFrame);

	bool ShowTool(int id);
	bool HideTool(int id);

#ifdef __WXMSW__
	virtual bool Realize();
#endif

protected:
	void MakeTool(char const* id, std::wstring const& art, wxString const& tooltip, wxString const& help = wxString(), wxItemKind type = wxITEM_NORMAL);
	void MakeTools();

	virtual void OnStateChange(CState* pState, t_statechange_notifications notification, std::wstring const& data, const void* data2) override;
	virtual void OnOptionsChanged(changed_options_t const& options);

	CMainFrame* m_pMainFrame{};

	std::map<int, wxToolBarToolBase*> m_hidden_tools;

#ifdef __WXMSW__
	std::unique_ptr<wxImageList> toolImages_;
	std::unique_ptr<wxImageList> disabledToolImages_;
#endif

	wxSize iconSize_;
};

#endif
