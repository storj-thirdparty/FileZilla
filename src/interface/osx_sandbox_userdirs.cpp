#include <filezilla.h>
#include "osx_sandbox_userdirs.h"

#include "filezillaapp.h"
#include "ipcmutex.h"
#include "Options.h"
#include "xmlfunctions.h"
#include "xrc_helper.h"

#include <wx/dirdlg.h>
#include <wx/osx/core/cfstring.h>

OSXSandboxUserdirs::OSXSandboxUserdirs()
{
}

OSXSandboxUserdirs::~OSXSandboxUserdirs()
{
	for (auto const& dir : userdirs_) {
		CFURLStopAccessingSecurityScopedResource(dir.second.url.get());
	}
}


OSXSandboxUserdirs& OSXSandboxUserdirs::Get()
{
	static OSXSandboxUserdirs userDirs;
	return userDirs;
}


namespace {
std::wstring GetPath(CFURLRef url)
{
	char buf[2048];
	if (!CFURLGetFileSystemRepresentation(url, true, reinterpret_cast<uint8_t*>(buf), sizeof(buf))) {
		return std::wstring();
	}

	return fz::to_wstring(std::string(buf));
}

void append(wxString& error, CFErrorRef ref, wxString const& func)
{
	wxString s;
	if (ref) {
		wxCFStringRef sref(CFErrorCopyDescription(ref));
		s = sref.AsString();
	}
	error += "\n";
	if (s.empty()) {
		error += wxString::Format(_("Function %s failed"), func);
	}
	else {
		error += s;
	}
}
}

void OSXSandboxUserdirs::Load()
{
	CInterProcessMutex mutex(MUTEX_MAC_SANDBOX_USERDIRS);

	CXmlFile file(wxGetApp().GetSettingsFile(L"osx_sandbox_userdirs"));
	auto xml = file.Load(true);
	if (!xml) {
		wxMessageBoxEx(file.GetError(), _("Error loading xml file"), wxICON_ERROR);
		return;
	}

	wxString error;
	for (auto xdir = xml.child("dir"); xdir; xdir = xdir.next_sibling("dir")) {
		auto data = fz::hex_decode(std::string(xdir.child_value()));
		if (data.empty()) {
			continue;
		}

		wxCFDataRef bookmark(data.data(), data.size());

		Boolean stale = false;
		CFErrorRef errorRef = 0;
		wxCFRef<CFURLRef> url(CFURLCreateByResolvingBookmarkData(0, bookmark.get(), kCFURLBookmarkResolutionWithSecurityScope, 0, 0, &stale, &errorRef));
		if (!url) {
			append(error, errorRef, L"CFURLCreateByResolvingBookmarkData");
			continue;
		}

		if (stale) {
			wxCFDataRef new_bookmark(CFURLCreateBookmarkData(0, url.get(), kCFURLBookmarkCreationWithSecurityScope, 0, 0, 0));
			if (new_bookmark) {
				bookmark = new_bookmark;
			}
		}

		auto path = GetPath(url);
		if (path.empty()) {
			error += L"\n";
			error += _("Could not get native path from CFURL");
			continue;
		}

		if (!CFURLStartAccessingSecurityScopedResource(url.get())) {
			error += "\n";
			error += wxString::Format(_("Could not access path %s"), path);
			continue;
		}

		bool dir = xdir.attribute("dir").value() != std::string("false");
		if (dir && path.back() != '/') {
			path += '/';
		}

		auto it = userdirs_.find(path);
		if (it != userdirs_.end()) {
			CFURLStopAccessingSecurityScopedResource(it->second.url.get());
		}
		userdirs_[path] = Data{dir, bookmark, url};
	}

	if (!error.empty()) {
		error = _("Access to some local directories could not be restored:") + _T("\n") + error;
		error += L"\n\n";
		error += _("Please check the granted permissions and re-add any missing local directories you are working with in the dialog that follows.");
		wxMessageBoxEx(error, _("Could not restore directory access"), wxICON_EXCLAMATION);
		OSXSandboxUserdirsDialog dlg;
		dlg.Run(0);
	}

	Save();
}


bool OSXSandboxUserdirs::Save()
{
	CXmlFile file(wxGetApp().GetSettingsFile(L"osx_sandbox_userdirs"));
	auto xml = file.Load(true);
	if (!xml) {
		wxMessageBoxEx(file.GetError(), _("Error loading xml file"), wxICON_ERROR);
		return false;
	}

	while (xml.remove_child("dir")) {}

	for (auto const& dir : userdirs_) {
		std::vector<uint8_t> data;
		data.resize(dir.second.bookmark.GetLength());
		dir.second.bookmark.GetBytes(CFRangeMake(0, data.size()), data.data());

		auto child = xml.append_child("dir");
		child.append_attribute("path") = fz::to_utf8(dir.first).c_str();
		child.append_attribute("dir") = dir.second.dir ? "true" : "false";
		child.text().set(fz::hex_encode<std::string>(data).c_str());
	}

	return file.Save(true);
}

bool OSXSandboxUserdirs::Add()
{
	std::wstring home = GetEnv("HOME");
	wxDirDialog dlg(0, (L"Select local data directory"), home, wxDD_DEFAULT_STYLE|wxDD_DIR_MUST_EXIST);
	if (dlg.ShowModal() != wxID_OK) {
		return false;
	}

	auto path = dlg.GetPath().ToStdWstring();
	wxCFStringRef pathref(path);
	wxCFRef<CFURLRef> url(CFURLCreateWithFileSystemPath(0, pathref.get(), kCFURLPOSIXPathStyle, true));
	if (!url) {
		wxMessageBoxEx(wxString::Format(_("Could not create CFURL from path %s"), path));
		return false;
	}

	CFErrorRef errorRef = 0;
	wxCFDataRef bookmark(CFURLCreateBookmarkData(0, url.get(), kCFURLBookmarkCreationWithSecurityScope, 0, 0, &errorRef));
	if (!bookmark) {
		wxString error;
		append(error, errorRef, L"CFURLCreateBookmarkData");
		wxMessageBoxEx(_("Could not create security-scoped bookmark from URL:") + error);
		return false;
	}

	std::wstring actualPath = GetPath(url.get());
	if (actualPath.empty()) {
		wxMessageBoxEx(_("Could not get path from URL"));
		return false;
	}

	if (actualPath.back() != '/') {
		actualPath += '/';
	}

	auto it = userdirs_.find(actualPath);
	if (it != userdirs_.end()) {
		CFURLStopAccessingSecurityScopedResource(it->second.url.get());
	}
	userdirs_[actualPath] = Data{true, bookmark, url};

	CInterProcessMutex mutex(MUTEX_MAC_SANDBOX_USERDIRS);

	return Save();
}

bool OSXSandboxUserdirs::AddFile(std::wstring const& file)
{
	if (file.empty() || file.back() == '/') {
		return false;
	}

	wxCFStringRef pathref(file);
	wxCFRef<CFURLRef> url(CFURLCreateWithFileSystemPath(0, pathref.get(), kCFURLPOSIXPathStyle, true));
	if (!url) {
		wxMessageBoxEx(wxString::Format(_("Could not create CFURL from path %s"), file));
		return false;
	}

	CFErrorRef errorRef = 0;
	wxCFDataRef bookmark(CFURLCreateBookmarkData(0, url.get(), kCFURLBookmarkCreationWithSecurityScope, 0, 0, &errorRef));
	if (!bookmark) {
		wxString error;
		append(error, errorRef, L"CFURLCreateBookmarkData");
		wxMessageBoxEx(_("Could not create security-scoped bookmark from URL:") + error);
		return false;
	}

	std::wstring actualPath = GetPath(url.get());
	if (actualPath.empty()) {
		wxMessageBoxEx(_("Could not get path from URL"));
		return false;
	}

	auto it = userdirs_.find(actualPath);
	if (it != userdirs_.end()) {
		CFURLStopAccessingSecurityScopedResource(it->second.url.get());
	}
	userdirs_[actualPath] = Data{false, bookmark, url};

	CInterProcessMutex mutex(MUTEX_MAC_SANDBOX_USERDIRS);

	return Save();
}

std::vector<std::wstring> OSXSandboxUserdirs::GetDirs() const
{
	std::vector<std::wstring> ret;
	ret.reserve(userdirs_.size());
	for (auto const& it : userdirs_) {
		ret.push_back(it.first);
	}
	return ret;
}

void OSXSandboxUserdirs::Remove(std::wstring const& dir)
{
	auto it = userdirs_.find(dir);
	if (it != userdirs_.cend()) {
		CFURLStopAccessingSecurityScopedResource(it->second.url.get());
		userdirs_.erase(it);
	}

	CInterProcessMutex mutex(MUTEX_MAC_SANDBOX_USERDIRS);

	Save();
}

void OSXSandboxUserdirsDialog::Run(wxWindow* parent, bool initial)
{
	if (!Load(parent, L"ID_OSX_SANDBOX_USERDIRS")) {
		wxBell();
		return;
	}

	WrapRecursive(this, GetSizer(), ConvertDialogToPixels(wxSize(250, -1)).x);
	GetSizer()->Fit(this);

	XRCCTRL(*this, "ID_ADD", wxButton)->Bind(wxEVT_BUTTON, &OSXSandboxUserdirsDialog::OnAdd, this);
	XRCCTRL(*this, "ID_REMOVE", wxButton)->Bind(wxEVT_BUTTON, &OSXSandboxUserdirsDialog::OnRemove, this);

	XRCCTRL(*this, "wxID_OK", wxButton)->Bind(wxEVT_BUTTON, [initial](wxCommandEvent& evt) {
		if (initial && OSXSandboxUserdirs::Get().GetDirs().empty()) {
			wxMessageBoxEx(_("Please add at least one directory you want to download files into or to upload files from."), _("No directory added"));
		}
		else {
			evt.Skip();
		}
	});

	DisplayCurrentDirs();

	ShowModal();
}

void OSXSandboxUserdirsDialog::OnAdd(wxCommandEvent&)
{
	OSXSandboxUserdirs::Get().Add();
	DisplayCurrentDirs();
}

void OSXSandboxUserdirsDialog::OnRemove(wxCommandEvent&)
{
	int pos = xrc_call(*this, "ID_DIRS", &wxListBox::GetSelection);
	if (pos != wxNOT_FOUND) {
		wxString sel = xrc_call(*this, "ID_DIRS", &wxListBox::GetString, pos);
		OSXSandboxUserdirs::Get().Remove(sel.ToStdWstring());
		DisplayCurrentDirs();
	}
}

void OSXSandboxUserdirsDialog::DisplayCurrentDirs()
{
	auto dirs = OSXSandboxUserdirs::Get().GetDirs();

	wxString sel;
	int pos = xrc_call(*this, "ID_DIRS", &wxListBox::GetSelection);
	if (pos != wxNOT_FOUND) {
		sel = xrc_call(*this, "ID_DIRS", &wxListBox::GetString, pos);
	}

	xrc_call(*this, "ID_DIRS", &wxListBox::Clear);

	for (auto const& dir : dirs) {
		XRCCTRL(*this, "ID_DIRS", wxListBox)->Append(dir);
	}

	XRCCTRL(*this, "ID_DIRS", wxListBox)->SetStringSelection(sel);
}
