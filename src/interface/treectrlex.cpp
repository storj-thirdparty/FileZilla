#include <filezilla.h>
#include "treectrlex.h"

IMPLEMENT_DYNAMIC_CLASS(wxTreeCtrlEx, wxNavigationEnabled<wxTreeCtrl>)

#ifdef __WXMAC__
BEGIN_EVENT_TABLE(wxTreeCtrlEx, wxNavigationEnabled<wxTreeCtrl>)
EVT_CHAR(wxTreeCtrlEx::OnChar)
END_EVENT_TABLE()
#endif

wxTreeCtrlEx::wxTreeCtrlEx()
	: m_nameSortMode(
#ifdef __WXMSW__
		CFileListCtrlSortBase::namesort_caseinsensitive
#else
		CFileListCtrlSortBase::namesort_casesensitive
#endif
	)
{
}

wxTreeCtrlEx::wxTreeCtrlEx(wxWindow *parent, wxWindowID id /*=wxID_ANY*/,
			   const wxPoint& pos /*=wxDefaultPosition*/,
			   const wxSize& size /*=wxDefaultSize*/,
			   long style /*=wxTR_HAS_BUTTONS|wxTR_LINES_AT_ROOT*/)
	: m_nameSortMode(
#ifdef __WXMSW__
		CFileListCtrlSortBase::namesort_caseinsensitive
#else
		CFileListCtrlSortBase::namesort_casesensitive
#endif
	)
{
	Create(parent, id, pos, size, style);
	SetBackgroundStyle(wxBG_STYLE_SYSTEM);
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

	wxTreeItemIdValue unused;

	wxTreeItemId item = GetRootItem();
	while (item) {
		if (IsSelected(item)) {
			ret.push_back(item);
		}
		wxTreeItemId next = GetFirstChild(item, unused);
		if (!next) {
			next = GetNextSibling(item);
			while (!next && item) {
				item = GetItemParent(item);
				if (item) {
					next = GetNextSibling(item);
				}
			}
		}
		item = next;
	}

	return ret;
}

void wxTreeCtrlEx::SafeSelectItem(wxTreeItemId const& item)
{
	if (!item) {
		++m_setSelection;
		UnselectAll();
		--m_setSelection;
	}
	else {
		std::vector<wxTreeItemId> selections;
		if (HasFlag(wxTR_MULTIPLE)) {
			selections = GetSelections();
			UnselectAll();
			SetFocusedItem(item);
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

wxTreeItemId wxTreeCtrlEx::GetNextItemSimple(wxTreeItemId const& item) const
{
	if (item.IsOk() && ItemHasChildren(item) && IsExpanded(item)) {
		wxTreeItemIdValue cookie;
		return GetFirstChild(item, cookie);
	}
	else {
		wxTreeItemId cur = item;
		wxTreeItemId next = GetNextSibling(cur);
		while (!next.IsOk() && cur.IsOk()) {
			cur = GetItemParent(cur);
			if (cur.IsOk()) {
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

	switch (m_nameSortMode)
	{
	case CFileListCtrlSortBase::namesort_casesensitive:
		return CFileListCtrlSortBase::CmpCase(label1, label2);

	default:
	case CFileListCtrlSortBase::namesort_caseinsensitive:
		return CFileListCtrlSortBase::CmpNoCase(label1, label2);

	case CFileListCtrlSortBase::namesort_natural:
		return CFileListCtrlSortBase::CmpNatural(label1, label2);
	}
}
