#ifndef FILEZILLA_INTERFACE_SPLITTER_HEADER
#define FILEZILLA_INTERFACE_SPLITTER_HEADER

class CSplitterWindowEx final : public wxSplitterWindow
{
public:
	CSplitterWindowEx();
	CSplitterWindowEx(wxWindow* parent, wxWindowID id, wxPoint const& point = wxDefaultPosition, wxSize const& size = wxDefaultSize, long style = wxSP_3D, wxString const& name = _T("splitterWindow"));

	bool Create(wxWindow* parent, wxWindowID id, wxPoint const& point = wxDefaultPosition, wxSize const& size = wxDefaultSize, long style = wxSP_3D, wxString const& name = _T("splitterWindow"));

	void SetSashGravity(double gravity);

	// If pane size goes below paneSize_soft, make sure both panes are equally large
	void SetMinimumPaneSize(int paneSize, int paneSize_soft = -1);

	int GetSashPosition() const;
	void SetSashPosition(int sash_position);
	void SetRelativeSashPosition(double relative_sash_position);
	double GetRelativeSashPosition() const { return m_relative_sash_position; }

	void Initialize(wxWindow *window);

	bool SplitHorizontally(wxWindow* window1, wxWindow* window2, int sashPosition = 0);
	bool SplitVertically(wxWindow* window1, wxWindow* window2, int sashPosition = 0);

	bool Unsplit(wxWindow* toRemove = NULL);

protected:
	void PrepareSplit(wxWindow* window1, wxWindow* window2, int & sashPosition, bool horizontal);

	virtual int OnSashPositionChanging(int newSashPosition);

	int CalcSoftLimit(int newSashPosition);

	DECLARE_EVENT_TABLE()
	void OnSize(wxSizeEvent& event);

	double m_relative_sash_position{0.5};

	int m_soft_min_pane_size{-1};

	int m_lastSashPosition{-1};
};

#endif
