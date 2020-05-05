#ifndef FILEZILLA_INTERFACE_FILTER_HEADER
#define FILEZILLA_INTERFACE_FILTER_HEADER

#include "dialogex.h"

#include <memory>
#include <regex>

enum t_filterType
{
	filter_name = 0x01,
	filter_size = 0x02,
	filter_attributes = 0x04,
	filter_permissions = 0x08,
	filter_path = 0x10,
	filter_date = 0x20,
#ifdef __WXMSW__
	filter_meta = filter_attributes,
	filter_foreign = filter_permissions,
#else
	filter_meta = filter_permissions,
	filter_foreign = filter_attributes
#endif
};

class CFilterCondition
{
public:
	bool set(t_filterType t, std::wstring const& v, int c, bool matchCase);

	std::wstring strValue;
	std::wstring lowerValue; // Name and path matches
	fz::datetime date; // If type is date
	int64_t value{}; // If type is size
	std::shared_ptr<std::wregex> pRegEx;

	t_filterType type{filter_name};
	int condition{};
};

class CFilter
{
public:
	enum t_matchType
	{
		all,
		any,
		none,
		not_all
	};

	bool empty() const { return filters.empty(); }

	explicit operator bool() const { return !filters.empty(); }

	std::vector<CFilterCondition> filters;

	std::wstring name;

	t_matchType matchType{ all };

	bool filterFiles{ true };
	bool filterDirs{ true };

	// Filenames on Windows ignore case
#ifdef __WXMSW__
	bool matchCase{};
#else
	bool matchCase{ true };
#endif

	bool HasConditionOfType(t_filterType type) const;
	bool IsLocalFilter() const;
};

class CFilterSet
{
public:
	std::wstring name;
	std::vector<bool> local;
	std::vector<bool> remote;
};

typedef std::pair<std::vector<CFilter>, std::vector<CFilter>> ActiveFilters;

namespace pugi { class xml_node; }
class CFilterManager
{
public:
	CFilterManager();
	virtual ~CFilterManager() = default;

	// Note: Under non-windows, attributes are permissions
	virtual bool FilenameFiltered(std::wstring const& name, std::wstring const& path, bool dir, int64_t size, bool local, int attributes, fz::datetime const& date) const;
	bool FilenameFiltered(std::vector<CFilter> const& filters, std::wstring const& name, std::wstring const& path, bool dir, int64_t size, int attributes, fz::datetime const& date) const;
	static bool FilenameFilteredByFilter(CFilter const& filter, std::wstring const& name, std::wstring const& path, bool dir, int64_t size, int attributes, fz::datetime const& date);
	static bool HasActiveFilters(bool ignore_disabled = false);

	bool HasSameLocalAndRemoteFilters() const;

	static void ToggleFilters();

	ActiveFilters GetActiveFilters();

	bool HasActiveLocalFilters() const;
	bool HasActiveRemoteFilters() const;

	static void Import(pugi::xml_node& element);
	static bool LoadFilter(pugi::xml_node& element, CFilter& filter);
	static void SaveFilter(pugi::xml_node& element, const CFilter& filter);

protected:
	static void LoadFilters();
	static void LoadFilters(pugi::xml_node& element);
	static void SaveFilters();

	static bool m_loaded;

	static std::vector<CFilter> m_globalFilters;

	static std::vector<CFilterSet> m_globalFilterSets;
	static unsigned int m_globalCurrentFilterSet;

	static bool m_filters_disabled;
};

class CMainFrame;
class CFilterDialog final : public wxDialogEx, public CFilterManager
{
public:
	CFilterDialog();

	bool Create(CMainFrame* parent);

protected:
	void DisplayFilters();

	void SetCtrlState();

	DECLARE_EVENT_TABLE()
	void OnOkOrApply(wxCommandEvent& event);
	void OnCancel(wxCommandEvent& event);
	void OnEdit(wxCommandEvent& event);
	void OnFilterSelect(wxCommandEvent& event);
	void OnMouseEvent(wxMouseEvent& event);
	void OnKeyEvent(wxKeyEvent& event);
	void OnSaveAs(wxCommandEvent& event);
	void OnRename(wxCommandEvent& event);
	void OnDeleteSet(wxCommandEvent& event);
	void OnSetSelect(wxCommandEvent& event);

	void OnChangeAll(wxCommandEvent& event);

	bool m_shiftClick{};

	CMainFrame* m_pMainFrame{};

	std::vector<CFilter> m_filters;
	std::vector<CFilterSet> m_filterSets;
	unsigned int m_currentFilterSet;
};

#endif
