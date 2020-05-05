#ifndef FILEZILLA_INTERFACE_SEARCH_HEADER
#define FILEZILLA_INTERFACE_SEARCH_HEADER

#include "filter_conditions_dialog.h"
#include "local_recursive_operation.h"
#include "listingcomparison.h"
#include "state.h"
#include <set>

class CWindowStateManager;
class CSearchDialogFileList;
class CQueueView;
class CFilelistStatusBar;
class CSearchDialog final : protected CFilterConditionsDialog, public CStateEventHandler
{
	friend class CSearchDialogFileList;
public:
	enum class search_mode
	{
		local,
		remote,
		comparison
	};

	CSearchDialog(wxWindow* parent, CState& state, CQueueView* pQueue);
	virtual ~CSearchDialog();

	bool Load();
	void Run();

	bool IsIdle();

protected:
	void ProcessDirectoryListing(std::shared_ptr<CDirectoryListing> const& listing);
	void ProcessDirectoryListing(CLocalRecursiveOperation::listing const& listing);

	void SetCtrlState();

	void SaveConditions();
	void LoadConditions();

	wxWindow* m_parent{};
	CSearchDialogFileList *m_results{};
	CSearchDialogFileList *m_remoteResults{};
	CQueueView* m_pQueue{};
	wxSize m_otherSize{-1, -1};

	CFilelistStatusBar* m_remoteStatusBar{};

	virtual void OnStateChange(t_statechange_notifications notification, std::wstring const& data, const void* data2) override;

	CWindowStateManager* m_pWindowStateManager{};

	CFilter m_search_filter;

	search_mode mode_{};
	bool searching_{};

	CServerPath m_original_dir;

	bool searched_remote_{};

	void Stop();

	DECLARE_EVENT_TABLE()
	void OnSearch(wxCommandEvent& event);
	void OnContextMenu(wxContextMenuEvent& event);
	void OnDownload(wxCommandEvent&);
	void OnUpload(wxCommandEvent&);
	void OnEdit(wxCommandEvent&);
	void OnDeleteLocal(wxCommandEvent&);
	void OnDeleteRemote(wxCommandEvent&);
	void OnCharHook(wxKeyEvent& event);
	void OnChangeSearchMode(wxCommandEvent&);
	void OnGetUrl(wxCommandEvent& event);
	void OnOpen(wxCommandEvent& event);
	void OnChangeCompareOption(wxCommandEvent& event);

	std::set<CServerPath> m_visited;

	CLocalPath m_local_search_root;
	CServerPath m_remote_search_root;

	CComparisonManager* m_pComparisonManager{};
};

#endif
