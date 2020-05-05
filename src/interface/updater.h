#ifndef FILEZILLA_UPDATER_HEADER
#define FILEZILLA_UPDATER_HEADER

#if FZ_MANUALUPDATECHECK

#include "notification.h"

#include <wx/timer.h>

#include <libfilezilla/uri.hpp>

#include <functional>
#include <list>

struct build
{
	std::wstring url_;
	std::wstring version_;
	std::wstring hash_;
	int64_t size_{-1};
};

struct version_information
{
	bool empty() const {
		return available_.version_.empty() && !eol_;
	}

	void update_available();

	build stable_;
	build beta_;
	build nightly_;

	build available_;

	std::wstring changelog_;

	std::wstring resources_;

	bool eol_{};
};

enum class UpdaterState
{
	idle,
	failed,
	checking,
	newversion, // There is a new version available, user needs to manually download
	newversion_downloading, // There is a new version available, file is being downloaded
	newversion_ready, // There is a new version available, file has been downloaded
	newversion_stale, // Very old version of FileZilla. Either update checking has been disabled or is otherwise not working.
	eol // Too old of an operating system
};

class CUpdateHandler
{
public:
	virtual void UpdaterStateChanged(UpdaterState s, build const& v) = 0;
};

class CFileZillaEngineContext;
class CUpdater final : public wxEvtHandler, private EngineNotificationHandler
{
public:
	CUpdater(CUpdateHandler& parent, CFileZillaEngineContext& engine_context, std::function<void(CActiveNotification const&)> const& activityNotificationHandler);
	virtual ~CUpdater();

	// 2-Stage initialization
	void Init();

	void AddHandler(CUpdateHandler& handler);
	void RemoveHandler(CUpdateHandler& handler);

	UpdaterState GetState() const { return state_; }
	build AvailableBuild() const { return version_information_.available_; }
	std::wstring GetChangelog() const { return version_information_.changelog_; }
	std::wstring GetResources() const { return version_information_.resources_; }

	std::wstring DownloadedFile() const;

	int64_t BytesDownloaded() const; // Returns -1 on error

	std::wstring GetLog() const { return log_; }

	static CUpdater* GetInstance();

	bool UpdatableBuild() const;

	void RunIfNeeded();

	bool Busy() const;

protected:
	bool LongTimeSinceLastCheck() const;

	int Download(std::wstring const& url, std::wstring const& local_file = std::wstring());
	int Request(fz::uri const& uri);
	int ContinueDownload();

	void AutoRunIfNeeded();
	bool Run(bool manual);

	bool CreateConnectCommand(std::wstring const& url);
	bool CreateTransferCommand(std::wstring const& url, std::wstring const& local_file);

	fz::uri GetUrl();
	void ProcessNotification(std::unique_ptr<CNotification> && notification);
	void ProcessOperation(COperationNotification const& operation);
	void ProcessData(CDataNotification& dataNotification);
	void ParseData();
	UpdaterState ProcessFinishedDownload();
	UpdaterState ProcessFinishedData(bool can_download);

	bool VerifyChecksum(std::wstring const& file, int64_t size, std::wstring const& checksum);

	std::wstring GetTempFile() const;
	std::wstring GetFilename(std::wstring const& url) const;
	std::wstring GetLocalFile(build const& b, bool allow_existing);

	void SetState(UpdaterState s);

	virtual void OnEngineEvent(CFileZillaEngine* engine);
	void DoOnEngineEvent(CFileZillaEngine* engine);

	DECLARE_EVENT_TABLE()
	void OnTimer(wxTimerEvent& ev);

	UpdaterState state_;
	std::wstring local_file_;

	CFileZillaEngineContext& engine_context_;
	CFileZillaEngine* engine_{};

	bool m_use_internal_rootcert{};

	std::wstring raw_version_information_;

	version_information version_information_;

	std::list<CUpdateHandler*> handlers_;

	std::wstring log_;

	wxTimer update_timer_;

	std::deque<std::unique_ptr<CCommand>> pending_commands_;

	std::function<void(CActiveNotification const&)> activityNotificationHandler_;

	bool manual_{};
};

#endif //FZ_MANUALUPDATECHECK

#endif
