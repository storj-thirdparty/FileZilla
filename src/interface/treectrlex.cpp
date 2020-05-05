#include <filezilla.h>
#include "treectrlex.h"

#ifdef __WXMAC__
BEGIN_EVENT_TABLE(wxTreeCtrlEx, wxNavigationEnabled<wxTreeCtrl>)
EVT_CHAR(wxTreeCtrlEx::OnChar)
END_EVENT_TABLE()
#endif

// Needed for OnCompareItems to work on Windows. Bad library design, why not use normal RTTI?
IMPLEMENT_CLASS(wxTreeCtrlEx, wxNavigationEnabled<wxTreeCtrl>)

wxTreeCtrlEx::wxTreeCtrlEx()
{
#ifdef __WXMSW__
	sortFunction_ = CFileListCtrlSortBase::GetCmpFunction(CFileListCtrlSortBase::namesort_caseinsensitive);
#else
	sortFunction_ = CFileListCtrlSortBase::GetCmpFunction(CFileListCtrlSortBase::namesort_casesensitive);
#endif
}

wxTreeCtrlEx::wxTreeCtrlEx(wxWindow *parent, wxWindowID id /*=wxID_ANY*/,
			   const wxPoint& pos /*=wxDefaultPosition*/,
			   const wxSize& size /*=wxDefaultSize*/,
			   long style /*=wxTR_HAS_BUTTONS|wxTR_LINES_AT_ROOT*/)
{
#ifdef __WXMSW__
	sortFunction_ = CFileListCtrlSortBase::GetCmpFunction(CFileListCtrlSortBase::namesort_caseinsensitive);
#else
	sortFunction_ = CFileListCtrlSortBase::GetCmpFunction(CFileListCtrlSortBase::namesort_casesensitive);
#endif

	Create(parent, id, pos, size, style);
	SetBackgroundStyle(wxBG_STYLE_SYSTEM);

	Bind(wxEVT_CHAR, [this](wxKeyEvent& evt) {
		auto key = evt.GetUnicodeKey();
		if (key && key > 32) {
			inPrefixSearch_ = true;
		}
		evt.Skip();
	});
	Bind(wxEVT_KEY_UP, [this](wxKeyEvent& evt) {
		inPrefixSearch_ = false;
		evt.Skip();
	});
}

wxTreeItemId wxTreeCtrlEx::GetSelection() const
{
	if (HasFlag(wxTR_MULTIPLE)) {
		auto const selections = GetSelections();
		if (selections.size() != 1) {
			return wxTreeItemId();
		}
		return selections.front();
	}
	else {
		return wxTreeCtrl::GetSelection();
	}
}

std::vector<wxTreeItemId> wxTreeCtrlEx::GetSelections() const
{
	std::vector<wxTreeItemId> ret;

	// Not only does wxTreeCtrl::GetSelections have a terrible API, it also returns items in a really weird order.
	// Sadly on MSW, the native TreeView_GetNextSelected can't be used either, it uses a weird and also nodeterministic order.
	//
	// Traverse the tree ourselves, in a nice, deterministic depth-first approach.
	wxTreeItemId item = GetRootItem();
	if (item && HasFlag(wxTR_HIDE_ROOT)) {
		item = GetNextItemSimple(item, true);
	}
	while (item) {
		if (IsSelected(item)) {
			ret.push_back(item);
		}
		item = GetNextItemSimple(item, true);
	}

	return ret;
}

void wxTreeCtrlEx::SafeSelectItem(wxTreeItemId const& item, bool clearSelection)
{
	if (!item) {
		++m_setSelection;
		UnselectAll();
		--m_setSelection;
	}
	else {
		std::vector<wxTreeItemId> selections;
		if (HasFlag(wxTR_MULTIPLE)) {
			++m_setSelection;
			selections = GetSelections();
			if (clearSelection) {
				UnselectAll();
			}
			
			if (clearSelection || selections.empty()) {
				SetFocusedItem(item);
			}
			--m_setSelection;
		}
		else {
			auto old = GetSelection();
			if (old) {
				selections.push_back(old);
			}
		}

		++m_setSelection;
		SelectItem(item);
		--m_setSelection;

		if (selections.empty()) {
			EnsureVisible(item);
		}
		else if (clearSelection) {
			bool found{};
			for (auto const& old : selections) {
				if (item == old) {
					found = true;
				}
			}
			if (!found) {
				EnsureVisible(item);
			}
		}
	}
}

#ifdef __WXMAC__
void wxTreeCtrlEx::OnChar(wxKeyEvent& event)
{
	if (event.GetKeyCode() != WXK_TAB) {
		event.Skip();
		return;
	}

	HandleAsNavigationKey(event);
}
#endif

wxTreeItemId wxTreeCtrlEx::GetFirstItem() const
{
	wxTreeItemId root = GetRootItem();
	if (root.IsOk() && GetWindowStyle() & wxTR_HIDE_ROOT) {
		wxTreeItemIdValue cookie;
		root = GetFirstChild(root, cookie);
	}

	return root;
}

wxTreeItemId wxTreeCtrlEx::GetLastItem() const
{
	wxTreeItemId cur = GetRootItem();
	if (cur.IsOk() && GetWindowStyle() & wxTR_HIDE_ROOT) {
		cur = GetLastChild(cur);
	}

	while (cur.IsOk() && HasChildren(cur) && IsExpanded(cur)) {
		cur = GetLastChild(cur);
	}

	return cur;
}

wxTreeItemId wxTreeCtrlEx::GetBottomItem() const
{
	wxTreeItemId cur = GetFirstVisibleItem();
	if (cur) {
		wxTreeItemId next;
		while ((next = GetNextVisible(cur)).IsOk()) {
			cur = next;
		}
	}
	return cur;
}

wxTreeItemId wxTreeCtrlEx::GetNextItemSimple(wxTreeItemId const& item, bool includeCollapsed) const
{
	if (item.IsOk() && ItemHasChildren(item) && (includeCollapsed || IsExpanded(item))) {
		wxTreeItemIdValue cookie;
		return GetFirstChild(item, cookie);
	}
	else {
		wxTreeItemId cur = item;
		wxTreeItemId next = GetNextSibling(cur);
		while (!next.IsOk() && cur.IsOk()) {
			cur = GetItemParent(cur);
			if (cur.IsOk()) {
				if (HasFlag(wxTR_HIDE_ROOT) && cur == GetRootItem()) {
					break;
				}
				next = GetNextSibling(cur);
			}
		}
		return next;
	}
}

wxTreeItemId wxTreeCtrlEx::GetPrevItemSimple(wxTreeItemId const& item) const
{
	wxTreeItemId cur = GetPrevSibling(item);
	if (cur.IsOk()) {
		while (cur.IsOk() && HasChildren(cur) && IsExpanded(cur)) {
			cur = GetLastChild(cur);
		}
	}
	else {
		cur = GetItemParent(item);
		if (cur.IsOk() && cur == GetRootItem() && (GetWindowStyle() & wxTR_HIDE_ROOT)) {
			cur = wxTreeItemId();
		}
	}
	return cur;
}

int wxTreeCtrlEx::OnCompareItems(wxTreeItemId const& item1, wxTreeItemId const& item2)
{
	wxString const& label1 = GetItemText(item1);
	wxString const& label2 = GetItemText(item2);
	auto const label1v = std::wstring_view(label1.data(), label1.size());
	auto const label2v = std::wstring_view(label2.data(), label2.size());

	return sortFunction_(label1v, label2v);
}

void wxTreeCtrlEx::Resort()
{
	std::vector<wxTreeItemId> work;
	if (!GetRootItem()) {
		return;
	}
	work.emplace_back(GetRootItem());
	while (!work.empty()) {
		wxTreeItemId item = work.back();
		work.pop_back();
		SortChildren(item);
		wxTreeItemIdValue cookie;
		for (wxTreeItemId child = GetFirstChild(item, cookie); child; child = GetNextSibling(child)) {
			work.push_back(child);
		}
	}
}