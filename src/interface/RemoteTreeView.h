#ifndef FILEZILLA_INTERFACE_REMOTETREEVIEW_HEADER
#define FILEZILLA_INTERFACE_REMOTETREEVIEW_HEADER

#include <option_change_event_handler.h>
#include "systemimagelist.h"
#include "state.h"
#include "filter.h"
#include "treectrlex.h"

class CQueueView;
class CWindowTinter;
class CRemoteTreeView final : public wxTreeCtrlEx, CSystemImageList, CStateEventHandler, COptionChangeEventHandler
{
	friend class CRemoteTreeViewDropTarget;

public:
	CRemoteTreeView(wxWindow* parent, wxWindowID id, CState& state, CQueueView* pQueue);
	virtual ~CRemoteTreeView();

protected:
	wxTreeItemId MakeParent(CServerPath path, bool select);
	void SetDirectoryListing(std::shared_ptr<CDirectoryListing> const& pListing, bool primary);
	virtual void OnStateChange(t_statechange_notifications notification, std::wstring const&, const void*) override;

	void DisplayItem(wxTreeItemId parent, const CDirectoryListing& listing);
	void RefreshItem(wxTreeItemId parent, const CDirectoryListing& listing, bool will_select_parent);

	void SetItemImages(wxTreeItemId item, bool unknown);

	bool HasSubdirs(const CDirectoryListing& listing, const CFilterManager& filter);

	CServerPath GetPathFromItem(const wxTreeItemId& item) const;

	bool ListExpand(wxTreeItemId item);

	void ApplyFilters(bool resort);

	CQueueView* m_pQueue;

	void CreateImageList();
	wxBitmap CreateIcon(int index, wxString const& overlay = wxString());
	wxImageList* m_pImageList{};

	// Set to true in SetDirectoryListing.
	// Used to suspends event processing in OnItemExpanding for example
	bool m_busy{};

	wxTreeItemId m_ExpandAfterList;

	wxTreeItemId m_dropHighlight;

	CServerPath MenuMkdir();

	void UpdateSortMode();

	virtual void OnOptionsChanged(changed_options_t const& options);

	std::unique_ptr<CWindowTinter> m_windowTinter;

	wxTreeItemId m_contextMenuItem;

	DECLARE_EVENT_TABLE()
	void OnItemExpanding(wxTreeEvent& event);
	void OnSelectionChanged(wxTreeEvent& event);
	void OnItemActivated(wxTreeEvent& event);
	void OnBeginDrag(wxTreeEvent& event);
	void OnContextMenu(wxTreeEvent& event);
	void OnMenuChmod(wxCommandEvent&);
	void OnMenuDownload(wxCommandEvent& event);
	void OnMenuDelete(wxCommandEvent&);
	void OnMenuRename(wxCommandEvent&);
	void OnBeginLabelEdit(wxTreeEvent& event);
	void OnEndLabelEdit(wxTreeEvent& event);
	void OnMkdir(wxCommandEvent&);
	void OnMenuMkdirChgDir(wxCommandEvent&);
	void OnChar(wxKeyEvent& event);
	void OnMenuGeturl(wxCommandEvent& event);
};

#endif
