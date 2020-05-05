#include <filezilla.h>

#define FILELISTCTRL_INCLUDE_TEMPLATE_DEFINITION

#include "view.h"
#include "chmoddialog.h"
#include "commandqueue.h"
#include "dndobjects.h"
#include "dragdropmanager.h"
#include "drop_target_ex.h"
#include "edithandler.h"
#include "filezillaapp.h"
#include "filter.h"
#include "graphics.h"
#include "infotext.h"
#include "inputdialog.h"
#include "Options.h"
#include "queue.h"
#include "RemoteListView.h"
#include "remote_recursive_operation.h"
#include "sizeformatting.h"
#include "timeformatting.h"

#include <wx/clipbrd.h>
#include <wx/menu.h>

#include <algorithm>

#include <libfilezilla/file.hpp>
#include <libfilezilla/local_filesys.hpp>
#include <libfilezilla/translate.hpp>
#include <libfilezilla/uri.hpp>

#ifdef __WXMSW__
#include "shellapi.h"
#include "commctrl.h"
#endif

std::map<ServerProtocol, CRemoteListView::ChmodHandler> CRemoteListView::chmodHandlers = {
};

class CRemoteListViewDropTarget final : public CFileDropTarget<wxListCtrlEx>
{
public:
	CRemoteListViewDropTarget(CRemoteListView* pRemoteListView)
		: CFileDropTarget<wxListCtrlEx>(pRemoteListView)
		, m_pRemoteListView(pRemoteListView)
	{
	}

	void ClearDropHighlight()
	{
		const int dropTarget = m_pRemoteListView->m_dropTarget;
		if (dropTarget != -1) {
			m_pRemoteListView->m_dropTarget = -1;
#ifdef __WXMSW__
			m_pRemoteListView->SetItemState(dropTarget, 0, wxLIST_STATE_DROPHILITED);
#else
			m_pRemoteListView->RefreshItem(dropTarget);
#endif
		}
	}

	virtual wxDragResult OnData(wxCoord x, wxCoord y, wxDragResult def)
	{
		def = FixupDragResult(def);

		if (def == wxDragError ||
			def == wxDragNone ||
			def == wxDragCancel)
		{
			return def;
		}

		if (!m_pRemoteListView->m_pDirectoryListing) {
			return wxDragError;
		}

		if (!GetData()) {
			return wxDragError;
		}

		CDragDropManager* pDragDropManager = CDragDropManager::Get();
		if (pDragDropManager) {
			pDragDropManager->pDropTarget = m_pRemoteListView;
		}

		auto const format = m_pDataObject->GetReceivedFormat();
		if (format == m_pFileDataObject->GetFormat() || format == m_pLocalDataObject->GetFormat()) {
			std::wstring subdir;
			int flags = 0;
			int hit = m_pRemoteListView->HitTest(wxPoint(x, y), flags, 0);
			if (hit != -1 && (flags & wxLIST_HITTEST_ONITEM)) {
				int index = m_pRemoteListView->GetItemIndex(hit);
				if (index != -1 && m_pRemoteListView->m_fileData[index].comparison_flags != CComparableListing::fill) {
					if (index == static_cast<int>(m_pRemoteListView->m_pDirectoryListing->size())) {
						subdir = L"..";
					}
					else if ((*m_pRemoteListView->m_pDirectoryListing)[index].is_dir()) {
						subdir = (*m_pRemoteListView->m_pDirectoryListing)[index].name;
					}
				}
			}

			if (format == m_pFileDataObject->GetFormat()) {
				m_pRemoteListView->m_state.UploadDroppedFiles(m_pFileDataObject, subdir, false);
			}
			else {
				m_pRemoteListView->m_state.UploadDroppedFiles(m_pLocalDataObject, subdir, false);
			}
			return wxDragCopy;
		}

		// At this point it's the remote data object.

		if (m_pRemoteDataObject->GetProcessId() != (int)wxGetProcessId()) {
			wxMessageBoxEx(_("Drag&drop between different instances of FileZilla has not been implemented yet."));
			return wxDragNone;
		}

		if (m_pRemoteDataObject->GetSite().server != m_pRemoteListView->m_state.GetSite().server) {
			wxMessageBoxEx(_("Drag&drop between different servers has not been implemented yet."));
			return wxDragNone;
		}

		// Find drop directory (if it exists)
		std::wstring subdir;
		int flags = 0;
		int hit = m_pRemoteListView->HitTest(wxPoint(x, y), flags, 0);
		if (hit != -1 && (flags & wxLIST_HITTEST_ONITEM)) {
			int index = m_pRemoteListView->GetItemIndex(hit);
			if (index != -1 && m_pRemoteListView->m_fileData[index].comparison_flags != CComparableListing::fill) {
				if (index == static_cast<int>(m_pRemoteListView->m_pDirectoryListing->size())) {
					subdir = L"..";
				}
				else if ((*m_pRemoteListView->m_pDirectoryListing)[index].is_dir()) {
					subdir = (*m_pRemoteListView->m_pDirectoryListing)[index].name;
				}
			}
		}

		// Get target path
		CServerPath target = m_pRemoteListView->m_pDirectoryListing->path;
		if (subdir == L"..") {
			if (target.HasParent()) {
				target = target.GetParent();
			}
		}
		else if (!subdir.empty()) {
			target.AddSegment(subdir);
		}

		// Make sure target path is valid
		if (target == m_pRemoteDataObject->GetServerPath()) {
			wxMessageBoxEx(_("Source and target of the drop operation are identical"));
			return wxDragNone;
		}

		const std::vector<CRemoteDataObject::t_fileInfo>& files = m_pRemoteDataObject->GetFiles();
		for (auto const& info : files) {
			if (info.dir) {
				CServerPath dir = m_pRemoteDataObject->GetServerPath();
				dir.AddSegment(info.name);
				if (dir == target) {
					return wxDragNone;
				}
				else if (dir.IsParentOf(target, false)) {
					wxMessageBoxEx(_("A directory cannot be dragged into one of its subdirectories."));
					return wxDragNone;
				}
			}
		}

		for (auto const& info : files) {
			m_pRemoteListView->m_state.m_pCommandQueue->ProcessCommand(
				new CRenameCommand(m_pRemoteDataObject->GetServerPath(), info.name, target, info.name)
				);
		}

		// Refresh remote listing
		m_pRemoteListView->m_state.m_pCommandQueue->ProcessCommand(new CListCommand());

		return wxDragNone;
	}

	virtual bool OnDrop(wxCoord x, wxCoord y)
	{
		CScrollableDropTarget<wxListCtrlEx>::OnDrop(x, y);
		ClearDropHighlight();

		if (!m_pRemoteListView->m_pDirectoryListing) {
			return false;
		}

		return true;
	}

	virtual int DisplayDropHighlight(wxPoint point)
	{
		DoDisplayDropHighlight(point);
		return -1;
	}

	int DoDisplayDropHighlight(wxPoint point)
	{
		int flags;
		int hit = m_pRemoteListView->HitTest(point, flags, 0);
		if (!(flags & wxLIST_HITTEST_ONITEM)) {
			hit = -1;
		}

		if (hit != -1) {
			int index = m_pRemoteListView->GetItemIndex(hit);
			if (index == -1 || m_pRemoteListView->m_fileData[index].comparison_flags == CComparableListing::fill) {
				hit = -1;
			}
			else if (index != static_cast<int>(m_pRemoteListView->m_pDirectoryListing->size())) {
				if (!(*m_pRemoteListView->m_pDirectoryListing)[index].is_dir()) {
					hit = -1;
				}
				else {
					const CDragDropManager* pDragDropManager = CDragDropManager::Get();
					if (pDragDropManager && pDragDropManager->pDragSource == m_pRemoteListView) {
						if (m_pRemoteListView->GetItemState(hit, wxLIST_STATE_SELECTED)) {
							hit = -1;
						}
					}
				}
			}
		}
		if (hit != m_pRemoteListView->m_dropTarget) {
			ClearDropHighlight();
			if (hit != -1) {
				m_pRemoteListView->m_dropTarget = hit;
#ifdef __WXMSW__
				m_pRemoteListView->SetItemState(hit, wxLIST_STATE_DROPHILITED, wxLIST_STATE_DROPHILITED);
#else
				m_pRemoteListView->RefreshItem(hit);
#endif
			}
		}
		return hit;
	}

	virtual wxDragResult OnDragOver(wxCoord x, wxCoord y, wxDragResult def)
	{
		def = CScrollableDropTarget<wxListCtrlEx>::OnDragOver(x, y, def);

		if (def == wxDragError ||
			def == wxDragNone ||
			def == wxDragCancel)
		{
			ClearDropHighlight();
			return def;
		}

		if (!m_pRemoteListView->m_pDirectoryListing) {
			ClearDropHighlight();
			return wxDragNone;
		}

		Site const& site = m_pRemoteListView->m_state.GetSite();
		if (!site) {
			ClearDropHighlight();
			return wxDragNone;
		}

		int hit = DoDisplayDropHighlight(wxPoint(x, y));
		const CDragDropManager* pDragDropManager = CDragDropManager::Get();

		if (hit == -1 && pDragDropManager &&
			pDragDropManager->remoteParent == m_pRemoteListView->m_pDirectoryListing->path &&
			site == pDragDropManager->site)
		{
			return wxDragNone;
		}

		return wxDragCopy;
	}

	virtual void OnLeave()
	{
		CScrollableDropTarget<wxListCtrlEx>::OnLeave();
		ClearDropHighlight();
	}

	virtual wxDragResult OnEnter(wxCoord x, wxCoord y, wxDragResult def)
	{
		def = CScrollableDropTarget<wxListCtrlEx>::OnEnter(x, y, def);
		return OnDragOver(x, y, def);
	}

protected:
	CRemoteListView *m_pRemoteListView{};
};

BEGIN_EVENT_TABLE(CRemoteListView, CFileListCtrl<CGenericFileData>)
	EVT_LIST_ITEM_ACTIVATED(wxID_ANY, CRemoteListView::OnItemActivated)
	EVT_CONTEXT_MENU(CRemoteListView::OnContextMenu)
	// Map both ID_DOWNLOAD and ID_ADDTOQUEUE to OnMenuDownload, code is identical
	EVT_MENU(XRCID("ID_DOWNLOAD"), CRemoteListView::OnMenuDownload)
	EVT_MENU(XRCID("ID_ADDTOQUEUE"), CRemoteListView::OnMenuDownload)
	EVT_MENU(XRCID("ID_MKDIR"), CRemoteListView::OnMenuMkdir)
	EVT_MENU(XRCID("ID_MKDIR_CHGDIR"), CRemoteListView::OnMenuMkdirChgDir)
	EVT_MENU(XRCID("ID_NEW_FILE"), CRemoteListView::OnMenuNewfile)
	EVT_MENU(XRCID("ID_DELETE"), CRemoteListView::OnMenuDelete)
	EVT_MENU(XRCID("ID_RENAME"), CRemoteListView::OnMenuRename)
	EVT_MENU(XRCID("ID_CHMOD"), CRemoteListView::OnMenuChmod)
	EVT_KEY_DOWN(CRemoteListView::OnKeyDown)
	EVT_SIZE(CRemoteListView::OnSize)
	EVT_LIST_BEGIN_DRAG(wxID_ANY, CRemoteListView::OnBeginDrag)
	EVT_MENU(XRCID("ID_EDIT"), CRemoteListView::OnMenuEdit)
	EVT_MENU(XRCID("ID_ENTER"), CRemoteListView::OnMenuEnter)
	EVT_MENU(XRCID("ID_GETURL"), CRemoteListView::OnMenuGeturl)
	EVT_MENU(XRCID("ID_GETURL_PASSWORD"), CRemoteListView::OnMenuGeturl)
	EVT_MENU(XRCID("ID_CONTEXT_REFRESH"), CRemoteListView::OnMenuRefresh)
END_EVENT_TABLE()

CRemoteListView::CRemoteListView(CView* pParent, CState& state, CQueueView* pQueue)
	: CFileListCtrl<CGenericFileData>(pParent, pQueue),
	CStateEventHandler(state),
	m_parentView(pParent)
{
	state.RegisterHandler(this, STATECHANGE_REMOTE_DIR);
	state.RegisterHandler(this, STATECHANGE_APPLYFILTER);
	state.RegisterHandler(this, STATECHANGE_REMOTE_LINKNOTDIR);
	state.RegisterHandler(this, STATECHANGE_SERVER);

	m_dropTarget = -1;

	m_pInfoText = new CInfoText(*this);

	m_pDirectoryListing = nullptr;

	const unsigned long widths[6] = { 150, 75, 80, 100, 80, 85 };

	AddColumn(_("Filename"), wxLIST_FORMAT_LEFT, widths[0], true);
	AddColumn(_("Filesize"), wxLIST_FORMAT_RIGHT, widths[1]);
	AddColumn(_("Filetype"), wxLIST_FORMAT_LEFT, widths[2]);
	AddColumn(_("Last modified"), wxLIST_FORMAT_LEFT, widths[3]);
	AddColumn(_("Permissions"), wxLIST_FORMAT_LEFT, widths[4]);
	AddColumn(_("Owner/Group"), wxLIST_FORMAT_LEFT, widths[5]);
	LoadColumnSettings(OPTION_REMOTEFILELIST_COLUMN_WIDTHS, OPTION_REMOTEFILELIST_COLUMN_SHOWN, OPTION_REMOTEFILELIST_COLUMN_ORDER);

	m_dirIcon = GetIconIndex(iconType::dir);
	SetImageList(GetSystemImageList(), wxIMAGE_LIST_SMALL);

	InitHeaderSortImageList();

	InitSort(OPTION_REMOTEFILELIST_SORTORDER);

	SetDirectoryListing(nullptr);

	SetDropTarget(new CRemoteListViewDropTarget(this));

	EnablePrefixSearch(true);

	m_windowTinter = std::make_unique<CWindowTinter>(*GetMainWindow());
}

CRemoteListView::~CRemoteListView()
{
	wxString str = wxString::Format(_T("%d %d"), m_sortDirection, m_sortColumn);
	COptions::Get()->SetOption(OPTION_REMOTEFILELIST_SORTORDER, str.ToStdWstring());
}

// See comment to OnGetItemText
int CRemoteListView::OnGetItemImage(long item) const
{
	CRemoteListView *pThis = const_cast<CRemoteListView *>(this);
	int index = GetItemIndex(item);
	if (index == -1) {
		return -1;
	}

	int &icon = pThis->m_fileData[index].icon;

	if (icon != -2) {
		return icon;
	}

	icon = pThis->GetIconIndex(iconType::file, (*m_pDirectoryListing)[index].name, false, (*m_pDirectoryListing)[index].is_dir());
	return icon;
}

int CRemoteListView::GetItemIndex(unsigned int item) const
{
	if (item >= m_indexMapping.size()) {
		return -1;
	}

	unsigned int index = m_indexMapping[item];
	if (index >= m_fileData.size()) {
		return -1;
	}

	return index;
}

bool CRemoteListView::IsItemValid(unsigned int item) const
{
	if (item >= m_indexMapping.size()) {
		return false;
	}

	unsigned int index = m_indexMapping[item];
	if (index >= m_fileData.size()) {
		return false;
	}

	return true;
}

void CRemoteListView::UpdateDirectoryListing_Added(std::shared_ptr<CDirectoryListing> const& pDirectoryListing)
{
	size_t const to_add = pDirectoryListing->size() - m_pDirectoryListing->size();
	m_pDirectoryListing = pDirectoryListing;

	m_indexMapping[0] = pDirectoryListing->size();

	CFilterManager const& filter = m_state.GetStateFilterManager();
	std::wstring const path = m_pDirectoryListing->path.GetPath();

	CGenericFileData last = m_fileData.back();
	m_fileData.pop_back();

	bool const has_selections = GetSelectedItemCount() != 0;

	std::vector<int> added_indexes;
	if (has_selections) {
		added_indexes.reserve(to_add);
	}

	std::unique_ptr<CFileListCtrlSortBase> compare = GetSortComparisonObject();
	for (size_t i = pDirectoryListing->size() - to_add; i < pDirectoryListing->size(); ++i) {
		CDirentry const& entry = (*pDirectoryListing)[i];
		CGenericFileData data;
		if (entry.is_dir()) {
			data.icon = m_dirIcon;
#ifndef __WXMSW__
			if (entry.is_link()) {
				data.icon += 3;
			}
#endif
		}
		m_fileData.push_back(data);

		if (filter.FilenameFiltered(entry.name, path, entry.is_dir(), entry.size, false, 0, entry.time)) {
			continue;
		}

		if (m_pFilelistStatusBar) {
			if (entry.is_dir()) {
				m_pFilelistStatusBar->AddDirectory();
			}
			else {
				m_pFilelistStatusBar->AddFile(entry.size);
			}
		}

		// Find correct position in index mapping
		std::vector<unsigned int>::iterator start = m_indexMapping.begin();
		if (m_hasParent) {
			++start;
		}
		std::vector<unsigned int>::iterator insertPos = std::lower_bound(start, m_indexMapping.end(), i, SortPredicate(compare));

		const int added_index = insertPos - m_indexMapping.begin();
		m_indexMapping.insert(insertPos, i);

		// Remember inserted index
		if (has_selections) {
			auto const added_indexes_insert_pos = std::lower_bound(added_indexes.begin(), added_indexes.end(), added_index);
			for (auto index = added_indexes_insert_pos; index != added_indexes.end(); ++index) {
				++(*index);
			}
			added_indexes.insert(added_indexes_insert_pos, added_index);
		}
	}

	m_fileData.push_back(last);

	SetItemCount(m_indexMapping.size());
	UpdateSelections_ItemsAdded(added_indexes);

	if (m_pFilelistStatusBar) {
		m_pFilelistStatusBar->SetHidden(m_pDirectoryListing->size() + 1 - m_indexMapping.size());
	}

	wxASSERT(m_indexMapping.size() <= pDirectoryListing->size() + 1);
}

void CRemoteListView::UpdateDirectoryListing_Removed(std::shared_ptr<CDirectoryListing> const& pDirectoryListing)
{
	size_t const countRemoved = m_pDirectoryListing->size() - pDirectoryListing->size();
	if (!countRemoved) {
		m_pDirectoryListing = pDirectoryListing;
		return;
	}
	wxASSERT(!IsComparing());

	std::vector<size_t> removedItems;
	{
		// Get indexes of the removed items in the listing
		size_t j = 0;
		size_t i = 0;
		while (i < pDirectoryListing->size() && j < m_pDirectoryListing->size()) {
			const CDirentry& oldEntry = (*m_pDirectoryListing)[j];
			const wxString& oldName = oldEntry.name;
			const wxString& newName = (*pDirectoryListing)[i].name;
			if (oldName == newName) {
				++i;
				++j;
				continue;
			}

			removedItems.push_back(j++);
		}
		for (; j < m_pDirectoryListing->size(); ++j) {
			removedItems.push_back(j);
		}

		wxASSERT(removedItems.size() == countRemoved);
	}

	std::list<int> selectedItems;

	// Number of items left to remove
	unsigned int toRemove = countRemoved;

	std::list<int> removedIndexes;

	const int size = m_indexMapping.size();
	for (int i = size - 1; i >= 0; --i) {
		bool removed = false;

		unsigned int& index = m_indexMapping[i];

		// j is the offset the index has to be adjusted
		int j = 0;
		for (auto iter = removedItems.cbegin(); iter != removedItems.cend(); ++iter, ++j) {
			if (*iter > index) {
				break;
			}

			if (*iter == index) {
				removedIndexes.push_back(i);
				removed = true;
				--toRemove;
				break;
			}
		}

		// Get old selection
		bool isSelected = GetItemState(i, wxLIST_STATE_SELECTED) != 0;

		// Update statusbar info
		if (removed && m_pFilelistStatusBar) {
			const CDirentry& oldEntry = (*m_pDirectoryListing)[index];
			if (isSelected) {
				if (oldEntry.is_dir()) {
					m_pFilelistStatusBar->UnselectDirectory();
				}
				else {
					m_pFilelistStatusBar->UnselectFile(oldEntry.size);
				}
			}
			if (oldEntry.is_dir()) {
				m_pFilelistStatusBar->RemoveDirectory();
			}
			else {
				m_pFilelistStatusBar->RemoveFile(oldEntry.size);
			}
		}

		// Update index
		index -= j;

		// Update selections
		bool needSelection;
		if (selectedItems.empty()) {
			needSelection = false;
		}
		else if (selectedItems.front() == i) {
			needSelection = true;
			selectedItems.pop_front();
		}
		else {
			needSelection = false;
		}

		if (isSelected) {
			if (!needSelection && (toRemove || removed)) {
				SetSelection(i, false);
			}

			if (!removed) {
				selectedItems.push_back(i - toRemove);
			}
		}
		else if (needSelection) {
			SetSelection(i, true);
		}
	}

	// Erase file data
	for (auto iter = removedItems.crbegin(); iter != removedItems.crend(); ++iter) {
		m_fileData.erase(m_fileData.begin() + *iter);
	}

	// Erase indexes
	wxASSERT(!toRemove);
	wxASSERT(removedIndexes.size() == countRemoved);
	for (auto const& removedIndex : removedIndexes) {
		m_indexMapping.erase(m_indexMapping.begin() + removedIndex);
	}

	wxASSERT(m_indexMapping.size() == pDirectoryListing->size() + 1);

	m_pDirectoryListing = pDirectoryListing;

	if (m_pFilelistStatusBar) {
		m_pFilelistStatusBar->SetHidden(m_pDirectoryListing->size() + 1 - m_indexMapping.size());
	}

	SaveSetItemCount(m_indexMapping.size());
}

bool CRemoteListView::UpdateDirectoryListing(std::shared_ptr<CDirectoryListing> const& pDirectoryListing)
{
	assert(!IsComparing());

	const int unsure = pDirectoryListing->get_unsure_flags() & ~(CDirectoryListing::unsure_unknown);

	if (!unsure) {
		return false;
	}

	if (unsure & CDirectoryListing::unsure_invalid) {
		return false;
	}

	if (!(unsure & ~(CDirectoryListing::unsure_dir_changed | CDirectoryListing::unsure_file_changed))) {
		if (m_sortColumn != 0 && m_sortColumn != 2) {
			// If not sorted by file or type, changing file attributes can influence
			// sort order.
			return false;
		}

		if (CFilterManager::HasActiveFilters()) {
			return false;
		}

		assert(pDirectoryListing->size() == m_pDirectoryListing->size());
		if (pDirectoryListing->size() != m_pDirectoryListing->size()) {
			return false;
		}

		m_pDirectoryListing = pDirectoryListing;

		// We don't have to do anything
		return true;
	}

	if (unsure & (CDirectoryListing::unsure_dir_added | CDirectoryListing::unsure_file_added))
	{
		if (unsure & (CDirectoryListing::unsure_dir_removed | CDirectoryListing::unsure_file_removed)) {
			return false; // Cannot handle both at the same time unfortunately
		}
		UpdateDirectoryListing_Added(pDirectoryListing);
		return true;
	}

	assert(pDirectoryListing->size() <= m_pDirectoryListing->size());
	UpdateDirectoryListing_Removed(pDirectoryListing);
	return true;
}

void CRemoteListView::SetDirectoryListing(std::shared_ptr<CDirectoryListing> const& pDirectoryListing)
{
	CancelLabelEdit();

	bool reset = false;
	if (!pDirectoryListing || !m_pDirectoryListing) {
		reset = true;
	}
	else if (m_pDirectoryListing->path != pDirectoryListing->path) {
		reset = true;
	}
	else if (m_pDirectoryListing->m_firstListTime == pDirectoryListing->m_firstListTime && !IsComparing()
		&& m_pDirectoryListing->size() > 200)
	{
		// Updated directory listing. Check if we can use process it in a different,
		// more efficient way.
		// Makes only sense for big listings though.
		if (UpdateDirectoryListing(pDirectoryListing)) {
			wxASSERT(GetItemCount() == (int)m_indexMapping.size());
			wxASSERT(GetItemCount() <= (int)m_fileData.size());
			wxASSERT(GetItemCount() == (int)m_fileData.size() || CFilterManager::HasActiveFilters());
			wxASSERT(m_pDirectoryListing->size() + 1 >= (size_t)GetItemCount());
			wxASSERT(m_indexMapping[0] == m_pDirectoryListing->size());

			RefreshListOnly();

			return;
		}
	}

	int focusedItem = -1;
	std::wstring prevFocused;
	std::vector<std::wstring> selectedNames;
	bool ensureVisible = false;
	if (reset) {
		ResetSearchPrefix();

		if (IsComparing() && m_pDirectoryListing) {
			ExitComparisonMode();
		}

		ClearSelection();

		prevFocused = m_state.GetPreviouslyVisitedRemoteSubdir();
		ensureVisible = !prevFocused.empty();
	}
	else {
		// Remember which items were selected
		selectedNames = RememberSelectedItems(prevFocused, focusedItem);
	}

	if (m_pFilelistStatusBar) {
		m_pFilelistStatusBar->UnselectAll();
		m_pFilelistStatusBar->SetConnected(pDirectoryListing != 0);
	}

	m_pDirectoryListing = pDirectoryListing;

	m_fileData.clear();
	m_indexMapping.clear();

	int64_t totalSize{};
	int unknown_sizes = 0;
	int totalFileCount = 0;
	int totalDirCount = 0;
	int hidden = 0;

	bool eraseBackground = false;
	if (m_pDirectoryListing) {
		SetInfoText();

		m_indexMapping.push_back(m_pDirectoryListing->size());

		std::wstring const path = m_pDirectoryListing->path.GetPath();

		CFilterManager const& filter = m_state.GetStateFilterManager();
		
		for (unsigned int i = 0; i < m_pDirectoryListing->size(); ++i) {
			const CDirentry& entry = (*m_pDirectoryListing)[i];
			CGenericFileData data;
			if (entry.is_dir()) {
				data.icon = m_dirIcon;
#ifndef __WXMSW__
				if (entry.is_link()) {
					data.icon += 3;
				}
#endif
			}
			m_fileData.push_back(data);

			if (filter.FilenameFiltered(entry.name, path, entry.is_dir(), entry.size, false, 0, entry.time)) {
				++hidden;
				continue;
			}

			if (entry.is_dir()) {
				++totalDirCount;
			}
			else {
				if (entry.size == -1) {
					++unknown_sizes;
				}
				else {
					totalSize += entry.size;
				}
				++totalFileCount;
			}

			m_indexMapping.push_back(i);
		}

		CGenericFileData data;
		data.icon = m_dirIcon;
		m_fileData.push_back(data);
	}
	else {
		eraseBackground = true;
		SetInfoText();
	}

	if (m_pFilelistStatusBar) {
		m_pFilelistStatusBar->SetDirectoryContents(totalFileCount, totalDirCount, totalSize, unknown_sizes, hidden);
	}

	if (m_dropTarget != -1) {
		bool resetDropTarget = false;
		int index = GetItemIndex(m_dropTarget);
		if (index == -1) {
			resetDropTarget = true;
		}
		else if (index != (int)m_pDirectoryListing->size()) {
			if (!(*m_pDirectoryListing)[index].is_dir()) {
				resetDropTarget = true;
			}
		}

		if (resetDropTarget) {
			if (m_dropTarget < GetItemCount()) {
				SetItemState(m_dropTarget, 0, wxLIST_STATE_DROPHILITED);
			}
			m_dropTarget = -1;
		}
	}

	if (!IsComparing()) {
		if (static_cast<size_t>(GetItemCount()) > m_indexMapping.size()) {
			eraseBackground = true;
		}
		if (static_cast<size_t>(GetItemCount()) != m_indexMapping.size()) {
			SetItemCount(m_indexMapping.size());
		}

		if (GetItemCount() && reset) {
			EnsureVisible(0);
			SetItemState(0, wxLIST_STATE_FOCUSED, wxLIST_STATE_FOCUSED);
		}
	}

	SortList(-1, -1, false);

	if (IsComparing()) {
		m_originalIndexMapping.clear();
		RefreshComparison();
		ReselectItems(selectedNames, prevFocused, focusedItem, ensureVisible);
	}
	else {
		ReselectItems(selectedNames, prevFocused, focusedItem, ensureVisible);
		RefreshListOnly(eraseBackground);
	}
}

// Filenames on VMS systems have a revision suffix, e.g.
// foo.bar;1
// foo.bar;2
// foo.bar;3
std::wstring StripVMSRevision(std::wstring const& name)
{
	size_t pos = name.rfind(';');
	if (pos == std::wstring::npos || !pos) {
		return name;
	}

	if (pos == name.size() - 1) {
		return name;
	}

	size_t p = pos;
	while (++p < name.size()) {
		wchar_t const& c = name[p];
		if (c < '0' || c > '9') {
			return name;
		}
	}

	return name.substr(0, pos);
}


void CRemoteListView::OnItemActivated(wxListEvent &event)
{
	int const action = COptions::Get()->GetOptionVal(OPTION_DOUBLECLICK_ACTION_DIRECTORY);
	if (!m_state.IsRemoteIdle(action ? false : true)) {
		wxBell();
		return;
	}

	int count = 0;
	bool back = false;

	int item = -1;
	for (;;) {
		item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		if (item == -1) {
			break;
		}

		int index = GetItemIndex(item);
		if (index == -1 || m_fileData[index].comparison_flags == fill) {
			continue;
		}

		count++;

		if (!item) {
			back = true;
		}
	}
	if (!count) {
		wxBell();
		return;
	}
	if (count > 1) {
		if (back) {
			wxBell();
			return;
		}

		wxCommandEvent cmdEvent;
		OnMenuDownload(cmdEvent);
		return;
	}

	item = event.GetIndex();

	if (item) {
		int index = GetItemIndex(item);
		if (index == -1) {
			return;
		}
		if (m_fileData[index].comparison_flags == fill) {
			return;
		}

		CDirentry const& entry = (*m_pDirectoryListing)[index];
		std::wstring const& name = entry.name;

		Site const& site = m_state.GetSite();
		if (!site) {
			wxBell();
			return;
		}

		if (entry.is_dir()) {
			if (action == 3) {
				// No action
				wxBell();
				return;
			}

			if (!action) {
				if (entry.is_link()) {
					m_pLinkResolveState.reset(new t_linkResolveState);
					m_pLinkResolveState->remote_path = m_pDirectoryListing->path;
					m_pLinkResolveState->link = name;
					m_pLinkResolveState->local_path = m_state.GetLocalDir();
					m_pLinkResolveState->site = site;
				}
				m_state.ChangeRemoteDir(m_pDirectoryListing->path, name, entry.is_link() ? LIST_FLAG_LINK : 0);
			}
			else {
				wxCommandEvent evt(0, action == 1 ? XRCID("ID_DOWNLOAD") : XRCID("ID_ADDTOQUEUE"));
				OnMenuDownload(evt);
			}
		}
		else {
			const int action = COptions::Get()->GetOptionVal(OPTION_DOUBLECLICK_ACTION_FILE);
			if (action == 3) {
				// No action
				wxBell();
				return;
			}

			if (action == 2) {
				// View / Edit action
				wxCommandEvent evt;
				OnMenuEdit(evt);
				return;
			}

			const bool queue_only = action == 1;

			const CLocalPath local_path = m_state.GetLocalDir();
			if (!local_path.IsWriteable()) {
				wxBell();
				return;
			}

			std::wstring localFile = CQueueView::ReplaceInvalidCharacters(name);
			if (m_pDirectoryListing->path.GetType() == VMS && COptions::Get()->GetOptionVal(OPTION_STRIP_VMS_REVISION)) {
				localFile = StripVMSRevision(localFile);
			}
			m_pQueue->QueueFile(queue_only, true, name,
				(name == localFile) ? std::wstring() : localFile,
				local_path, m_pDirectoryListing->path, site, entry.size);
			m_pQueue->QueueFile_Finish(true);
		}
	}
	else {
		m_state.ChangeRemoteDir(m_pDirectoryListing->path, _T(".."));
	}
}

void CRemoteListView::OnMenuEnter(wxCommandEvent &)
{
	if (!m_state.IsRemoteIdle(true)) {
		wxBell();
		return;
	}

	int item = GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);

	if (GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED) != -1) {
		wxBell();
		return;
	}

	if (item) {
		int index = GetItemIndex(item);
		if (index == -1) {
			wxBell();
			return;
		}
		if (m_fileData[index].comparison_flags == fill) {
			wxBell();
			return;
		}

		CDirentry const& entry = (*m_pDirectoryListing)[index];
		std::wstring const& name = entry.name;

		Site const& site = m_state.GetSite();
		if (!site) {
			wxBell();
			return;
		}

		if (!entry.is_dir()) {
			wxBell();
			return;
		}

		if (entry.is_link()) {
			m_pLinkResolveState.reset(new t_linkResolveState);
			m_pLinkResolveState->remote_path = m_pDirectoryListing->path;
			m_pLinkResolveState->link = name;
			m_pLinkResolveState->local_path = m_state.GetLocalDir();
			m_pLinkResolveState->site = site;
		}
		m_state.ChangeRemoteDir(m_pDirectoryListing->path, name, entry.is_link() ? LIST_FLAG_LINK : 0);
	}
	else {
		m_state.ChangeRemoteDir(m_pDirectoryListing->path, L"..");
	}
}

void CRemoteListView::OnContextMenu(wxContextMenuEvent& event)
{
	if (GetEditControl()) {
		event.Skip();
		return;
	}

	wxMenu menu;
	auto item = new wxMenuItem(&menu, XRCID("ID_DOWNLOAD"), _("&Download"), _("Download selected files and directories"));
	item->SetBitmap(wxArtProvider::GetBitmap(_T("ART_DOWNLOAD"), wxART_MENU));
	menu.Append(item);
	item = new wxMenuItem(&menu, XRCID("ID_ADDTOQUEUE"), _("&Add files to queue"), _("Add selected files and folders to the transfer queue"));
	item->SetBitmap(wxArtProvider::GetBitmap(_T("ART_DOWNLOADADD"), wxART_MENU));
	menu.Append(item);
	
	Site const& site = m_state.GetSite();
	auto protocol = site.server.GetProtocol();
	
	if (protocol == STORJ) {
		menu.Append(XRCID("ID_ENTER"), _("E&nter bucket"), _("Enter selected bucket"));
		menu.Append(XRCID("ID_ENTERPATH"), _("E&nter path"), _("Enter selected path"));
	} else {
		menu.Append(XRCID("ID_ENTER"), _("E&nter directory"), _("Enter selected directory"));
	}	
	menu.Append(XRCID("ID_EDIT"), _("&View/Edit"));

	menu.AppendSeparator();
	
	
	if (protocol == STORJ) {
		menu.Append(XRCID("ID_MKDIR"), _("&Create bucket"), _("Create a new bucket in the current project"));
		menu.Append(XRCID("ID_MKDIR_CHGDIR"), _("Create &bucket and enter it"), _("Create a new bucket in the current project and change into it"));
	} else {
		menu.Append(XRCID("ID_MKDIR"), _("&Create directory"), _("Create a new subdirectory in the current directory"));
		menu.Append(XRCID("ID_MKDIR_CHGDIR"), _("Create director&y and enter it"), _("Create a new subdirectory in the current directory and change into it"));
	}

	menu.Append(XRCID("ID_NEW_FILE"), _("Crea&te new file"), _("Create a new, empty file in the current directory"));
	menu.Append(XRCID("ID_CONTEXT_REFRESH"), _("Re&fresh"));

	menu.AppendSeparator();
	menu.Append(XRCID("ID_DELETE"), _("D&elete"), _("Delete selected files and directories"));
	menu.Append(XRCID("ID_RENAME"), _("&Rename"), _("Rename selected files and directories"));
	menu.Append(XRCID("ID_GETURL"), _("C&opy URL(s) to clipboard"), _("Copy the URLs of the selected items to clipboard."));
	menu.Append(XRCID("ID_GETURL_PASSWORD"), _("C&opy URL(s) with password to clipboard"), _("Copy the URLs of the selected items to clipboard, including password."));
	menu.Append(XRCID("ID_CHMOD"), _("&File permissions..."), _("Change the file permissions."));


	bool const idle = m_state.IsRemoteIdle();
	bool const userIdle = m_state.IsRemoteIdle(true);
	if (!m_state.IsRemoteConnected() || !idle) {
		bool canEnter = false;
		if (userIdle) {
			int item = GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
			if (item > 0 && GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED) == -1) {
				int index = GetItemIndex(item);
				if (index != -1 && m_fileData[index].comparison_flags != fill) {
					if ((*m_pDirectoryListing)[index].is_dir()) {
						canEnter = true;
					}
				}
			}
		}
		else {
			menu.Enable(XRCID("ID_ENTER"), false);
		}
		if (!canEnter) {
			menu.Delete(XRCID("ID_ENTER"));
		}
		menu.Enable(XRCID("ID_DOWNLOAD"), false);
		menu.Enable(XRCID("ID_ADDTOQUEUE"), false);
		menu.Enable(XRCID("ID_MKDIR"), false);
		menu.Enable(XRCID("ID_MKDIR_CHGDIR"), false);
		menu.Enable(XRCID("ID_DELETE"), false);
		menu.Enable(XRCID("ID_RENAME"), false);
		menu.Enable(XRCID("ID_CHMOD"), false);
		menu.Enable(XRCID("ID_EDIT"), false);
		menu.Enable(XRCID("ID_GETURL"), false);
		menu.Enable(XRCID("ID_GETURL_PASSWORD"), false);
		menu.Enable(XRCID("ID_CONTEXT_REFRESH"), false);
		menu.Enable(XRCID("ID_NEW_FILE"), false);
	}
	else if (GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED) == -1) {
		menu.Delete(XRCID("ID_ENTER"));
		menu.Enable(XRCID("ID_DOWNLOAD"), false);
		menu.Enable(XRCID("ID_ADDTOQUEUE"), false);
		menu.Enable(XRCID("ID_DELETE"), false);
		menu.Enable(XRCID("ID_RENAME"), false);
		menu.Enable(XRCID("ID_CHMOD"), false);
		menu.Enable(XRCID("ID_EDIT"), false);
		menu.Enable(XRCID("ID_GETURL"), false);
		menu.Enable(XRCID("ID_GETURL_PASSWORD"), false);
		if (protocol == STORJ) {
			// Disable Create new file option
			// in the empty area where buckets are listed
			if (m_pDirectoryListing->path.HasParent()) {
				// within a bucket or an upload path
				menu.Delete(XRCID("ID_MKDIR"));
				menu.Delete(XRCID("ID_MKDIR_CHGDIR"));
			} else {
				// in a project
				menu.Enable(XRCID("ID_MKDIR"), true);
				menu.Enable(XRCID("ID_MKDIR_CHGDIR"), true);
				menu.Enable(XRCID("ID_NEW_FILE"), false);
			}
		}		
	}
	else {
		if ((GetItemCount() && GetItemState(0, wxLIST_STATE_SELECTED))) {
			menu.Enable(XRCID("ID_RENAME"), false);
			menu.Enable(XRCID("ID_CHMOD"), false);
			menu.Enable(XRCID("ID_EDIT"), false);
			menu.Enable(XRCID("ID_GETURL"), false);
			menu.Enable(XRCID("ID_GETURL_PASSWORD"), false);
		}

		int count = 0;
		int fillCount = 0;
		bool selectedDir = false;
		int item = -1;
		while ((item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != -1) {
			if (!item) {
				++count;
				++fillCount;
				continue;
			}

			int index = GetItemIndex(item);
			if (index == -1) {
				continue;
			}
			++count;
			if (m_fileData[index].comparison_flags == fill) {
				++fillCount;
				continue;
			}
			if ((*m_pDirectoryListing)[index].is_dir()) {
				selectedDir = true;
			}
		}
		if (!count || fillCount == count) {
			menu.Delete(XRCID("ID_ENTER"));
			menu.Delete(XRCID("ID_MKDIR"));
			menu.Delete(XRCID("ID_MKDIR_CHGDIR"));
			menu.Enable(XRCID("ID_NEW_FILE"), false);
			if (protocol == STORJ)
				menu.Delete(XRCID("ID_ENTERPATH"));
			menu.Enable(XRCID("ID_DOWNLOAD"), false);
			menu.Enable(XRCID("ID_ADDTOQUEUE"), false);
			menu.Enable(XRCID("ID_DELETE"), false);
			menu.Enable(XRCID("ID_RENAME"), false);
			menu.Enable(XRCID("ID_CHMOD"), false);
			menu.Enable(XRCID("ID_EDIT"), false);
			menu.Enable(XRCID("ID_GETURL"), false);
			menu.Enable(XRCID("ID_GETURL_PASSWORD"), false);
		}
		else {
			if (selectedDir) {
				menu.Enable(XRCID("ID_EDIT"), false);
				if (protocol == STORJ) {
					// Bucket list context menu 
					menu.Enable(XRCID("ID_NEW_FILE"), false);
					menu.Enable(XRCID("ID_RENAME"), false);
					menu.Enable(XRCID("ID_GETURL"), false);
					menu.Enable(XRCID("ID_CHMOD"), false);
					//
					if (m_pDirectoryListing->path.HasParent()) {
						// inside a bucket or a path
						menu.Delete(XRCID("ID_ENTER"));
						menu.Delete(XRCID("ID_MKDIR"));
						menu.Delete(XRCID("ID_MKDIR_CHGDIR"));
					} else {
						// inside a project
						menu.Delete(XRCID("ID_ENTERPATH"));
						menu.Enable(XRCID("ID_MKDIR"), false);
						menu.Enable(XRCID("ID_MKDIR_CHGDIR"), false);
					}
				}								
				if (!CServer::ProtocolHasFeature(m_state.GetSite().server.GetProtocol(), ProtocolFeature::DirectoryRename)) {
					menu.Enable(XRCID("ID_RENAME"), false);
				}
			}
			else {
				// Inside bucket context menu, with an object selected
				if (protocol == STORJ) {
					menu.Delete(XRCID("ID_ENTERPATH"));
					menu.Delete(XRCID("ID_MKDIR"));
					menu.Delete(XRCID("ID_MKDIR_CHGDIR"));
					menu.Enable(XRCID("ID_NEW_FILE"), false);
					menu.Enable(XRCID("ID_RENAME"), false);
					menu.Enable(XRCID("ID_GETURL"), false);
					menu.Enable(XRCID("ID_CHMOD"), false);
				}				
				menu.Delete(XRCID("ID_ENTER"));
			}
			if (count > 1) {
				if (selectedDir) {
					menu.Delete(XRCID("ID_ENTER"));
				}
				menu.Enable(XRCID("ID_RENAME"), false);
			}

			if (!m_state.GetLocalDir().IsWriteable()) {
				menu.Enable(XRCID("ID_DOWNLOAD"), false);
				menu.Enable(XRCID("ID_ADDTOQUEUE"), false);
			}
		}
	}

	menu.Delete(XRCID(wxGetKeyState(WXK_SHIFT) ? "ID_GETURL" : "ID_GETURL_PASSWORD"));

	PopupMenu(&menu);
}

void CRemoteListView::OnMenuDownload(wxCommandEvent& event)
{
	// Make sure selection is valid
	bool idle = m_state.IsRemoteIdle();

	const CLocalPath localDir = m_state.GetLocalDir();
	if (!localDir.IsWriteable()) {
		wxBell();
		return;
	}

	long item = -1;
	for (;;) {
		item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		if (item == -1)
			break;

		if (!item)
			continue;

		int index = GetItemIndex(item);
		if (index == -1)
			continue;
		if (m_fileData[index].comparison_flags == fill)
			continue;
		if ((*m_pDirectoryListing)[index].is_dir() && !idle) {
			wxBell();
			return;
		}
	}

	TransferSelectedFiles(localDir, event.GetId() == XRCID("ID_ADDTOQUEUE"));
}

void CRemoteListView::TransferSelectedFiles(const CLocalPath& local_parent, bool queue_only)
{
	bool idle = m_state.IsRemoteIdle();

	CRemoteRecursiveOperation* pRecursiveOperation = m_state.GetRemoteRecursiveOperation();
	wxASSERT(pRecursiveOperation);

	wxASSERT(local_parent.IsWriteable());

	Site const& site = m_state.GetSite();
	if (!site) {
		wxBell();
		return;
	}

	bool added = false;
	long item = -1;

	recursion_root root(m_pDirectoryListing->path, false);
	for (;;) {
		item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		if (item == -1) {
			break;
		}
		if (!item) {
			continue;
		}

		int index = GetItemIndex(item);
		if (index == -1) {
			continue;
		}
		if (m_fileData[index].comparison_flags == fill) {
			continue;
		}

		CDirentry const& entry = (*m_pDirectoryListing)[index];
		std::wstring const& name = entry.name;

		if (entry.is_dir()) {
			if (!idle) {
				continue;
			}
			CLocalPath local_path(local_parent);
			local_path.AddSegment(CQueueView::ReplaceInvalidCharacters(name));
			CServerPath remotePath = m_pDirectoryListing->path;
			if (remotePath.AddSegment(name)) {
				root.add_dir_to_visit(m_pDirectoryListing->path, name, local_path, entry.is_link());
			}
		}
		else {
			std::wstring localFile = CQueueView::ReplaceInvalidCharacters(name);
			if (m_pDirectoryListing->path.GetType() == VMS && COptions::Get()->GetOptionVal(OPTION_STRIP_VMS_REVISION)) {
				localFile = StripVMSRevision(localFile);
			}
			m_pQueue->QueueFile(queue_only, true,
				name, (name == localFile) ? std::wstring() : localFile,
				local_parent, m_pDirectoryListing->path, site, entry.size);
			added = true;
		}
	}
	if (added) {
		m_pQueue->QueueFile_Finish(!queue_only);
	}

	if (!root.empty()) {
		pRecursiveOperation->AddRecursionRoot(std::move(root));
		CFilterManager filter;
		pRecursiveOperation->StartRecursiveOperation(CRecursiveOperation::recursive_transfer, filter.GetActiveFilters(), m_pDirectoryListing->path, !queue_only);
	}
}

// Create a new Directory
void CRemoteListView::OnMenuMkdir(wxCommandEvent&)
{
	MenuMkdir();
}

// Create a new Directory and enter the new Directory
void CRemoteListView::OnMenuMkdirChgDir(wxCommandEvent&)
{
	CServerPath newdir = MenuMkdir();
	if (!newdir.empty()) {
		m_state.ChangeRemoteDir(newdir, std::wstring(), 0, true);
	}
}

// Help-Function to create a new Directory
// Returns the name of the new directory
CServerPath CRemoteListView::MenuMkdir()
{
	if (!m_pDirectoryListing || !m_state.IsRemoteIdle()) {
		wxBell();
		return CServerPath();
	}

	CInputDialog dlg;
	if (!dlg.Create(this, _("Create directory"), _("Please enter the name of the directory which should be created:")))
		return CServerPath();

	CServerPath path = m_pDirectoryListing->path;

	// Append a long segment which does (most likely) not exist in the path and
	// replace it with "New directory" later. This way we get the exact position of
	// "New directory" and can preselect it in the dialog.
	std::wstring tmpName = _T("25CF809E56B343b5A12D1F0466E3B37A49A9087FDCF8412AA9AF8D1E849D01CF");
	if (path.AddSegment(tmpName)) {
		wxString pathName = path.GetPath();
		int pos = pathName.Find(tmpName);
		wxASSERT(pos != -1);
		wxString newName = _("New directory");
		pathName.Replace(tmpName, newName);
		dlg.SetValue(pathName);
		dlg.SelectText(pos, pos + newName.Length());
	}

	const CServerPath oldPath = m_pDirectoryListing->path;

	if (dlg.ShowModal() != wxID_OK)
		return CServerPath();

	if (!m_pDirectoryListing || oldPath != m_pDirectoryListing->path ||
		!m_state.IsRemoteIdle())
	{
		wxBell();
		return CServerPath();
	}

	path = m_pDirectoryListing->path;
	if (!path.ChangePath(dlg.GetValue().ToStdWstring())) {
		wxBell();
		return CServerPath();
	}

	m_state.m_pCommandQueue->ProcessCommand(new CMkdirCommand(path));

	// Return name of the New Directory
	return path;
}

void CRemoteListView::OnMenuDelete(wxCommandEvent&)
{
	if (!m_state.IsRemoteIdle()) {
		wxBell();
		return;
	}

	int count_dirs = 0;
	int count_files = 0;
	bool selected_link = false;

	long item = -1;
	for (;;) {
		item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		if (!item) {
			continue;
		}
		if (item == -1) {
			break;
		}

		if (!IsItemValid(item)) {
			wxBell();
			return;
		}

		int index = GetItemIndex(item);
		if (index == -1) {
			continue;
		}
		if (m_fileData[index].comparison_flags == fill) {
			continue;
		}

		const CDirentry& entry = (*m_pDirectoryListing)[index];
		if (entry.is_dir()) {
			count_dirs++;
			if (entry.is_link()) {
				selected_link = true;
			}
		}
		else {
			count_files++;
		}
	}

	std::wstring question;
	if (!count_dirs) {
		question = fz::sprintf(fztranslate("Really delete %d file from the server?", "Really delete %d files from the server?", count_files), count_files);
	}
	else if (!count_files) {
		question = fz::sprintf(fztranslate("Really delete %d directory with its contents from the server?", "Really delete %d directories with their contents from the server?", count_dirs), count_dirs);
	}
	else {
		std::wstring files = fz::sprintf(fztranslate("%d file", "%d files", count_files), count_files);
		std::wstring dirs = fz::sprintf(fztranslate("%d directory with its contents", "%d directories with their contents", count_dirs), count_dirs);
		question = fz::sprintf(fztranslate("Really delete %s and %s from the server?"), files, dirs);
	}

	if (wxMessageBoxEx(question, _("Confirmation needed"), wxICON_QUESTION | wxYES_NO, this) != wxYES) {
		return;
	}

	bool follow_symlink = false;
	if (selected_link) {
		wxDialogEx dlg;
		if (!dlg.Load(this, _T("ID_DELETE_SYMLINK"))) {
			wxBell();
			return;
		}
		if (dlg.ShowModal() != wxID_OK) {
			return;
		}

		follow_symlink = XRCCTRL(dlg, "ID_RECURSE", wxRadioButton)->GetValue();
	}

	CFilterManager filter;
	if (CServer::ProtocolHasFeature(m_state.GetSite().server.GetProtocol(), ProtocolFeature::RecursiveDelete) && !filter.HasActiveRemoteFilters()) {
		std::vector<std::wstring> filesToDelete;
		filesToDelete.reserve(count_files);

		for (item = -1; ;) {
			item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
			if (!item) {
				continue;
			}
			if (item == -1) {
				break;
			}

			int index = GetItemIndex(item);
			if (index == -1) {
				continue;
			}
			if (m_fileData[index].comparison_flags == fill) {
				continue;
			}

			const CDirentry& entry = (*m_pDirectoryListing)[index];
			std::wstring const& name = entry.name;

			if (entry.is_dir()) {
				m_state.m_pCommandQueue->ProcessCommand(new CRemoveDirCommand(m_pDirectoryListing->path, name));
			}
			else {
				filesToDelete.push_back(name);
			}
		}

		if (!filesToDelete.empty()) {
			m_state.m_pCommandQueue->ProcessCommand(new CDeleteCommand(m_pDirectoryListing->path, std::move(filesToDelete)));
		}
	}
	else {
		CRemoteRecursiveOperation* pRecursiveOperation = m_state.GetRemoteRecursiveOperation();
		wxASSERT(pRecursiveOperation);

		std::vector<std::wstring> filesToDelete;
		filesToDelete.reserve(count_files);

		recursion_root root(m_pDirectoryListing->path, false);
		for (item = -1; ;) {
			item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
			if (!item) {
				continue;
			}
			if (item == -1) {
				break;
			}

			int index = GetItemIndex(item);
			if (index == -1) {
				continue;
			}
			if (m_fileData[index].comparison_flags == fill) {
				continue;
			}

			const CDirentry& entry = (*m_pDirectoryListing)[index];
			std::wstring const& name = entry.name;

			if (entry.is_dir() && (follow_symlink || !entry.is_link())) {
				CServerPath remotePath = m_pDirectoryListing->path;
				if (remotePath.AddSegment(name)) {
					root.add_dir_to_visit(m_pDirectoryListing->path, name, CLocalPath(), true);
				}
			}
			else {
				filesToDelete.push_back(name);
			}
		}

		if (!filesToDelete.empty()) {
			m_state.m_pCommandQueue->ProcessCommand(new CDeleteCommand(m_pDirectoryListing->path, std::move(filesToDelete)));
		}

		if (!root.empty()) {
			pRecursiveOperation->AddRecursionRoot(std::move(root));

			pRecursiveOperation->StartRecursiveOperation(CRecursiveOperation::recursive_delete,
														 filter.GetActiveFilters(), m_pDirectoryListing->path);
		}
	}
}

void CRemoteListView::OnMenuRename(wxCommandEvent&)
{
	if (GetEditControl()) {
		GetEditControl()->SetFocus();
		return;
	}

	if (!m_state.IsRemoteIdle()) {
		wxBell();
		return;
	}

	int item = GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (item <= 0) {
		wxBell();
		return;
	}

	if (GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED) != -1) {
		wxBell();
		return;
	}

	int index = GetItemIndex(item);
	if (index == -1 || m_fileData[index].comparison_flags == fill) {
		wxBell();
		return;
	}

	EditLabel(item);
}

void CRemoteListView::OnKeyDown(wxKeyEvent& event)
{
#ifdef __WXMAC__
#define CursorModifierKey wxMOD_CMD
#else
#define CursorModifierKey wxMOD_ALT
#endif

	int code = event.GetKeyCode();
	if (code == WXK_DELETE || code == WXK_NUMPAD_DELETE) {
		if (GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED) == -1) {
			wxBell();
			return;
		}

		wxCommandEvent tmp;
		OnMenuDelete(tmp);
	}
	else if (code == WXK_F2) {
		wxCommandEvent tmp;
		OnMenuRename(tmp);
	}
	else if (code == WXK_RIGHT && event.GetModifiers() == CursorModifierKey) {
		wxListEvent evt;
		evt.m_itemIndex = GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_FOCUSED);
		OnItemActivated(evt);
	}
	else if (code == WXK_DOWN && event.GetModifiers() == CursorModifierKey) {
		wxCommandEvent cmdEvent;
		OnMenuDownload(cmdEvent);
	}
	else if (code == 'N' && event.GetModifiers() == (wxMOD_CONTROL | wxMOD_SHIFT)) {
		MenuMkdir();
	}
	else if (code == 'F' && event.GetModifiers() == wxMOD_CONTROL) {
		if (m_parentView) {
			m_parentView->ShowSearchPanel();
		}
	}
	else {
		event.Skip();
	}
}

bool CRemoteListView::OnBeginRename(const wxListEvent& event)
{
	if (!m_state.IsRemoteIdle()) {
		wxBell();
		return false;
	}

	if (!m_pDirectoryListing) {
		wxBell();
		return false;
	}

	int item = event.GetIndex();
	if (!item) {
		return false;
	}

	int index = GetItemIndex(item);
	if (index == -1 || m_fileData[index].comparison_flags == fill) {
		return false;
	}

	return true;
}

bool CRemoteListView::OnAcceptRename(const wxListEvent& event)
{
	if (!m_state.IsRemoteIdle()) {
		wxBell();
		return false;
	}

	if (!m_pDirectoryListing) {
		wxBell();
		return false;
	}

	int item = event.GetIndex();
	if (!item) {
		return false;
	}

	int index = GetItemIndex(item);
	if (index == -1 || m_fileData[index].comparison_flags == fill) {
		wxBell();
		return false;
	}

	const CDirentry& entry = (*m_pDirectoryListing)[index];

	std::wstring newFile = event.GetLabel().ToStdWstring();

	CServerPath newPath = m_pDirectoryListing->path;
	if (!newPath.ChangePath(newFile, true)) {
		wxMessageBoxEx(_("Filename invalid"), _("Cannot rename file"), wxICON_EXCLAMATION);
		return false;
	}

	if (newPath == m_pDirectoryListing->path) {
		if (entry.name == newFile) {
			return false;
		}

		// Check if target file already exists
		for (size_t i = 0; i < m_pDirectoryListing->size(); ++i) {
			if (newFile == (*m_pDirectoryListing)[i].name) {
				if (wxMessageBoxEx(_("Target filename already exists, really continue?"), _("File exists"), wxICON_QUESTION | wxYES_NO) != wxYES) {
					return false;
				}
				break;
			}
		}
	}

	m_state.m_pCommandQueue->ProcessCommand(new CRenameCommand(m_pDirectoryListing->path, entry.name, newPath, newFile));

	return true;
}

void CRemoteListView::OnMenuChmod(wxCommandEvent&)
{
	Site const& site = m_state.GetSite();
	auto protocol = site.server.GetProtocol();
	
	if (!m_state.IsRemoteConnected() || !m_state.IsRemoteIdle()) {
		wxBell();
		return;
	}

	int fileCount = 0;
	int dirCount = 0;
	std::wstring name;

	char permissions[9] = {};

	long item = -1;
	for (;;) {
		item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		if (item == -1) {
			break;
		}

		if (!item) {
			return;
		}

		int index = GetItemIndex(item);
		if (index == -1) {
			return;
		}
		if (m_fileData[index].comparison_flags == fill) {
			continue;
		}

		const CDirentry& entry = (*m_pDirectoryListing)[index];

		if (entry.is_dir()) {
			dirCount++;
		}
		else {
			fileCount++;
		}
		name = entry.name;

		char file_perms[9];
		if (ChmodData::ConvertPermissions(*entry.permissions, file_perms)) {
			for (int i = 0; i < 9; i++) {
				if (!permissions[i] || permissions[i] == file_perms[i]) {
					permissions[i] = file_perms[i];
				}
				else {
					permissions[i] = -1;
				}
			}
		}
	}
	if (!dirCount && !fileCount) {
		wxBell();
		return;
	}

	for (int i = 0; i < 9; ++i) {
		if (permissions[i] == -1) {
			permissions[i] = 0;
		}
	}

	ChmodUICommand cmd = {
		this,
		permissions,
		fileCount,
		dirCount,
		name
	};
	auto handler = chmodHandlers[protocol];
	if (handler) {
		handler(cmd, m_state);
	}
	else {
		HandleGenericChmod(cmd);
	}
}

void CRemoteListView::HandleGenericChmod(ChmodUICommand &command)
{
	auto chmodData = std::make_unique<ChmodData>();
	auto chmodDialog = std::make_unique<CChmodDialog>(*chmodData);

	if (!chmodDialog->Create(command.parentWindow, command.fileCount, command.dirCount, command.name, command.permissions)) {
		return;
	}

	if (chmodDialog->ShowModal() != wxID_OK) {
		return;
	}

	// State may have changed while chmod dialog was shown
	if (!m_state.IsRemoteConnected() || !m_state.IsRemoteIdle()) {
		wxBell();
		return;
	}

	int const applyType = chmodData->GetApplyType();

	CRemoteRecursiveOperation* pRecursiveOperation = m_state.GetRemoteRecursiveOperation();
	wxASSERT(pRecursiveOperation);
	recursion_root root(m_pDirectoryListing->path, false);

	long item = -1;
	for (;;) {
		item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		if (item == -1) {
			break;
		}

		if (!item) {
			return;
		}

		int index = GetItemIndex(item);
		if (index == -1) {
			return;
		}
		if (m_fileData[index].comparison_flags == fill) {
			continue;
		}

		const CDirentry& entry = (*m_pDirectoryListing)[index];

		if (!applyType ||
			(!entry.is_dir() && applyType == 1) ||
			(entry.is_dir() && applyType == 2))
		{
			char newPermissions[9]{};
			bool res = ChmodData::ConvertPermissions(*entry.permissions, newPermissions);
			std::wstring const newPerms = chmodData->GetPermissions(res ? newPermissions : 0, entry.is_dir());

			m_state.m_pCommandQueue->ProcessCommand(new CChmodCommand(m_pDirectoryListing->path, entry.name, newPerms));
		}

		if (chmodDialog->Recursive() && entry.is_dir()) {
			root.add_dir_to_visit(m_pDirectoryListing->path, entry.name);
		}
	}

	if (chmodDialog->Recursive()) {
		chmodDialog.reset();
		pRecursiveOperation->SetChmodData(std::move(chmodData));
		pRecursiveOperation->AddRecursionRoot(std::move(root));
		CFilterManager filter;
		pRecursiveOperation->StartRecursiveOperation(CRecursiveOperation::recursive_chmod,
													 filter.GetActiveFilters(), m_pDirectoryListing->path);

		// Refresh listing. This gets done implicitely by the recursive operation, so
		// only it if not recursing.
		if (pRecursiveOperation->GetOperationMode() != CRecursiveOperation::recursive_chmod) {
			m_state.ChangeRemoteDir(m_pDirectoryListing->path);
		}
	}
	else {
		m_state.ChangeRemoteDir(m_pDirectoryListing->path, std::wstring(), 0, true);
	}

}

void CRemoteListView::ApplyCurrentFilter()
{
	CFilterManager const& filter = m_state.GetStateFilterManager();

	if (!filter.HasSameLocalAndRemoteFilters() && IsComparing()) {
		ExitComparisonMode();
	}

	if (m_fileData.size() <= 1) {
		return;
	}

	int focusedItem = -1;
	std::wstring focused;
	std::vector<std::wstring> selectedNames = RememberSelectedItems(focused, focusedItem);

	if (m_pFilelistStatusBar) {
		m_pFilelistStatusBar->UnselectAll();
	}

	int64_t totalSize{};
	int unknown_sizes = 0;
	int totalFileCount = 0;
	int totalDirCount = 0;
	int hidden = 0;

	std::wstring const path = m_pDirectoryListing->path.GetPath();

	m_indexMapping.clear();
	size_t const count = m_pDirectoryListing->size();
	m_indexMapping.push_back(count);
	for (size_t i = 0; i < count; ++i) {
		const CDirentry& entry = (*m_pDirectoryListing)[i];
		if (filter.FilenameFiltered(entry.name, path, entry.is_dir(), entry.size, false, 0, entry.time)) {
			++hidden;
			continue;
		}
	
		if (entry.is_dir()) {
			++totalDirCount;
		}
		else {
			if (entry.size == -1) {
				++unknown_sizes;
			}
			else {
				totalSize += entry.size;
			}
			++totalFileCount;
		}

		m_indexMapping.push_back(i);
	}

	if (m_pFilelistStatusBar) {
		m_pFilelistStatusBar->SetDirectoryContents(totalFileCount, totalDirCount, totalSize, unknown_sizes, hidden);
	}

	SetItemCount(m_indexMapping.size());

	SortList(-1, -1, false);

	if (IsComparing()) {
		m_originalIndexMapping.clear();
		RefreshComparison();
	}

	ReselectItems(selectedNames, focused, focusedItem);
	if (!IsComparing()) {
		RefreshListOnly();
	}
}

std::vector<std::wstring> CRemoteListView::RememberSelectedItems(std::wstring& focused, int & focusedItem)
{
	wxASSERT(GetItemCount() == (int)m_indexMapping.size());
	std::vector<std::wstring> selectedNames;
	// Remember which items were selected
#ifndef __WXMSW__
	// GetNextItem is O(n) if nothing is selected, GetSelectedItemCount() is O(1)
	if (GetSelectedItemCount())
#endif
	{
		int item = -1;
		for (;;) {
			item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
			if (item < 0) {
				break;
			}
			
			if (!item) {
				selectedNames.push_back(L"..");
			}
			else {
				int index = GetItemIndex(item);
				if (index != -1 && m_fileData[index].comparison_flags != fill) {
					const CDirentry& entry = (*m_pDirectoryListing)[index];
					if (entry.is_dir()) {
						selectedNames.push_back(L"d" + entry.name);
					}
					else {
						selectedNames.push_back(L"-" + entry.name);
					}
				}
			}
			SetSelection(item, false);
		}
	}

	focusedItem = -1;
	int item = GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_FOCUSED);
	if (item != -1) {
		int index = GetItemIndex(item);
		if (index != -1 && m_fileData[index].comparison_flags != fill) {
			if (!item) {
				focused = _T("..");
			}
			else {
				focused = (*m_pDirectoryListing)[index].name;
			}
		}
		focusedItem = item;
	}

	return selectedNames;
}

void CRemoteListView::ReselectItems(std::vector<std::wstring>& selectedNames, std::wstring focused, int focusedItem, bool ensureVisible)
{
	if (!GetItemCount()) {
		return;
	}

	// Reselect previous items if neccessary.
	// Sorting direction did not change. We just have to scan through items once

	if (focused == L"..") {
		focused.clear();
		SetItemState(0, wxLIST_STATE_FOCUSED, wxLIST_STATE_FOCUSED);
	}

	if (selectedNames.empty()) {
		if (focused.empty()) {
			return;
		}

		for (unsigned int i = 1; i < m_indexMapping.size(); ++i) {
			const int index = m_indexMapping[i];
			if (m_fileData[index].comparison_flags == fill) {
				continue;
			}

			if ((*m_pDirectoryListing)[index].name == focused) {
				SetItemState(i, wxLIST_STATE_FOCUSED, wxLIST_STATE_FOCUSED);
				if (ensureVisible) {
					EnsureVisible(i);
				}
				return;
			}
		}

		if (focusedItem != -1 && GetItemCount() != 0) {
			if (focusedItem >= GetItemCount()) {
				focusedItem = GetItemCount() - 1;
			}
			SetItemState(focusedItem, wxLIST_STATE_FOCUSED, wxLIST_STATE_FOCUSED);
		}

		return;
	}
	else {
		auto nameIt = selectedNames.cbegin();
		if (*nameIt == L"..") {
			++nameIt;
			SetItemState(0, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
		}

		int firstSelected = -1;

		// Reselect previous items if neccessary.
		// Sorting direction did not change. We just have to scan through items once
		unsigned int i = 0;
		for (; nameIt != selectedNames.cend(); ++nameIt) {
			std::wstring const& selectedName = *nameIt;
			while (++i < m_indexMapping.size()) {
				int index = GetItemIndex(i);
				if (index == -1 || m_fileData[index].comparison_flags == fill) {
					continue;
				}
				CDirentry const& entry = (*m_pDirectoryListing)[index];
				if (entry.name == focused) {
					SetItemState(i, wxLIST_STATE_FOCUSED, wxLIST_STATE_FOCUSED);
					if (ensureVisible) {
						EnsureVisible(i);
					}
					focused.clear();
					focusedItem = -1;
				}
				if (entry.is_dir() && selectedName == (_T("d") + entry.name)) {
					if (firstSelected == -1) {
						firstSelected = i;
					}
					if (m_pFilelistStatusBar) {
						m_pFilelistStatusBar->SelectDirectory();
					}
					SetSelection(i, true);
					break;
				}
				else if (selectedName == (_T("-") + entry.name)) {
					if (firstSelected == -1) {
						firstSelected = i;
					}
					if (m_pFilelistStatusBar) {
						m_pFilelistStatusBar->SelectFile(entry.size);
					}
					SetSelection(i, true);
					break;
				}
			}
			if (i == m_indexMapping.size()) {
				break;
			}
		}
		if (!focused.empty()) {
			if (firstSelected != -1) {
				SetItemState(firstSelected, wxLIST_STATE_FOCUSED, wxLIST_STATE_FOCUSED);
			}
			else {
				if (GetItemCount() != 0) {
					if (focusedItem == -1) {
						focusedItem = 0;
					}
					else if (focusedItem >= GetItemCount()) {
						focusedItem = GetItemCount() - 1;
					}
					SetItemState(focusedItem, wxLIST_STATE_FOCUSED, wxLIST_STATE_FOCUSED);
				}
			}
		}
	}
}

void CRemoteListView::OnSize(wxSizeEvent& event)
{
	event.Skip();
	if (m_pInfoText) {
		m_pInfoText->Reposition();
	}
}

void CRemoteListView::OnStateChange(t_statechange_notifications notification, std::wstring const& data, const void* data2)
{
	if (notification == STATECHANGE_REMOTE_DIR) {
		SetDirectoryListing(m_state.GetRemoteDir());
	}
	else if (notification == STATECHANGE_REMOTE_LINKNOTDIR) {
		wxASSERT(data2);
		LinkIsNotDir(*(CServerPath*)data2, data);
	}
	else if (notification == STATECHANGE_SERVER) {
		if (m_windowTinter) {
			m_windowTinter->SetBackgroundTint(m_state.GetSite().m_colour);
		}
		if (m_pInfoText) {
			m_pInfoText->SetBackgroundTint(m_state.GetSite().m_colour);
		}
	}
	else {
		wxASSERT(notification == STATECHANGE_APPLYFILTER);
		ApplyCurrentFilter();
	}
}

void CRemoteListView::SetInfoText()
{
	if (!m_pInfoText) {
		return;
	}

	wxString text;
	if (!IsComparing()) {
		if (!m_pDirectoryListing) {
			text = _("Not connected to any server");
		}
		else if (m_pDirectoryListing->failed()) {
			text = _("Directory listing failed");
		}
		else if (!m_pDirectoryListing->size()) {
			text = _("Empty directory listing");
		}
	}


	if (text.empty()) {
		m_pInfoText->Hide();
	}
	else {
		m_pInfoText->SetText(text);
		m_pInfoText->Reposition();
		m_pInfoText->Show();
	}
}

void CRemoteListView::OnBeginDrag(wxListEvent&)
{
	if (COptions::Get()->GetOptionVal(OPTION_DND_DISABLED) != 0) {
		return;
	}

	if (!m_state.IsRemoteIdle()) {
		wxBell();
		return;
	}

	if (GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED) == -1) {
		// Nothing selected
		return;
	}

	bool idle = m_state.m_pCommandQueue->Idle();

	long item = -1;
	size_t count = 0;
	for (;;) {
		item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		if (item == -1) {
			break;
		}

		if (!item) {
			// Can't drag ".."
			wxBell();
			return;
		}

		int index = GetItemIndex(item);
		if (index == -1 || m_fileData[index].comparison_flags == fill) {
			continue;
		}
		if ((*m_pDirectoryListing)[index].is_dir() && !idle) {
			// Drag could result in recursive operation, don't allow at this point
			wxBell();
			return;
		}
		++count;
	}
	if (!count) {
		wxBell();
		return;
	}

	wxDataObjectComposite object;

	Site site = m_state.GetSite(); // Make a copy as DoDragDrop later runs the event loop
	if (!site) {
		return;
	}
	CServerPath const path = m_pDirectoryListing->path;

	CRemoteDataObject *pRemoteDataObject = new CRemoteDataObject(site, m_pDirectoryListing->path);
	pRemoteDataObject->Reserve(count);

	CDragDropManager* pDragDropManager = CDragDropManager::Init();
	pDragDropManager->pDragSource = this;
	pDragDropManager->site = site;
	pDragDropManager->remoteParent = m_pDirectoryListing->path;

	// Add files to remote data object
	item = -1;
	for (;;) {
		item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
		if (item == -1) {
			break;
		}

		int index = GetItemIndex(item);
		if (index == -1 || m_fileData[index].comparison_flags == fill) {
			continue;
		}
		const CDirentry& entry = (*m_pDirectoryListing)[index];

		pRemoteDataObject->AddFile(entry.name, entry.is_dir(), entry.size, entry.is_link());
	}

	pRemoteDataObject->Finalize();

	object.Add(pRemoteDataObject, true);

#if FZ3_USESHELLEXT
	std::unique_ptr<CShellExtensionInterface> ext = CShellExtensionInterface::CreateInitialized();
	if (ext) {
		const wxString& file = ext->GetDragDirectory();

		wxASSERT(!file.empty());

		wxFileDataObject *pFileDataObject = new wxFileDataObject;
		pFileDataObject->AddFile(file);

		object.Add(pFileDataObject);
	}
#endif

	CLabelEditBlocker b(*this);

	wxDropSource source(this);
	source.SetData(object);

	int res = source.DoDragDrop();

	pDragDropManager->Release();

	if (res != wxDragCopy) {
		return;
	}

#if FZ3_USESHELLEXT
	if (ext) {
		if (!pRemoteDataObject->DidSendData()) {
			Site newSite = m_state.GetSite();
			if (!m_state.IsRemoteIdle() ||
				!newSite || newSite.server != pRemoteDataObject->GetSite().server ||
				!m_pDirectoryListing || m_pDirectoryListing->path != path)
			{
				// Remote listing has changed since drag started
				wxBell();
				return;
			}

			// Same checks as before
			idle = m_state.m_pCommandQueue->Idle();

			item = -1;
			for (;;) {
				item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
				if (item == -1) {
					break;
				}

				if (!item) {
					// Can't drag ".."
					wxBell();
					return;
				}

				int index = GetItemIndex(item);
				if (index == -1 || m_fileData[index].comparison_flags == fill) {
					continue;
				}
				if ((*m_pDirectoryListing)[index].is_dir() && !idle) {
					// Drag could result in recursive operation, don't allow at this point
					wxBell();
					return;
				}
			}

			CLocalPath target(ext->GetTarget().ToStdWstring());
			if (target.empty()) {
				ext.reset(); // Release extension before the modal message box
				wxMessageBoxEx(_("Could not determine the target of the Drag&Drop operation.\nEither the shell extension is not installed properly or you didn't drop the files into an Explorer window."));
				return;
			}

			TransferSelectedFiles(target, false);
		}
	}
#endif
}

void CRemoteListView::OnMenuEdit(wxCommandEvent&)
{
	if (!m_state.IsRemoteConnected() || !m_pDirectoryListing) {
		wxBell();
		return;
	}

	long item = -1;

	std::vector<CEditHandler::FileData> selected_items;
	while ((item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != -1) {
		if (!item) {
			wxBell();
			return;
		}

		int index = GetItemIndex(item);
		if (index == -1 || m_fileData[index].comparison_flags == fill) {
			continue;
		}

		const CDirentry& entry = (*m_pDirectoryListing)[index];
		if (entry.is_dir()) {
			wxBell();
			return;
		}

		selected_items.push_back({entry.name, entry.size});
	}

	CEditHandler* pEditHandler = CEditHandler::Get();
	if (!pEditHandler) {
		wxBell();
		return;
	}

	CServerPath const path = m_pDirectoryListing->path;
	pEditHandler->Edit(CEditHandler::remote, selected_items, path, m_state.GetSite(), this);
}

#ifdef __WXDEBUG__
void CRemoteListView::ValidateIndexMapping()
{
	// This ensures that the index mapping is a bijection.
	// Beware:
	// - NO filter may be used!
	// - Doesn't work in comparison mode

	char* buffer = new char[m_pDirectoryListing->size() + 1];
	memset(buffer, 0, m_pDirectoryListing->size() + 1);

	// Injectivity
	for (auto const& item : m_indexMapping) {
		if (item > m_pDirectoryListing->size()) {
			abort();
		}
		else if (buffer[item]) {
			abort();
		}

		buffer[item] = 1;
	}

	// Surjectivity
	for (size_t i = 0; i < m_pDirectoryListing->size() + 1; ++i) {
		wxASSERT(buffer[i] != 0);
	}

	delete [] buffer;
}
#endif

bool CRemoteListView::CanStartComparison()
{
	return m_pDirectoryListing != 0;
}

void CRemoteListView::StartComparison()
{
	if (m_sortDirection || m_sortColumn != 0) {
		wxASSERT(m_originalIndexMapping.empty());
		SortList(0, 0);
	}

	ComparisonRememberSelections();

	if (m_originalIndexMapping.empty()) {
		m_originalIndexMapping.swap(m_indexMapping);
	}
	else {
		m_indexMapping.clear();
	}

	m_comparisonIndex = -1;

	if (m_fileData.empty() || m_fileData.back().comparison_flags != fill) {
		CGenericFileData data;
		data.icon = -1;
		data.comparison_flags = fill;
		m_fileData.push_back(data);
	}
}

bool CRemoteListView::get_next_file(std::wstring_view & name, std::wstring & path, bool& dir, int64_t& size, fz::datetime& date)
{
	if (++m_comparisonIndex >= (int)m_originalIndexMapping.size()) {
		return false;
	}

	const unsigned int index = m_originalIndexMapping[m_comparisonIndex];
	if (index >= m_fileData.size()) {
		return false;
	}

	if (index == m_pDirectoryListing->size()) {
		name = _T("..");
		dir = true;
		size = -1;
		return true;
	}

	CDirentry const& entry = (*m_pDirectoryListing)[index];

	name = entry.name;
	dir = entry.is_dir();
	size = entry.size;
	date = entry.time;

	return true;
}

void CRemoteListView::FinishComparison()
{
	SetInfoText();

	SetItemCount(m_indexMapping.size());

	ComparisonRestoreSelections();

	RefreshListOnly();
}

wxListItemAttr* CRemoteListView::OnGetItemAttr(long item) const
{
	CRemoteListView *pThis = const_cast<CRemoteListView *>(this);
	int index = GetItemIndex(item);

	if (index == -1) {
		return 0;
	}

#ifndef __WXMSW__
	if (item == m_dropTarget) {
		return &pThis->m_dropHighlightAttribute;
	}
#endif

	const CGenericFileData& data = m_fileData[index];

	switch (data.comparison_flags)
	{
	case different:
		return &pThis->m_comparisonBackgrounds[0];
	case lonely:
		return &pThis->m_comparisonBackgrounds[1];
	case newer:
		return &pThis->m_comparisonBackgrounds[2];
	default:
		return 0;
	}
}

wxString CRemoteListView::GetItemText(int item, unsigned int column)
{
	int index = GetItemIndex(item);
	if (index == -1) {
		return wxString();
	}

	if (!column) {
		if ((size_t)index == m_pDirectoryListing->size()) {
			return _T("..");
		}
		else if ((size_t)index < m_pDirectoryListing->size()) {
			return (*m_pDirectoryListing)[index].name;
		}
		else {
			return wxString();
		}
	}
	if (!item) {
		return wxString(); //.. has no attributes
	}

	if ((size_t)index >= m_pDirectoryListing->size()) {
		return wxString();
	}

	if (column == 1) {
		const CDirentry& entry = (*m_pDirectoryListing)[index];
		if (entry.is_dir() || entry.size < 0) {
			return wxString();
		}
		else {
			return CSizeFormat::Format(entry.size);
		}
	}
	else if (column == 2) {
		CGenericFileData& data = m_fileData[index];
		if (data.fileType.empty()) {
			const CDirentry& entry = (*m_pDirectoryListing)[index];
			if (m_pDirectoryListing->path.GetType() == VMS) {
				data.fileType = GetType(StripVMSRevision(entry.name), entry.is_dir());
			}
			else {
				data.fileType = GetType(entry.name, entry.is_dir());
			}
		}

		return data.fileType;
	}
	else if (column == 3) {
		const CDirentry& entry = (*m_pDirectoryListing)[index];
		return CTimeFormat::Format(entry.time);
	}
	else if (column == 4) {
		return *(*m_pDirectoryListing)[index].permissions;
	}
	else if (column == 5) {
		return *(*m_pDirectoryListing)[index].ownerGroup;
	}
	return wxString();
}

std::unique_ptr<CFileListCtrlSortBase> CRemoteListView::GetSortComparisonObject()
{
	CFileListCtrlSort<CDirectoryListing>::DirSortMode dirSortMode = GetDirSortMode();
	CFileListCtrlSort<CDirectoryListing>::NameSortMode nameSortMode = GetNameSortMode();

	CDirectoryListing const& directoryListing = *m_pDirectoryListing;
	if (!m_sortDirection) {
		if (m_sortColumn == 1) {
			return std::make_unique<CFileListCtrlSortSize<CDirectoryListing, CGenericFileData>>(directoryListing, m_fileData, dirSortMode, nameSortMode, this);
		}
		else if (m_sortColumn == 2) {
			return std::make_unique<CFileListCtrlSortType<CDirectoryListing, CGenericFileData>>(directoryListing, m_fileData, dirSortMode, nameSortMode, this);
		}
		else if (m_sortColumn == 3) {
			return std::make_unique<CFileListCtrlSortTime<CDirectoryListing, CGenericFileData>>(directoryListing, m_fileData, dirSortMode, nameSortMode, this);
		}
		else if (m_sortColumn == 4) {
			return std::make_unique<CFileListCtrlSortPermissions<CDirectoryListing, CGenericFileData>>(directoryListing, m_fileData, dirSortMode, nameSortMode, this);
		}
		else if (m_sortColumn == 5) {
			return std::make_unique<CFileListCtrlSortOwnerGroup<CDirectoryListing, CGenericFileData>>(directoryListing, m_fileData, dirSortMode, nameSortMode, this);
		}
		else {
			return std::make_unique<CFileListCtrlSortName<CDirectoryListing, CGenericFileData>>(directoryListing, m_fileData, dirSortMode, nameSortMode, this);
		}
	}
	else {
		if (m_sortColumn == 1) {
			return std::make_unique<CReverseSort<CFileListCtrlSortSize<CDirectoryListing, CGenericFileData>, CGenericFileData>>(directoryListing, m_fileData, dirSortMode, nameSortMode, this);
		}
		else if (m_sortColumn == 2) {
			return std::make_unique<CReverseSort<CFileListCtrlSortType<CDirectoryListing, CGenericFileData>, CGenericFileData>>(directoryListing, m_fileData, dirSortMode, nameSortMode, this);
		}
		else if (m_sortColumn == 3) {
			return std::make_unique<CReverseSort<CFileListCtrlSortTime<CDirectoryListing, CGenericFileData>, CGenericFileData>>(directoryListing, m_fileData, dirSortMode, nameSortMode, this);
		}
		else if (m_sortColumn == 4) {
			return std::make_unique<CReverseSort<CFileListCtrlSortPermissions<CDirectoryListing, CGenericFileData>, CGenericFileData>>(directoryListing, m_fileData, dirSortMode, nameSortMode, this);
		}
		else if (m_sortColumn == 5) {
			return std::make_unique<CReverseSort<CFileListCtrlSortOwnerGroup<CDirectoryListing, CGenericFileData>, CGenericFileData>>(directoryListing, m_fileData, dirSortMode, nameSortMode, this);
		}
		else {
			return std::make_unique<CReverseSort<CFileListCtrlSortName<CDirectoryListing, CGenericFileData>, CGenericFileData>>(directoryListing, m_fileData, dirSortMode, nameSortMode, this);
		}
	}
}

void CRemoteListView::OnExitComparisonMode()
{
	CFileListCtrl<CGenericFileData>::OnExitComparisonMode();
	SetInfoText();
}

bool CRemoteListView::ItemIsDir(int index) const
{
	return (*m_pDirectoryListing)[index].is_dir();
}

int64_t CRemoteListView::ItemGetSize(int index) const
{
	return (*m_pDirectoryListing)[index].size;
}

void CRemoteListView::LinkIsNotDir(CServerPath const& path, std::wstring const& link)
{
	if (m_pLinkResolveState && m_pLinkResolveState->remote_path == path && m_pLinkResolveState->link == link) {
		std::wstring localFile = CQueueView::ReplaceInvalidCharacters(link);
		if (m_pDirectoryListing->path.GetType() == VMS && COptions::Get()->GetOptionVal(OPTION_STRIP_VMS_REVISION)) {
			localFile = StripVMSRevision(localFile);
		}
		m_pQueue->QueueFile(false, true,
			link, (link != localFile) ? localFile : std::wstring(),
			m_pLinkResolveState->local_path, m_pLinkResolveState->remote_path, m_pLinkResolveState->site, -1);
		m_pQueue->QueueFile_Finish(true);
	}

	m_pLinkResolveState.reset();
}

void CRemoteListView::OnMenuGeturl(wxCommandEvent& event)
{
	if (!m_pDirectoryListing) {
		return;
	}

	Site const& site = m_state.GetSite();
	if (!site) {
		return;
	}

	long item = -1;

	std::list<CDirentry> selected_item_list;
	while ((item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != -1) {
		if (!item) {
			wxBell();
			return;
		}

		int index = GetItemIndex(item);
		if (index == -1 || m_fileData[index].comparison_flags == fill) {
			continue;
		}

		CDirentry const& entry = (*m_pDirectoryListing)[index];

		selected_item_list.push_back(entry);
	}
	if (selected_item_list.empty()) {
		wxBell();
		return;
	}

	if (!wxTheClipboard->Open()) {
		wxMessageBoxEx(_("Could not open clipboard"), _("Could not copy URLs"), wxICON_EXCLAMATION);
		return;
	}

	const CServerPath& path = m_pDirectoryListing->path;

	std::wstring const url = site.Format((event.GetId() == XRCID("ID_GETURL_PASSWORD")) ? ServerFormat::url_with_password : ServerFormat::url);

	auto getUrl = [](std::wstring const& serverPart, CServerPath const& path, std::wstring const& name) {
		std::wstring url = serverPart;

		auto const pathPart = fz::percent_encode_w(path.FormatFilename(name, false), true);
		if (!pathPart.empty() && pathPart[0] != '/') {
			url += '/';
		}
		url += pathPart;

		return url;
	};

	std::wstring urls;
	if (selected_item_list.size() == 1) {
		urls = getUrl(url, path, selected_item_list.front().name);
	}
	else {
		for (auto const& entry : selected_item_list) {
			urls += getUrl(url, path, entry.name);
#ifdef __WXMSW__
			urls += L"\r\n";
#else
			urls += L"\n";
#endif
		}
	}
	wxTheClipboard->SetData(new wxURLDataObject(urls));

	wxTheClipboard->Flush();
	wxTheClipboard->Close();
}

#ifdef __WXMSW__
int CRemoteListView::GetOverlayIndex(int item)
{
	int index = GetItemIndex(item);
	if (index == -1) {
		return 0;
	}
	if ((size_t)index >= m_pDirectoryListing->size()) {
		return 0;
	}

	if ((*m_pDirectoryListing)[index].is_link()) {
		return GetLinkOverlayIndex();
	}

	return 0;
}
#endif

void CRemoteListView::OnMenuRefresh(wxCommandEvent&)
{
	m_state.RefreshRemote();
}

void CRemoteListView::OnNavigationEvent(bool forward)
{
	if (!forward) {
		if (!m_state.IsRemoteIdle(true)) {
			wxBell();
			return;
		}

		if (!m_pDirectoryListing) {
			wxBell();
			return;
		}

		m_state.ChangeRemoteDir(m_pDirectoryListing->path, _T(".."));
	}
}

void CRemoteListView::OnMenuNewfile(wxCommandEvent&)
{
	if (!m_state.IsRemoteIdle() || !m_pDirectoryListing) {
		wxBell();
		return;
	}

	CInputDialog dlg;
	if (!dlg.Create(this, _("Create empty file"), _("Please enter the name of the file which should be created:"))) {
		return;
	}

	if (dlg.ShowModal() != wxID_OK) {
		return;
	}

	if (dlg.GetValue().empty()) {
		wxBell();
		return;
	}

	std::wstring newFileName = dlg.GetValue().ToStdWstring();

	// Check if target file already exists
	for (size_t i = 0; i < m_pDirectoryListing->size(); ++i) {
		if (newFileName == (*m_pDirectoryListing)[i].name) {
			wxMessageBoxEx(_("Target filename already exists!"));
			return;
		}
	}

	CEditHandler* edithandler = CEditHandler::Get(); // Used to get the temporary folder

	std::wstring const emptyfile_name = L"empty_file_yq744zm";
	std::wstring emptyfile = edithandler->GetLocalDirectory() + emptyfile_name;

	// Create the empty temporary file and update its modification time
	{
		auto const fn = fz::to_native(emptyfile);
		fz::file f(fn, fz::file::writing, fz::file::existing);
		f.close();
		fz::local_filesys::set_modification_time(fn, fz::datetime::now());
	}

	Site const& site = m_state.GetSite();
	if (!site) {
		wxBell();
		return;
	}

	CFileTransferCommand *cmd = new CFileTransferCommand(emptyfile, m_pDirectoryListing->path, newFileName, false, CFileTransferCommand::t_transferSettings());
	m_state.m_pCommandQueue->ProcessCommand(cmd);
}
