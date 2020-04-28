#include <filezilla.h>
#include "conditionaldialog.h"
#include "dialogex.h"
#include "edithandler.h"
#include "filezillaapp.h"
#include "file_utils.h"
#include "Options.h"
#include "queue.h"
#include "window_state_manager.h"
#include "xrc_helper.h"

#include <libfilezilla/file.hpp>
#include <libfilezilla/local_filesys.hpp>

class CChangedFileDialog : public wxDialogEx
{
	DECLARE_EVENT_TABLE()
	void OnYes(wxCommandEvent& event);
	void OnNo(wxCommandEvent& event);
};

BEGIN_EVENT_TABLE(CChangedFileDialog, wxDialogEx)
EVT_BUTTON(wxID_YES, CChangedFileDialog::OnYes)
EVT_BUTTON(wxID_NO, CChangedFileDialog::OnNo)
END_EVENT_TABLE()

void CChangedFileDialog::OnYes(wxCommandEvent&)
{
	EndDialog(wxID_YES);
}

void CChangedFileDialog::OnNo(wxCommandEvent&)
{
	EndDialog(wxID_NO);
}

//-------------

DECLARE_EVENT_TYPE(fzEDIT_CHANGEDFILE, -1)
DEFINE_EVENT_TYPE(fzEDIT_CHANGEDFILE)

BEGIN_EVENT_TABLE(CEditHandler, wxEvtHandler)
EVT_TIMER(wxID_ANY, CEditHandler::OnTimerEvent)
EVT_COMMAND(wxID_ANY, fzEDIT_CHANGEDFILE, CEditHandler::OnChangedFileEvent)
END_EVENT_TABLE()

CEditHandler* CEditHandler::m_pEditHandler = 0;

CEditHandler::CEditHandler()
{
	m_pQueue = 0;

	m_timer.SetOwner(this);
	m_busyTimer.SetOwner(this);

#ifdef __WXMSW__
	m_lockfile_handle = INVALID_HANDLE_VALUE;
#else
	m_lockfile_descriptor = -1;
#endif
}

CEditHandler* CEditHandler::Create()
{
	if (!m_pEditHandler) {
		m_pEditHandler = new CEditHandler();
	}

	return m_pEditHandler;
}

CEditHandler* CEditHandler::Get()
{
	return m_pEditHandler;
}

void CEditHandler::RemoveTemporaryFiles(wxString const& temp)
{
	wxDir dir(temp);
	if (!dir.IsOpened()) {
		return;
	}

	wxString file;
	if (!dir.GetFirst(&file, _T("fz3temp-*"), wxDIR_DIRS)) {
		return;
	}

	wxChar const& sep = wxFileName::GetPathSeparator();
	do {
		if (!m_localDir.empty() && temp + file + sep == m_localDir) {
			// Don't delete own working directory
			continue;
		}

		RemoveTemporaryFilesInSpecificDir((temp + file + sep).ToStdWstring());
	} while (dir.GetNext(&file));
}

void CEditHandler::RemoveTemporaryFilesInSpecificDir(std::wstring const& temp)
{
	std::wstring const lockfile = temp + L"fz3temp-lockfile";
	if (wxFileName::FileExists(lockfile)) {
#ifndef __WXMSW__
		int fd = open(fz::to_string(lockfile).c_str(), O_RDWR | O_CLOEXEC, 0);
		if (fd >= 0) {
			// Try to lock 1 byte region in the lockfile. m_type specifies the byte to lock.
			struct flock f = {};
			f.l_type = F_WRLCK;
			f.l_whence = SEEK_SET;
			f.l_start = 0;
			f.l_len = 1;
			f.l_pid = getpid();
			if (fcntl(fd, F_SETLK, &f)) {
				// In use by other process
				close(fd);
				return;
			}
			close(fd);
		}
#endif
		fz::remove_file(fz::to_native(lockfile));

		if (wxFileName::FileExists(lockfile)) {
			return;
		}
	}

	wxLogNull log;

	{
		wxString file;
		wxDir dir(temp);
		bool res;
		for ((res = dir.GetFirst(&file, _T(""), wxDIR_FILES)); res; res = dir.GetNext(&file)) {
			wxRemoveFile(temp + file);
		}
	}

	wxRmdir(temp);

}

std::wstring CEditHandler::GetLocalDirectory()
{
	if (!m_localDir.empty()) {
		return m_localDir;
	}

	wxFileName tmpdir(wxFileName::GetTempDir(), _T(""));
	// Need to call GetLongPath on MSW, GetTempDir can return short path
	// which will cause problems when calculating maximum allowed file
	// length
	wxString dir = tmpdir.GetLongPath();
	if (dir.empty() || !wxFileName::DirExists(dir)) {
		return std::wstring();
	}

	if (dir.Last() != wxFileName::GetPathSeparator()) {
		dir += wxFileName::GetPathSeparator();
	}

	// On POSIX, the permissions of the created directory (700) ensure
	// that this is a safe operation.
	// On Windows, the user's profile directory and associated temp dir
	// already has the correct permissions which get inherited.
	int i = 1;
	do {
		wxString newDir = dir + wxString::Format(_T("fz3temp-%d"), ++i);
		if (wxFileName::FileExists(newDir) || wxFileName::DirExists(newDir)) {
			continue;
		}

		if (!wxMkdir(newDir, 0700)) {
			return std::wstring();
		}

		m_localDir = (newDir + wxFileName::GetPathSeparator()).ToStdWstring();
		break;
	} while (true);

	// Defer deleting stale directories until after having created our own
	// working directory.
	// This avoids some strange errors where freshly deleted directories
	// cannot be instantly recreated.
	RemoveTemporaryFiles(dir);

#ifdef __WXMSW__
	m_lockfile_handle = ::CreateFile((m_localDir + L"fz3temp-lockfile").c_str(), GENERIC_WRITE, 0, 0, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY, 0);
	if (m_lockfile_handle == INVALID_HANDLE_VALUE) {
		wxRmdir(m_localDir);
		m_localDir.clear();
	}
#else
	auto file = fz::to_native(m_localDir) + "fz3temp-lockfile";
	m_lockfile_descriptor = open(file.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
	if (m_lockfile_descriptor >= 0) {
		// Lock 1 byte region in the lockfile.
		struct flock f = {};
		f.l_type = F_WRLCK;
		f.l_whence = SEEK_SET;
		f.l_start = 0;
		f.l_len = 1;
		f.l_pid = getpid();
		fcntl(m_lockfile_descriptor, F_SETLKW, &f);
	}
#endif

	return m_localDir;
}

void CEditHandler::Release()
{
	if (m_timer.IsRunning()) {
		m_timer.Stop();
	}
	if (m_busyTimer.IsRunning()) {
		m_busyTimer.Stop();
	}

	if (!m_localDir.empty()) {
#ifdef __WXMSW__
		if (m_lockfile_handle != INVALID_HANDLE_VALUE) {
			CloseHandle(m_lockfile_handle);
		}
		wxRemoveFile(m_localDir + _T("fz3temp-lockfile"));
#else
		wxRemoveFile(m_localDir + _T("fz3temp-lockfile"));
		if (m_lockfile_descriptor >= 0) {
			close(m_lockfile_descriptor);
		}
#endif

		wxLogNull log;
		wxRemoveFile(m_localDir + _T("empty_file_yq744zm"));
		RemoveAll(true);
		RemoveTemporaryFilesInSpecificDir(m_localDir);
	}

	m_pEditHandler = 0;
	delete this;
}

CEditHandler::fileState CEditHandler::GetFileState(wxString const& fileName) const
{
	std::list<t_fileData>::const_iterator iter = GetFile(fileName);
	if (iter == m_fileDataList[local].end()) {
		return unknown;
	}

	return iter->state;
}

CEditHandler::fileState CEditHandler::GetFileState(wxString const& fileName, CServerPath const& remotePath, Site const& site) const
{
	std::list<t_fileData>::const_iterator iter = GetFile(fileName, remotePath, site);
	if (iter == m_fileDataList[remote].end()) {
		return unknown;
	}

	return iter->state;
}

int CEditHandler::GetFileCount(CEditHandler::fileType type, CEditHandler::fileState state, Site const& site) const
{
	int count = 0;
	if (state == unknown) {
		wxASSERT(!site);
		if (type != remote) {
			count += m_fileDataList[local].size();
		}
		if (type != local) {
			count += m_fileDataList[remote].size();
		}
	}
	else {
		auto f = [state, &site](decltype(m_fileDataList[0]) & items) {
			int cnt = 0;
			for (auto const& data : items) {
				if (data.state != state) {
					continue;
				}

				if (!site || data.site == site) {
					++cnt;
				}
			}
			return cnt;
		};
		if (type != remote) {
			count += f(m_fileDataList[local]);
		}
		if (type != local) {
			count += f(m_fileDataList[remote]);
		}
	}

	return count;
}

bool CEditHandler::AddFile(CEditHandler::fileType type, std::wstring& fileName, CServerPath const& remotePath, Site const& site)
{
	wxASSERT(type != none);

	fileState state;
	if (type == local) {
		state = GetFileState(fileName);
	}
	else {
		state = GetFileState(fileName, remotePath, site);
	}
	if (state != unknown) {
		wxFAIL_MSG(_T("File state not unknown"));
		return false;
	}

	t_fileData data;
	if (type == remote) {
		data.state = download;
		data.name = fileName;
		data.file = GetTemporaryFile(fileName);
		fileName = data.file;
	}
	else {
		data.file = fileName;
		data.name = wxFileName(fileName).GetFullName().ToStdWstring();
		data.state = edit;
	}
	data.remotePath = remotePath;
	data.site = site;

	if (type == local && !COptions::Get()->GetOptionVal(OPTION_EDIT_TRACK_LOCAL)) {
		return StartEditing(local, data);
	}

	if (type == remote || StartEditing(type, data)) {
		m_fileDataList[type].push_back(data);
	}

	return true;
}

bool CEditHandler::Remove(const wxString& fileName)
{
	std::list<t_fileData>::iterator iter = GetFile(fileName);
	if (iter == m_fileDataList[local].end()) {
		return true;
	}

	wxASSERT(iter->state != upload && iter->state != upload_and_remove);
	if (iter->state == upload || iter->state == upload_and_remove) {
		return false;
	}

	m_fileDataList[local].erase(iter);

	return true;
}

bool CEditHandler::Remove(wxString const& fileName, CServerPath const& remotePath, Site const& site)
{
	std::list<t_fileData>::iterator iter = GetFile(fileName, remotePath, site);
	if (iter == m_fileDataList[remote].end()) {
		return true;
	}

	wxASSERT(iter->state != download && iter->state != upload && iter->state != upload_and_remove);
	if (iter->state == download || iter->state == upload || iter->state == upload_and_remove) {
		return false;
	}

	if (wxFileName::FileExists(iter->file)) {
		if (!wxRemoveFile(iter->file)) {
			iter->state = removing;
			return false;
		}
	}

	m_fileDataList[remote].erase(iter);

	return true;
}

bool CEditHandler::RemoveAll(bool force)
{
	std::list<t_fileData> keep;

	for (std::list<t_fileData>::iterator iter = m_fileDataList[remote].begin(); iter != m_fileDataList[remote].end(); ++iter) {
		if (!force && (iter->state == download || iter->state == upload || iter->state == upload_and_remove)) {
			keep.push_back(*iter);
			continue;
		}

		if (wxFileName::FileExists(iter->file)) {
			if (!wxRemoveFile(iter->file)) {
				iter->state = removing;
				keep.push_back(*iter);
				continue;
			}
		}
	}
	m_fileDataList[remote].swap(keep);
	keep.clear();

	for (auto iter = m_fileDataList[local].begin(); iter != m_fileDataList[local].end(); ++iter) {
		if (force) {
			continue;
		}

		if (iter->state == upload || iter->state == upload_and_remove) {
			keep.push_back(*iter);
			continue;
		}
	}
	m_fileDataList[local].swap(keep);

	return m_fileDataList[local].empty() && m_fileDataList[remote].empty();
}

bool CEditHandler::RemoveAll(fileState state, Site const& site)
{
	// Others not implemented
	wxASSERT(state == upload_and_remove_failed);
	if (state != upload_and_remove_failed) {
		return false;
	}

	std::list<t_fileData> keep;

	for (auto iter = m_fileDataList[remote].begin(); iter != m_fileDataList[remote].end(); ++iter) {
		if (iter->state != state) {
			keep.push_back(*iter);
			continue;
		}

		if (site && iter->site != site) {
			keep.push_back(*iter);
			continue;
		}

		if (wxFileName::FileExists(iter->file)) {
			if (!wxRemoveFile(iter->file)) {
				iter->state = removing;
				keep.push_back(*iter);
				continue;
			}
		}
	}
	m_fileDataList[remote].swap(keep);

	return true;
}

std::list<CEditHandler::t_fileData>::iterator CEditHandler::GetFile(const wxString& fileName)
{
	std::list<t_fileData>::iterator iter;
	for (iter = m_fileDataList[local].begin(); iter != m_fileDataList[local].end(); ++iter) {
		if (iter->file == fileName) {
			break;
		}
	}

	return iter;
}

std::list<CEditHandler::t_fileData>::const_iterator CEditHandler::GetFile(const wxString& fileName) const
{
	std::list<t_fileData>::const_iterator iter;
	for (iter = m_fileDataList[local].begin(); iter != m_fileDataList[local].end(); ++iter) {
		if (iter->file == fileName) {
			break;
		}
	}

	return iter;
}

std::list<CEditHandler::t_fileData>::iterator CEditHandler::GetFile(wxString const& fileName, CServerPath const& remotePath, Site const& site)
{
	std::list<t_fileData>::iterator iter;
	for (iter = m_fileDataList[remote].begin(); iter != m_fileDataList[remote].end(); ++iter) {
		if (iter->name != fileName) {
			continue;
		}

		if (iter->site != site) {
			continue;
		}

		if (iter->remotePath != remotePath) {
			continue;
		}

		return iter;
	}

	return iter;
}

std::list<CEditHandler::t_fileData>::const_iterator CEditHandler::GetFile(wxString const& fileName, CServerPath const& remotePath, Site const& site) const
{
	std::list<t_fileData>::const_iterator iter;
	for (iter = m_fileDataList[remote].begin(); iter != m_fileDataList[remote].end(); ++iter) {
		if (iter->name != fileName) {
			continue;
		}

		if (iter->site != site) {
			continue;
		}

		if (iter->remotePath != remotePath) {
			continue;
		}

		return iter;
	}

	return iter;
}

void CEditHandler::FinishTransfer(bool, wxString const& fileName)
{
	auto iter = GetFile(fileName);
	if (iter == m_fileDataList[local].end()) {
		return;
	}

	wxASSERT(iter->state == upload || iter->state == upload_and_remove);

	switch (iter->state)
	{
	case upload_and_remove:
		m_fileDataList[local].erase(iter);
		break;
	case upload:
		if (wxFileName::FileExists(fileName)) {
			iter->state = edit;
		}
		else {
			m_fileDataList[local].erase(iter);
		}
		break;
	default:
		return;
	}

	SetTimerState();
}

void CEditHandler::FinishTransfer(bool successful, wxString const& fileName, CServerPath const& remotePath, Site const& site)
{
	auto iter = GetFile(fileName, remotePath, site);
	if (iter == m_fileDataList[remote].end()) {
		return;
	}

	wxASSERT(iter->state == download || iter->state == upload || iter->state == upload_and_remove);

	switch (iter->state)
	{
	case upload_and_remove:
		if (successful) {
			if (wxFileName::FileExists(iter->file) && !wxRemoveFile(iter->file)) {
				iter->state = removing;
			}
			else {
				m_fileDataList[remote].erase(iter);
			}
		}
		else {
			if (!wxFileName::FileExists(iter->file)) {
				m_fileDataList[remote].erase(iter);
			}
			else {
				iter->state = upload_and_remove_failed;
			}
		}
		break;
	case upload:
		if (wxFileName::FileExists(iter->file)) {
			iter->state = edit;
		}
		else {
			m_fileDataList[remote].erase(iter);
		}
		break;
	case download:
		if (wxFileName::FileExists(iter->file)) {
			iter->state = edit;
			if (StartEditing(remote, *iter)) {
				break;
			}
		}
		if (wxFileName::FileExists(iter->file) && !wxRemoveFile(iter->file)) {
			iter->state = removing;
		}
		else {
			m_fileDataList[remote].erase(iter);
		}
		break;
	default:
		return;
	}

	SetTimerState();
}

bool CEditHandler::StartEditing(wxString const& file)
{
	auto iter = GetFile(file);
	if (iter == m_fileDataList[local].end()) {
		return false;
	}

	return StartEditing(local, *iter);
}

bool CEditHandler::StartEditing(wxString const& file, CServerPath const& remotePath, Site const& site)
{
	auto iter = GetFile(file, remotePath, site);
	if (iter == m_fileDataList[remote].end()) {
		return false;
	}

	return StartEditing(remote, *iter);
}

bool CEditHandler::StartEditing(CEditHandler::fileType type, t_fileData& data)
{
	wxASSERT(type != none);
	wxASSERT(data.state == edit);

	bool is_link;
	if (fz::local_filesys::get_file_info(fz::to_native(data.file), is_link, 0, &data.modificationTime, 0) != fz::local_filesys::file) {
		return false;
	}

	bool program_exists = false;
	wxString cmd = GetOpenCommand(data.file, program_exists);
	if (cmd.empty() || !program_exists) {
		return false;
	}

	if (!wxExecute(cmd)) {
		return false;
	}

	return true;
}

void CEditHandler::CheckForModifications(bool emitEvent)
{
	static bool insideCheckForModifications = false;
	if (insideCheckForModifications)
		return;

	if (emitEvent) {
		QueueEvent(new wxCommandEvent(fzEDIT_CHANGEDFILE));
		return;
	}

	insideCheckForModifications = true;

	for (int i = 0; i < 2; ++i) {
checkmodifications_loopbegin:
		for (auto iter = m_fileDataList[i].begin(); iter != m_fileDataList[i].end(); ++iter) {
			if (iter->state != edit) {
				continue;
			}

			fz::datetime mtime;
			bool is_link;
			if (fz::local_filesys::get_file_info(fz::to_native(iter->file), is_link, 0, &mtime, 0) != fz::local_filesys::file) {
				m_fileDataList[i].erase(iter);

				// Evil goto. Imo the next C++ standard needs a comefrom keyword.
				goto checkmodifications_loopbegin;
			}

			if (mtime.empty()) {
				continue;
			}

			if (!iter->modificationTime.empty() && !iter->modificationTime.compare(mtime)) {
				continue;
			}

			// File has changed, ask user what to do

			m_busyTimer.Stop();
			if (!wxDialogEx::CanShowPopupDialog()) {
				m_busyTimer.Start(1000, true);
				insideCheckForModifications = false;
				return;
			}
			wxTopLevelWindow* pTopWindow = (wxTopLevelWindow*)wxTheApp->GetTopWindow();
			if (pTopWindow && pTopWindow->IsIconized()) {
				pTopWindow->RequestUserAttention(wxUSER_ATTENTION_INFO);
				insideCheckForModifications = false;
				return;
			}

			bool remove;
			int res = DisplayChangeNotification(CEditHandler::fileType(i), iter, remove);
			if (res == -1) {
				continue;
			}

			if (res == wxID_YES) {
				UploadFile(CEditHandler::fileType(i), iter, remove);
				goto checkmodifications_loopbegin;
			}
			else if (remove) {
				if (i == static_cast<int>(remote)) {
					if (fz::local_filesys::get_file_info(fz::to_native(iter->file), is_link, 0, &mtime, 0) != fz::local_filesys::file || wxRemoveFile(iter->file)) {
						m_fileDataList[i].erase(iter);
						goto checkmodifications_loopbegin;
					}
					iter->state = removing;
				}
				else {
					m_fileDataList[i].erase(iter);
					goto checkmodifications_loopbegin;
				}
			}
			else if (fz::local_filesys::get_file_info(fz::to_native(iter->file), is_link, 0, &mtime, 0) != fz::local_filesys::file) {
				m_fileDataList[i].erase(iter);
				goto checkmodifications_loopbegin;
			}
			else {
				iter->modificationTime = mtime;
			}
		}
	}

	SetTimerState();

	insideCheckForModifications = false;
}

int CEditHandler::DisplayChangeNotification(CEditHandler::fileType type, std::list<CEditHandler::t_fileData>::const_iterator iter, bool& remove)
{
	CChangedFileDialog dlg;
	if (!dlg.Load(wxTheApp->GetTopWindow(), _T("ID_CHANGEDFILE"))) {
		return -1;
	}
	if (type == remote) {
		XRCCTRL(dlg, "ID_DESC_UPLOAD_LOCAL", wxStaticText)->Hide();
	}
	else {
		XRCCTRL(dlg, "ID_DESC_UPLOAD_REMOTE", wxStaticText)->Hide();
	}

	dlg.SetChildLabel(XRCID("ID_FILENAME"), iter->name);

	if (type == local) {
		XRCCTRL(dlg, "ID_DESC_OPENEDAS", wxStaticText)->Hide();
		XRCCTRL(dlg, "ID_OPENEDAS", wxStaticText)->Hide();

		dlg.SetChildLabel("ID_DELETE", _T("&Finish editing"));
	}
	else {
		wxString file = iter->file;
		int pos = file.Find(wxFileName::GetPathSeparator(), true);
		wxASSERT(pos != -1);
		file = file.Mid(pos + 1);

		if (file == iter->name) {
			XRCCTRL(dlg, "ID_DESC_OPENEDAS", wxStaticText)->Hide();
			XRCCTRL(dlg, "ID_OPENEDAS", wxStaticText)->Hide();
		}
		else {
			dlg.SetChildLabel(XRCID("ID_OPENEDAS"), file);
		}
	}

	dlg.SetChildLabel(XRCID("ID_SERVER"), iter->site.Format(ServerFormat::with_user_and_optional_port));
	dlg.SetChildLabel(XRCID("ID_REMOTEPATH"), iter->remotePath.GetPath());

	dlg.GetSizer()->Fit(&dlg);

	int res = dlg.ShowModal();

	remove = XRCCTRL(dlg, "ID_DELETE", wxCheckBox)->IsChecked();

	return res;
}

bool CEditHandler::UploadFile(wxString const& file, CServerPath const& remotePath, Site const& site, bool unedit)
{
	std::list<t_fileData>::iterator iter = GetFile(file, remotePath, site);
	return UploadFile(remote, iter, unedit);
}

bool CEditHandler::UploadFile(const wxString& file, bool unedit)
{
	std::list<t_fileData>::iterator iter = GetFile(file);
	return UploadFile(local, iter, unedit);
}

bool CEditHandler::UploadFile(fileType type, std::list<t_fileData>::iterator iter, bool unedit)
{
	wxCHECK(type != none, false);

	if (iter == m_fileDataList[type].end()) {
		return false;
	}

	wxASSERT(iter->state == edit || iter->state == upload_and_remove_failed);
	if (iter->state != edit && iter->state != upload_and_remove_failed) {
		return false;
	}

	iter->state = unedit ? upload_and_remove : upload;

	int64_t size;
	fz::datetime mtime;

	bool is_link;
	if (fz::local_filesys::get_file_info(fz::to_native(iter->file), is_link, &size, &mtime, 0) != fz::local_filesys::file) {
		m_fileDataList[type].erase(iter);
		return false;
	}

	if (mtime.empty()) {
		mtime = fz::datetime::now();
	}

	iter->modificationTime = mtime;

	wxASSERT(m_pQueue);

	std::wstring file;
	CLocalPath localPath(iter->file, &file);
	if (file.empty()) {
		m_fileDataList[type].erase(iter);
		return false;
	}

	m_pQueue->QueueFile(false, false, file, iter->name, localPath, iter->remotePath, iter->site, size, type, QueuePriority::high);
	m_pQueue->QueueFile_Finish(true);

	return true;
}

void CEditHandler::OnTimerEvent(wxTimerEvent&)
{
#ifdef __WXMSW__
	// Don't check for changes if mouse is captured,
	// e.g. if user is dragging a file
	if (GetCapture()) {
		return;
	}
#endif

	CheckForModifications();
}

void CEditHandler::SetTimerState()
{
	bool editing = GetFileCount(none, edit) != 0;

	if (m_timer.IsRunning()) {
		if (!editing) {
			m_timer.Stop();
		}
	}
	else if (editing) {
		m_timer.Start(15000);
	}
}

wxString CEditHandler::CanOpen(CEditHandler::fileType type, const wxString& fileName, bool &dangerous, bool &program_exists)
{
	wxASSERT(type != none);

	wxString command = GetOpenCommand(fileName, program_exists);
	if (command.empty() || !program_exists) {
		return command;
	}

	wxFileName fn;
	if (type == remote) {
		fn = wxFileName(m_localDir, fileName);
	}
	else {
		fn = wxFileName(fileName);
	}

	wxString name = fn.GetFullPath();
	wxString tmp = command;
	wxString args;
	if (UnquoteCommand(tmp, args) && tmp == name) {
		dangerous = true;
	}
	else {
		dangerous = false;
	}

	return command;
}

wxString CEditHandler::GetOpenCommand(const wxString& file, bool& program_exists)
{
#ifdef __WXMSW__
	if (file.find('"') != std::wstring::npos) {
		// Windows doesn't allow double-quotes in filenames.
		// On top if it, wxExecute does not properly deal with quotes preceeded by backslashes.
		// This is the safe choice.
		return wxString();
	}
#endif

	if (!COptions::Get()->GetOptionVal(OPTION_EDIT_ALWAYSDEFAULT)) {
		const wxString command = GetCustomOpenCommand(file, program_exists);
		if (!command.empty()) {
			return command;
		}

		if (COptions::Get()->GetOptionVal(OPTION_EDIT_INHERITASSOCIATIONS)) {
			const wxString sysCommand = GetSystemOpenCommand(file, program_exists);
			if (!sysCommand.empty()) {
				return sysCommand;
			}
		}
	}

	wxString command = COptions::Get()->GetOption(OPTION_EDIT_DEFAULTEDITOR);
	if (command.empty() || command[0] == '0') {
		return wxString(); // None set
	}
	else if (command[0] == '1') {
		// Text editor
		const wxString random = _T("5AC2EE515D18406 space aB77C2C60F1F88952.txt"); // Chosen by fair dice roll. Guaranteed to be random.
		wxString sysCommand = GetSystemOpenCommand(random, program_exists);
		if (sysCommand.empty() || !program_exists) {
			return sysCommand;
		}

		sysCommand.Replace(random, file);
		return sysCommand;
	}
	else if (command[0] == '2') {
		command = command.Mid(1);
	}

	if (command.empty()) {
		return wxString();
	}

	wxString args;
	wxString editor = command;
	if (!UnquoteCommand(editor, args)) {
		return wxString();
	}

	if (!ProgramExists(editor)) {
		program_exists = false;
		return editor;
	}

	program_exists = true;
#ifndef __WXMSW__
	wxString escaped = file;
	escaped.Replace(L"\\", L"\\\\");
	escaped.Replace(L"\"", L"\\\"");
	return command + _T(" \"") + escaped + _T("\"");
#else
	return command + _T(" \"") + file + _T("\"");
#endif
}

wxString CEditHandler::GetCustomOpenCommand(const wxString& file, bool& program_exists)
{
#ifdef __WXMSW__
	if (file.find('"') != std::wstring::npos) {
		// Windows doesn't allow double-quotes in filenames.
		// On top if it, wxExecute does not properly deal with quotes preceeded by backslashes.
		// This is the safe choice.
		return wxString();
	}
#endif

	wxFileName fn(file);

	wxString ext = fn.GetExt();
	if (ext.empty()) {
		if (fn.GetFullName()[0] == '.') {
			ext = _T(".");
		}
		else {
			ext = _T("/");
		}
	}

	wxString associations = COptions::Get()->GetOption(OPTION_EDIT_CUSTOMASSOCIATIONS) + _T("\n");
	associations.Replace(_T("\r"), _T(""));
	int pos;
	while ((pos = associations.Find('\n')) != -1) {
		wxString assoc = associations.Left(pos);
		associations = associations.Mid(pos + 1);

		if (assoc.empty()) {
			continue;
		}

		wxString command;
		if (!UnquoteCommand(assoc, command)) {
			continue;
		}

		if (assoc != ext) {
			continue;
		}

		wxString prog = command;

		wxString args;
		if (!UnquoteCommand(prog, args)) {
			return wxString();
		}

		if (prog.empty()) {
			return wxString();
		}

		if (!ProgramExists(prog)) {
			program_exists = false;
			return prog;
		}

		program_exists = true;

		wxString arg = fn.GetFullPath();
#ifndef __WXMSW__
		arg.Replace(L"\\", L"\\\\");
		arg.Replace(L"\"", L"\\\"");
#endif
		return command + L" \"" + arg + L"\"";
	}

	return wxString();
}

void CEditHandler::OnChangedFileEvent(wxCommandEvent&)
{
	CheckForModifications();
}

std::wstring CEditHandler::GetTemporaryFile(std::wstring name)
{
	name = CQueueView::ReplaceInvalidCharacters(name);
#ifdef __WXMSW__
	// MAX_PATH - 1 is theoretical limit, we subtract another 4 to allow
	// editors which create temporary files
	size_t max = MAX_PATH - 5;
#else
	size_t max = std::wstring::npos;
#endif
	if (max != std::wstring::npos) {
		name = TruncateFilename(m_localDir, name, max);
		if (name.empty()) {
			return std::wstring();
		}
	}

	std::wstring file = m_localDir + name;
	if (!FilenameExists(file)) {
		return file;
	}

	if (max != std::wstring::npos) {
		--max;
	}
	int cutoff = 1;
	int n = 1;
	while (++n < 10000) { // Just to give up eventually
		// Further reduce length if needed
		if (max != std::wstring::npos && n >= cutoff) {
			cutoff *= 10;
			--max;
			name = TruncateFilename(m_localDir, name, max);
			if (name.empty()) {
				return std::wstring();
			}
		}

		size_t pos = name.rfind('.');
		if (pos == std::wstring::npos || !pos) {
			file = m_localDir + name + fz::sprintf(L" %d", n);
		}
		else {
			file = m_localDir + name.substr(0, pos) + fz::sprintf(L" %d", n) + name.substr(pos);
		}

		if (!FilenameExists(file)) {
			return file;
		}
	}

	return std::wstring();
}

std::wstring CEditHandler::TruncateFilename(std::wstring const& path, std::wstring const& name, size_t max)
{
	size_t const pathlen = path.size();
	size_t const namelen = name.size();

	if (namelen + pathlen > max) {
		size_t pos = name.rfind('.');
		if (pos != std::wstring::npos) {
			size_t extlen = namelen - pos;
			if (pathlen + extlen >= max)
			{
				// Cannot truncate extension
				return std::wstring();
			}

			return name.substr(0, max - pathlen - extlen) + name.substr(pos);
		}
	}

	return name;
}

bool CEditHandler::FilenameExists(const wxString& file)
{
	for (auto const& fileData : m_fileDataList[remote]) {
		// Always ignore case, we don't know which type of filesystem the user profile
		// is installed upon.
		if (!wxString(fileData.file).CmpNoCase(file)) {
			return true;
		}
	}

	if (wxFileName::FileExists(file)) {
		// Save to remove, it's not marked as edited anymore.
		{
			wxLogNull log;
			wxRemoveFile(file);
		}

		if (wxFileName::FileExists(file)) {
			return true;
		}
	}

	return false;
}

BEGIN_EVENT_TABLE(CEditHandlerStatusDialog, wxDialogEx)
EVT_LIST_ITEM_SELECTED(wxID_ANY, CEditHandlerStatusDialog::OnSelectionChanged)
EVT_BUTTON(XRCID("ID_UNEDIT"), CEditHandlerStatusDialog::OnUnedit)
EVT_BUTTON(XRCID("ID_UPLOAD"), CEditHandlerStatusDialog::OnUpload)
EVT_BUTTON(XRCID("ID_UPLOADANDUNEDIT"), CEditHandlerStatusDialog::OnUpload)
EVT_BUTTON(XRCID("ID_EDIT"), CEditHandlerStatusDialog::OnEdit)
END_EVENT_TABLE()

#define COLUMN_NAME 0
#define COLUMN_TYPE 1
#define COLUMN_REMOTEPATH 2
#define COLUMN_STATUS 3

CEditHandlerStatusDialog::CEditHandlerStatusDialog(wxWindow* parent)
	: m_pParent(parent)
{
	m_pWindowStateManager = 0;
}

CEditHandlerStatusDialog::~CEditHandlerStatusDialog()
{
	if (m_pWindowStateManager) {
		m_pWindowStateManager->Remember(OPTION_EDITSTATUSDIALOG_SIZE);
		delete m_pWindowStateManager;
	}
}

int CEditHandlerStatusDialog::ShowModal()
{
	const CEditHandler* const pEditHandler = CEditHandler::Get();
	if (!pEditHandler) {
		return wxID_CANCEL;
	}

	if (!pEditHandler->GetFileCount(CEditHandler::none, CEditHandler::unknown)) {
		wxMessageBoxEx(_("No files are currently being edited."), _("Cannot show dialog"), wxICON_INFORMATION, m_pParent);
		return wxID_CANCEL;
	}

	if (!Load(m_pParent, _T("ID_EDITING"))) {
		return wxID_CANCEL;
	}

	wxListCtrl* pListCtrl = XRCCTRL(*this, "ID_FILES", wxListCtrl);
	if (!pListCtrl) {
		return wxID_CANCEL;
	}

	pListCtrl->InsertColumn(0, _("Filename"));
	pListCtrl->InsertColumn(1, _("Type"));
	pListCtrl->InsertColumn(2, _("Remote path"));
	pListCtrl->InsertColumn(3, _("Status"));

	{
		const std::list<CEditHandler::t_fileData>& files = pEditHandler->GetFiles(CEditHandler::remote);
		unsigned int i = 0;
		for (std::list<CEditHandler::t_fileData>::const_iterator iter = files.begin(); iter != files.end(); ++iter, ++i) {
			pListCtrl->InsertItem(i, iter->name);
			pListCtrl->SetItem(i, COLUMN_TYPE, _("Remote"));
			switch (iter->state)
			{
			case CEditHandler::download:
				pListCtrl->SetItem(i, COLUMN_STATUS, _("Downloading"));
				break;
			case CEditHandler::upload:
				pListCtrl->SetItem(i, COLUMN_STATUS, _("Uploading"));
				break;
			case CEditHandler::upload_and_remove:
				pListCtrl->SetItem(i, COLUMN_STATUS, _("Uploading and pending removal"));
				break;
			case CEditHandler::upload_and_remove_failed:
				pListCtrl->SetItem(i, COLUMN_STATUS, _("Upload failed"));
				break;
			case CEditHandler::removing:
				pListCtrl->SetItem(i, COLUMN_STATUS, _("Pending removal"));
				break;
			case CEditHandler::edit:
				pListCtrl->SetItem(i, COLUMN_STATUS, _("Being edited"));
				break;
			default:
				pListCtrl->SetItem(i, COLUMN_STATUS, _("Unknown"));
				break;
			}
			pListCtrl->SetItem(i, COLUMN_REMOTEPATH, iter->site.Format(ServerFormat::with_user_and_optional_port) + iter->remotePath.GetPath());
			CEditHandler::t_fileData* pData = new CEditHandler::t_fileData(*iter);
			pListCtrl->SetItemPtrData(i, (wxUIntPtr)pData);
		}
	}

	{
		const std::list<CEditHandler::t_fileData>& files = pEditHandler->GetFiles(CEditHandler::local);
		unsigned int i = 0;
		for (std::list<CEditHandler::t_fileData>::const_iterator iter = files.begin(); iter != files.end(); ++iter, ++i)
		{
			pListCtrl->InsertItem(i, iter->file);
			pListCtrl->SetItem(i, COLUMN_TYPE, _("Local"));
			switch (iter->state)
			{
			case CEditHandler::upload:
				pListCtrl->SetItem(i, COLUMN_STATUS, _("Uploading"));
				break;
			case CEditHandler::upload_and_remove:
				pListCtrl->SetItem(i, COLUMN_STATUS, _("Uploading and unediting"));
				break;
			case CEditHandler::edit:
				pListCtrl->SetItem(i, COLUMN_STATUS, _("Being edited"));
				break;
			default:
				pListCtrl->SetItem(i, COLUMN_STATUS, _("Unknown"));
				break;
			}
			pListCtrl->SetItem(i, COLUMN_REMOTEPATH, iter->site.Format(ServerFormat::with_user_and_optional_port) + iter->remotePath.GetPath());
			CEditHandler::t_fileData* pData = new CEditHandler::t_fileData(*iter);
			pListCtrl->SetItemPtrData(i, (wxUIntPtr)pData);
		}
	}

	for (int i = 0; i < 4; ++i) {
		pListCtrl->SetColumnWidth(i, wxLIST_AUTOSIZE);
	}
	pListCtrl->SetMinSize(wxSize(pListCtrl->GetColumnWidth(0) + pListCtrl->GetColumnWidth(1) + pListCtrl->GetColumnWidth(2) + pListCtrl->GetColumnWidth(3) + 10, pListCtrl->GetMinSize().GetHeight()));
	GetSizer()->Fit(this);

	m_pWindowStateManager = new CWindowStateManager(this);
	m_pWindowStateManager->Restore(OPTION_EDITSTATUSDIALOG_SIZE, GetSize());

	SetCtrlState();

	int res = wxDialogEx::ShowModal();

	for (int i = 0; i < pListCtrl->GetItemCount(); ++i) {
		delete (CEditHandler::t_fileData*)pListCtrl->GetItemData(i);
	}

	return res;
}

void CEditHandlerStatusDialog::SetCtrlState()
{
	const CEditHandler* const pEditHandler = CEditHandler::Get();
	if (!pEditHandler) {
		return;
	}

	wxListCtrl* pListCtrl = XRCCTRL(*this, "ID_FILES", wxListCtrl);

	bool selectedEdited = false;
	bool selectedOther = false;
	bool selectedUploadRemoveFailed = false;

	int item = -1;
	while ((item = pListCtrl->GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != -1) {
		CEditHandler::fileType type;
		CEditHandler::t_fileData* pData = GetDataFromItem(item, type);
		if (pData->state == CEditHandler::edit) {
			selectedEdited = true;
		}
		else if (pData->state == CEditHandler::upload_and_remove_failed) {
			selectedUploadRemoveFailed = true;
		}
		else {
			selectedOther = true;
		}
	}

	bool select = selectedEdited && !selectedOther && !selectedUploadRemoveFailed;
	XRCCTRL(*this, "ID_UNEDIT", wxWindow)->Enable(select || (!selectedOther && selectedUploadRemoveFailed));
	XRCCTRL(*this, "ID_UPLOAD", wxWindow)->Enable(select || (!selectedEdited && !selectedOther && selectedUploadRemoveFailed));
	XRCCTRL(*this, "ID_UPLOADANDUNEDIT", wxWindow)->Enable(select || (!selectedEdited && !selectedOther && selectedUploadRemoveFailed));
	XRCCTRL(*this, "ID_EDIT", wxWindow)->Enable(select);
}

void CEditHandlerStatusDialog::OnSelectionChanged(wxListEvent&)
{
	SetCtrlState();
}

void CEditHandlerStatusDialog::OnUnedit(wxCommandEvent&)
{
	CEditHandler* const pEditHandler = CEditHandler::Get();
	if (!pEditHandler) {
		return;
	}

	wxListCtrl* pListCtrl = XRCCTRL(*this, "ID_FILES", wxListCtrl);

	std::list<int> files;
	int item = -1;
	while ((item = pListCtrl->GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != -1) {
		pListCtrl->SetItemState(item, 0, wxLIST_STATE_SELECTED);
		CEditHandler::fileType type;
		CEditHandler::t_fileData* pData = GetDataFromItem(item, type);
		if (pData->state != CEditHandler::edit && pData->state != CEditHandler::upload_and_remove_failed) {
			wxBell();
			return;
		}

		files.push_front(item);
	}

	for (std::list<int>::const_iterator iter = files.begin(); iter != files.end(); ++iter) {
		const int i = *iter;

		CEditHandler::fileType type;
		CEditHandler::t_fileData* pData = GetDataFromItem(i, type);

		if (type == CEditHandler::local) {
			pEditHandler->Remove(pData->file);
			delete pData;
			pListCtrl->DeleteItem(i);
		}
		else {
			if (pEditHandler->Remove(pData->name, pData->remotePath, pData->site)) {
				delete pData;
				pListCtrl->DeleteItem(i);
			}
			else {
				pListCtrl->SetItem(i, COLUMN_STATUS, _("Pending removal"));
			}
		}
	}

	SetCtrlState();
}

void CEditHandlerStatusDialog::OnUpload(wxCommandEvent& event)
{
	CEditHandler* const pEditHandler = CEditHandler::Get();
	if (!pEditHandler) {
		return;
	}

	wxListCtrl* pListCtrl = XRCCTRL(*this, "ID_FILES", wxListCtrl);

	std::list<int> files;
	int item = -1;
	while ((item = pListCtrl->GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != -1) {
		pListCtrl->SetItemState(item, 0, wxLIST_STATE_SELECTED);

		CEditHandler::fileType type;
		CEditHandler::t_fileData* pData = GetDataFromItem(item, type);

		if (pData->state != CEditHandler::edit && pData->state != CEditHandler::upload_and_remove_failed) {
			wxBell();
			return;
		}
		files.push_front(item);
	}

	for (std::list<int>::const_iterator iter = files.begin(); iter != files.end(); ++iter) {
		const int i = *iter;

		CEditHandler::fileType type;
		CEditHandler::t_fileData* pData = GetDataFromItem(i, type);

		bool unedit = event.GetId() == XRCID("ID_UPLOADANDUNEDIT") || pData->state == CEditHandler::upload_and_remove_failed;

		if (type == CEditHandler::local) {
			pEditHandler->UploadFile(pData->file, unedit);
		}
		else {
			pEditHandler->UploadFile(pData->name, pData->remotePath, pData->site, unedit);
		}

		if (!unedit) {
			pListCtrl->SetItem(i, COLUMN_STATUS, _("Uploading"));
		}
		else if (type == CEditHandler::remote) {
			pListCtrl->SetItem(i, COLUMN_STATUS, _("Uploading and pending removal"));
		}
		else {
			pListCtrl->SetItem(i, COLUMN_STATUS, _("Uploading and unediting"));
		}
	}

	SetCtrlState();
}

void CEditHandlerStatusDialog::OnEdit(wxCommandEvent&)
{
	CEditHandler* const pEditHandler = CEditHandler::Get();
	if (!pEditHandler) {
		return;
	}

	wxListCtrl* pListCtrl = XRCCTRL(*this, "ID_FILES", wxListCtrl);

	std::list<int> files;
	int item = -1;
	while ((item = pListCtrl->GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED)) != -1) {
		pListCtrl->SetItemState(item, 0, wxLIST_STATE_SELECTED);

		CEditHandler::fileType type;
		CEditHandler::t_fileData* pData = GetDataFromItem(item, type);

		if (pData->state != CEditHandler::edit) {
			wxBell();
			return;
		}
		files.push_front(item);
	}

	for (std::list<int>::const_iterator iter = files.begin(); iter != files.end(); ++iter) {
		const int i = *iter;

		CEditHandler::fileType type;
		CEditHandler::t_fileData* pData = GetDataFromItem(i, type);

		if (type == CEditHandler::local) {
			if (!pEditHandler->StartEditing(pData->file)) {
				if (pEditHandler->Remove(pData->file)) {
					delete pData;
					pListCtrl->DeleteItem(i);
				}
				else {
					pListCtrl->SetItem(i, COLUMN_STATUS, _("Pending removal"));
				}
			}
		}
		else {
			if (!pEditHandler->StartEditing(pData->name, pData->remotePath, pData->site)) {
				if (pEditHandler->Remove(pData->name, pData->remotePath, pData->site)) {
					delete pData;
					pListCtrl->DeleteItem(i);
				}
				else {
					pListCtrl->SetItem(i, COLUMN_STATUS, _("Pending removal"));
				}
			}
		}
	}

	SetCtrlState();
}

CEditHandler::t_fileData* CEditHandlerStatusDialog::GetDataFromItem(int item, CEditHandler::fileType &type)
{
	wxListCtrl* pListCtrl = XRCCTRL(*this, "ID_FILES", wxListCtrl);

	CEditHandler::t_fileData* pData = (CEditHandler::t_fileData*)pListCtrl->GetItemData(item);
	wxASSERT(pData);

	wxListItem info;
	info.SetMask(wxLIST_MASK_TEXT);
	info.SetId(item);
	info.SetColumn(1);
	pListCtrl->GetItem(info);
	if (info.GetText() == _("Local")) {
		type = CEditHandler::local;
	}
	else {
		type = CEditHandler::remote;
	}

	return pData;
}

//----------

BEGIN_EVENT_TABLE(CNewAssociationDialog, wxDialogEx)
EVT_RADIOBUTTON(wxID_ANY, CNewAssociationDialog::OnRadioButton)
EVT_BUTTON(wxID_OK, CNewAssociationDialog::OnOK)
EVT_BUTTON(XRCID("ID_BROWSE"), CNewAssociationDialog::OnBrowseEditor)
END_EVENT_TABLE()

CNewAssociationDialog::CNewAssociationDialog(wxWindow *parent)
	: m_pParent(parent)
{
}

bool CNewAssociationDialog::Run(const wxString &file)
{
	if (!Load(m_pParent, _T("ID_EDIT_NOPROGRAM"))) {
		return true;
	}

	int pos = file.Find('.', true);
	if (!pos) {
		m_ext = _T(".");
	}
	else if (pos != -1) {
		m_ext = file.Mid(pos + 1);
	}
	else {
		m_ext.clear();
	}

	wxStaticText *const pDesc = XRCCTRL(*this, "ID_DESC", wxStaticText);
	if (pDesc) {
		pDesc->SetLabel(wxString::Format(pDesc->GetLabel(), m_ext));
	}

	bool program_exists = false;
	wxString cmd = GetSystemOpenCommand(_T("foo.txt"), program_exists);
	if (!program_exists) {
		cmd.clear();
	}
	if (!cmd.empty()) {
		wxString args;
		if (!UnquoteCommand(cmd, args)) {
			cmd.clear();
		}
	}

	if (!PathExpand(cmd)) {
		cmd.clear();
	}

	if (cmd.empty()) {
		xrc_call(*this, "ID_USE_CUSTOM", &wxRadioButton::SetValue, true);
		xrc_call(*this, "ID_USE_EDITOR", &wxRadioButton::Enable, false);
		xrc_call(*this, "ID_EDITOR_DESC", &wxStaticText::Hide);
	}
	else {
		xrc_call(*this, "ID_EDITOR_DESC_NONE", &wxStaticText::Hide);
		wxStaticText* const pEditorDesc = XRCCTRL(*this, "ID_EDITOR_DESC", wxStaticText);
		if (pEditorDesc) {
			pEditorDesc->SetLabel(wxString::Format(pEditorDesc->GetLabel(), cmd));
		}
	}

	SetCtrlState();

	GetSizer()->Fit(this);

	if (ShowModal() != wxID_OK) {
		return false;
	}

	return true;
}

void CNewAssociationDialog::SetCtrlState()
{
	wxRadioButton* pCustom = dynamic_cast<wxRadioButton*>(FindWindow(XRCID("ID_USE_CUSTOM")));
	if (!pCustom) {
		// Return since it can get called before dialog got fully loaded
		return;
	}

	bool const custom = pCustom->GetValue();

	xrc_call(*this, "ID_CUSTOM", &wxTextCtrl::Enable, custom);
	xrc_call(*this, "ID_BROWSE", &wxButton::Enable, custom);
}

void CNewAssociationDialog::OnRadioButton(wxCommandEvent&)
{
	SetCtrlState();
}

void CNewAssociationDialog::OnOK(wxCommandEvent&)
{
	const bool custom = XRCCTRL(*this, "ID_USE_CUSTOM", wxRadioButton)->GetValue();
	const bool always = XRCCTRL(*this, "ID_ALWAYS", wxCheckBox)->GetValue();

	if (custom) {
		wxString cmd = XRCCTRL(*this, "ID_CUSTOM", wxTextCtrl)->GetValue();
		wxString editor = cmd;
		wxString args;
		if (!UnquoteCommand(editor, args) || editor.empty()) {
			wxMessageBoxEx(_("You need to enter a properly quoted command."), _("Cannot set file association"), wxICON_EXCLAMATION);
			return;
		}
		if (!ProgramExists(editor)) {
			wxMessageBoxEx(_("Selected editor does not exist."), _("Cannot set file association"), wxICON_EXCLAMATION, this);
			return;
		}

		if (always) {
			COptions::Get()->SetOption(OPTION_EDIT_DEFAULTEDITOR, _T("2") + cmd.ToStdWstring());
		}
		else {
			wxString associations = COptions::Get()->GetOption(OPTION_EDIT_CUSTOMASSOCIATIONS);
			if (!associations.empty() && associations.Last() != '\n') {
				associations += '\n';
			}
			if (m_ext.empty()) {
				m_ext = _T("/");
			}
			associations += m_ext.ToStdWstring() + _T(" ") + cmd.ToStdWstring();
			COptions::Get()->SetOption(OPTION_EDIT_CUSTOMASSOCIATIONS, associations.ToStdWstring());
		}
	}
	else {
		if (always) {
			COptions::Get()->SetOption(OPTION_EDIT_DEFAULTEDITOR, _T("1"));
		}
		else {
			bool program_exists = false;
			wxString cmd = GetSystemOpenCommand(_T("foo.txt"), program_exists);
			if (!program_exists) {
				cmd.clear();
			}
			if (!cmd.empty()) {
				wxString args;
				if (!UnquoteCommand(cmd, args)) {
					cmd.clear();
				}
			}
			if (cmd.empty()
#ifdef __WXGTK__
				|| !PathExpand(cmd)
#endif
				)
			{
				wxMessageBoxEx(_("The default editor for text files could not be found."), _("Cannot set file association"), wxICON_EXCLAMATION, this);
				return;
			}
			if (cmd.Find(' ') != -1) {
				cmd = _T("\"") + cmd + _T("\"");
			}
			wxString associations = COptions::Get()->GetOption(OPTION_EDIT_CUSTOMASSOCIATIONS);
			if (!associations.empty() && associations.Last() != '\n') {
				associations += '\n';
			}
			if (m_ext.empty()) {
				m_ext = _T("/");
			}
			associations += m_ext + _T(" ") + cmd;
			COptions::Get()->SetOption(OPTION_EDIT_CUSTOMASSOCIATIONS, associations.ToStdWstring());
		}
	}

	EndModal(wxID_OK);
}

void CNewAssociationDialog::OnBrowseEditor(wxCommandEvent&)
{
	wxFileDialog dlg(this, _("Select default editor"), _T(""), _T(""),
#ifdef __WXMSW__
		_T("Executable file (*.exe)|*.exe"),
#elif __WXMAC__
		_T("Applications (*.app)|*.app"),
#else
		wxFileSelectorDefaultWildcardStr,
#endif
		wxFD_OPEN | wxFD_FILE_MUST_EXIST);

	if (dlg.ShowModal() != wxID_OK) {
		return;
	}

	wxString editor = dlg.GetPath();
	if (editor.empty()) {
		return;
	}

	if (!ProgramExists(editor)) {
		XRCCTRL(*this, "ID_EDITOR", wxWindow)->SetFocus();
		wxMessageBoxEx(_("Selected editor does not exist."), _("File not found"), wxICON_EXCLAMATION, this);
		return;
	}

	if (editor.Find(' ') != -1) {
		editor = _T("\"") + editor + _T("\"");
	}

	XRCCTRL(*this, "ID_CUSTOM", wxTextCtrl)->ChangeValue(editor);
}

bool CEditHandler::Edit(CEditHandler::fileType type, std::wstring const& fileName, CServerPath const& path, Site const& site, int64_t size, wxWindow * parent)
{
	std::vector<FileData> data;
	FileData d{fileName, size};
	data.push_back(d);

	return Edit(type, data, path, site, parent);
}

bool CEditHandler::Edit(CEditHandler::fileType type, std::vector<FileData> const& data, CServerPath const& path, Site const& site, wxWindow * parent)
{
	if (type == CEditHandler::remote) {
		wxString const& localDir = GetLocalDirectory();
		if (localDir.empty()) {
			wxMessageBoxEx(_("Could not get temporary directory to download file into."), _("Cannot edit file"), wxICON_STOP);
			return false;
		}
	}

	if (data.empty()) {
		wxBell();
		return false;
	}

	if (data.size() > 10) {
		CConditionalDialog dlg(parent, CConditionalDialog::many_selected_for_edit, CConditionalDialog::yesno);
		dlg.SetTitle(_("Confirmation needed"));
		dlg.AddText(_("You have selected more than 10 files for editing, do you really want to continue?"));

		if (!dlg.Run()) {
			return false;
		}
	}

	bool success = true;
	int already_editing_action{};
	for (auto const& file : data) {
		if (!DoEdit(type, file, path, site, parent, data.size(), already_editing_action)) {
			success = false;
		}
	}

	return success;
}

bool CEditHandler::DoEdit(CEditHandler::fileType type, FileData const& file, CServerPath const& path, Site const& site, wxWindow * parent, size_t fileCount, int & already_editing_action)
{
	bool dangerous = false;
	bool program_exists = false;
	wxString cmd = CanOpen(type, file.name, dangerous, program_exists);
	if (cmd.empty()) {
		CNewAssociationDialog dlg(parent);
		if (!dlg.Run(file.name)) {
			return false;
		}
		cmd = CanOpen(type, file.name, dangerous, program_exists);
		if (cmd.empty()) {
			wxMessageBoxEx(wxString::Format(_("The file '%s' could not be opened:\nNo program has been associated on your system with this file type."), file.name), _("Opening failed"), wxICON_EXCLAMATION);
			return false;
		}
	}
	if (!program_exists) {
		wxString msg = wxString::Format(_("The file '%s' cannot be opened:\nThe associated program (%s) could not be found.\nPlease check your filetype associations."), file.name, cmd);
		wxMessageBoxEx(msg, _("Cannot edit file"), wxICON_EXCLAMATION);
		return false;
	}
	if (dangerous) {
		int res = wxMessageBoxEx(_("The selected file would be executed directly.\nThis can be dangerous and might damage your system.\nDo you really want to continue?"), _("Dangerous filetype"), wxICON_EXCLAMATION | wxYES_NO);
		if (res != wxYES) {
			wxBell();
			return false;
		}
	}

	fileState state;
	if (type == local) {
		state = GetFileState(file.name);
	}
	else {
		state = GetFileState(file.name, path, site);
	}
	switch (state)
	{
	case CEditHandler::download:
	case CEditHandler::upload:
	case CEditHandler::upload_and_remove:
	case CEditHandler::upload_and_remove_failed:
		wxMessageBoxEx(_("A file with that name is already being transferred."), _("Cannot view/edit selected file"), wxICON_EXCLAMATION);
		return false;
	case CEditHandler::removing:
		if (!Remove(file.name, path, site)) {
			wxMessageBoxEx(_("A file with that name is still being edited. Please close it and try again."), _("Selected file is already opened"), wxICON_EXCLAMATION);
			return false;
		}
		break;
	case CEditHandler::edit:
		{
			int action = already_editing_action;
			if (!action) {
				wxDialogEx dlg;
				if (!dlg.Load(parent, type == CEditHandler::local ? _T("ID_EDITEXISTING_LOCAL") : _T("ID_EDITEXISTING_REMOTE"))) {
					wxBell();
					return false;
				}
				dlg.SetChildLabel(XRCID("ID_FILENAME"), file.name);

				int choices = COptions::Get()->GetOptionVal(OPTION_PERSISTENT_CHOICES);

				if (fileCount < 2) {
					xrc_call(dlg, "ID_ALWAYS", &wxCheckBox::Hide);
				}
				else {
					if (choices & edit_choices::edit_existing_always) {
						xrc_call(dlg, "ID_ALWAYS", &wxCheckBox::SetValue, true);
					}
				}

				if (type == CEditHandler::remote && (choices & edit_choices::edit_existing_action)) {
					xrc_call(dlg, "ID_RETRANSFER", &wxRadioButton::SetValue, true);
				}

				dlg.GetSizer()->Fit(&dlg);
				int res = dlg.ShowModal();
				if (res != wxID_OK && res != wxID_YES) {
					wxBell();
					action = -1;
				}
				else if (type == CEditHandler::local || xrc_call(dlg, "ID_REOPEN", &wxRadioButton::GetValue)) {
					action = 1;
					if (type == CEditHandler::remote) {
						choices &= ~edit_choices::edit_existing_action;
					}
				}
				else {
					action = 2;
					choices |= edit_choices::edit_existing_action;
				}

				bool always = xrc_call(dlg, "ID_ALWAYS", &wxCheckBox::GetValue);
				if (always) {
					already_editing_action = action;
					choices |= edit_choices::edit_existing_always;
				}
				else {
					choices &= ~edit_choices::edit_existing_always;
				}
				COptions::Get()->SetOption(OPTION_PERSISTENT_CHOICES, choices);
			}

			if (action == -1) {
				return false;
			}
			else if (action == 1) {
				if (type == CEditHandler::local) {
					StartEditing(file.name);
				}
				else {
					StartEditing(file.name, path, site);
				}
				return true;
			}
			else {
				if (!Remove(file.name, path, site)) {
					wxMessageBoxEx(_("The selected file is still opened in some other program, please close it."), _("Selected file is still being edited"), wxICON_EXCLAMATION);
					return false;
				}
			}
		}
		break;
	default:
		break;
	}

	std::wstring localFile = file.name;
	if (!AddFile(type, localFile, path, site)) {
		if( type == CEditHandler::local) {
			wxMessageBoxEx(wxString::Format(_("The file '%s' could not be opened:\nThe associated command failed"), file.name), _("Opening failed"), wxICON_EXCLAMATION);
		}
		else {
			wxFAIL;
			wxBell();
			return false;
		}
	}

	if (type == CEditHandler::remote) {
		std::wstring localFileName;
		CLocalPath localPath(localFile, &localFileName);

		m_pQueue->QueueFile(false, true, file.name, (localFileName != file.name) ? localFileName : std::wstring(),
			localPath, path, site, file.size, type, QueuePriority::high);
		m_pQueue->QueueFile_Finish(true);
	}

	return true;
}
