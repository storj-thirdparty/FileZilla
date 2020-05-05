#include "filezilla.h"

#include "file_utils.h"

#include <wx/stdpaths.h>
#ifdef FZ_WINDOWS
#include <knownfolders.h>
#include <shlobj.h>
#include <shlwapi.h>
#else
#include <wx/mimetype.h>
#include <wx/textfile.h>
#include <wordexp.h>
#endif

#include "Options.h"

std::wstring GetAsURL(std::wstring const& dir)
{
	// Cheap URL encode
	std::string utf8 = fz::to_utf8(dir);

	std::wstring encoded;
	encoded.reserve(utf8.size());

	char const* p = utf8.c_str();
	while (*p) {
		// List of characters that don't need to be escaped taken
		// from the BNF grammar in RFC 1738
		// Again attention seeking Windows wants special treatment...
		unsigned char const c = static_cast<unsigned char>(*p++);
		if ((c >= 'a' && c <= 'z') ||
			(c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') ||
			c == '$' ||
			c == '_' ||
			c == '-' ||
			c == '.' ||
			c == '+' ||
			c == '!' ||
			c == '*' ||
#ifndef __WXMSW__
			c == '\'' ||
#endif
			c == '(' ||
			c == ')' ||
			c == ',' ||
			c == '?' ||
			c == ':' ||
			c == '@' ||
			c == '&' ||
			c == '=' ||
			c == '/')
		{
			encoded += c;
		}
#ifdef __WXMSW__
		else if (c == '\\') {
			encoded += '/';
		}
#endif
		else {
			encoded += fz::sprintf(L"%%%x", c);
		}
	}
#ifdef __WXMSW__
	if (fz::starts_with(encoded, std::wstring(L"//"))) {
		// UNC path
		encoded = encoded.substr(2);
	}
	else {
		encoded = L"/" + encoded;
	}
#endif
	return L"file://" + encoded;
}

bool OpenInFileManager(std::wstring const& dir)
{
	bool ret = false;
#ifdef __WXMSW__
	// Unfortunately under Windows, UTF-8 encoded file:// URLs don't work, so use native paths.
	// Unfortunatelier, we cannot use this for UNC paths, have to use file:// here
	// Unfortunateliest, we again have a problem with UTF-8 characters which we cannot fix...
	if (dir.substr(0, 2) != L"\\\\" && dir != L"/") {
		ret = wxLaunchDefaultBrowser(dir);
	}
	else
#endif
	{
		std::wstring url = GetAsURL(dir);
		if (!url.empty()) {
			ret = wxLaunchDefaultBrowser(url);
		}
	}


	if (!ret) {
		wxBell();
	}

	return ret;
}

namespace {
bool PathExpand(std::wstring& cmd)
{
	if (cmd.empty()) {
		return false;
	}
#ifndef __WXMSW__
	if (!cmd.empty() && cmd[0] == '/') {
		return true;
	}
#else
	if (!cmd.empty() && cmd[0] == '\\') {
		// UNC or root of current working dir, whatever that is
		return true;
	}
	if (cmd.size() > 2 && cmd[1] == ':') {
		// Absolute path
		return true;
	}
#endif

	// Need to search for program in $PATH
	std::wstring path = GetEnv("PATH");
	if (path.empty()) {
		return false;
	}

	wxString full_cmd;
	bool found = wxFindFileInPath(&full_cmd, path, cmd);
#ifdef __WXMSW__
	if (!found && !fz::equal_insensitive_ascii(cmd.substr(cmd.size() - 1), L".exe")) {
		cmd += L".exe";
		found = wxFindFileInPath(&full_cmd, path, cmd);
	}
#endif

	if (!found) {
		return false;
	}

	cmd = full_cmd;
	return true;
}
}

std::vector<std::wstring> GetSystemAssociation(std::wstring const& file)
{
	std::vector<std::wstring> ret;

	auto const ext = GetExtension(file);
	if (ext.empty() || ext == L".") {
		return ret;
	}

#if FZ_WINDOWS
	auto query = [&](wchar_t const* verb) {
		DWORD len{};
		int res = AssocQueryString(0, ASSOCSTR_COMMAND, (L"." + ext).c_str(), verb, nullptr, &len);
		if (res == S_FALSE && len > 1) {
			std::wstring raw;

			// len as returned by AssocQueryString includes terminating null
			raw.resize(static_cast<size_t>(len - 1));

			res = AssocQueryString(0, ASSOCSTR_COMMAND, (L"." + ext).c_str(), verb, raw.data(), &len);
			if (res == S_OK && len > 1) {
				raw.resize(len - 1);
				return UnquoteCommand(raw);
			}
		}

		return std::vector<std::wstring>();
	};

	ret = query(L"edit");
	if (ret.empty()) {
		ret = query(nullptr);
	}
	
	std::vector<std::wstring> raw;
	std::swap(ret, raw);
	if (!raw.empty()) {
		ret.emplace_back(std::move(raw.front()));
	}

	// Process placeholders.

	// Phase 1: Look for %1
	bool got_first{};
	for (size_t i = 1; i < raw.size(); ++i) {
		auto const& arg = raw[i];

		std::wstring out;
		out.reserve(arg.size());
		bool percent{};
		for (auto const& c : arg) {
			if (percent) {
				if (!got_first) {
					if (c == '1') {
						out += L"%f";
						got_first = true;
					}
					else if (c == '*') {
						out += '%';
						out += c;
					}
					// Ignore others.
				}

				percent = false;
			}
			else if (c == '%') {
				percent = true;
			}
			else {
				out += c;
			}
		}

		if (!out.empty() || arg.empty()) {
			ret.emplace_back(std::move(out));
		}
	}

	std::swap(ret, raw);
	ret.clear();
	if (!raw.empty()) {
		ret.emplace_back(std::move(raw.front()));
	}

	// Phase 2: Filter %*
	for (size_t i = 1; i < raw.size(); ++i) {
		auto& arg = raw[i];

		std::wstring out;
		out.reserve(arg.size());
		bool percent{};
		for (auto const& c : arg) {
			if (percent) {
				if (c == '*') {
					if (!got_first) {
						out += L"%f";
					}
				}
				else if (c == 'f') {
					out += L"%f";
				}
				// Ignore others.

				percent = false;
			}
			else if (c == '%') {
				percent = true;
			}
			else {
				out += c;
			}
		}

		if (!out.empty() || arg.empty()) {
			ret.emplace_back(std::move(out));
		}
	}
#else
	// wxWidgets doens't escape properly.
	// For now don't support these files until we can replace wx with something sane.
	if (ext.find_first_of(L"\"\\") != std::wstring::npos) {
		return ret;
	}

	std::unique_ptr<wxFileType> pType(wxTheMimeTypesManager->GetFileTypeFromExtension(ext));
	if (!pType) {
		return ret;
	}

	std::wstring const placeholder = L"placeholderJkpZ0eet6lWsI8glm3uSJYZyvn7WZn5S";
	ret = UnquoteCommand(pType->GetOpenCommand(placeholder).ToStdWstring());

	bool replaced{};
	for (size_t i = 0; i < ret.size(); ++i) {
		auto& arg = ret[i];

		fz::replace_substrings(arg, L"%", L"%%");
		if (fz::replace_substrings(arg, placeholder, L"%f")) {
			if (!i) {
				// Placeholder found in the command itself? Something went wrong.
				ret.clear();
				return ret;
			}
			replaced = true;
		}
	}

	if (!replaced) {
		ret.clear();
	}

	if (!ret.empty()) {
		if (!PathExpand(ret[0])) {
			ret.clear();
		}
	}

#endif

	if (!ret.empty() && !ProgramExists(ret[0])) {
		ret.clear();
	}

	return ret;
}

std::vector<fz::native_string> AssociationToCommand(std::vector<std::wstring> const& association, std::wstring_view const& file)
{
	std::vector<fz::native_string> ret;
	ret.reserve(association.size());

	if (!association.empty()) {
		ret.push_back(fz::to_native(association.front()));
	}

	bool replaced{};

	for (size_t i = 1; i < association.size(); ++i) {
		bool percent{};
		std::wstring const& arg = association[i];

		std::wstring out;
		out.reserve(arg.size());
		for (auto const& c : arg) {
			if (percent) {
				percent = false;
				if (c == 'f') {
					replaced = true;
					out += file;
				}
				else {
					out += c;
				}
			}
			else if (c == '%') {
				percent = true;
			}
			else {
				out += c;
			}
		}
		ret.emplace_back(fz::to_native(out));
	}

	if (!replaced) {
		ret.emplace_back(fz::to_native(file));
	}

	return ret;
}

std::wstring QuoteCommand(std::vector<std::wstring> const& cmd_with_args)
{
	std::wstring ret;

	for (auto const& arg : cmd_with_args) {
		if (!ret.empty()) {
			ret += ' ';
		}
		size_t pos = arg.find_first_of(L" \t\"'");
		if (pos != std::wstring::npos || arg.empty()) {
			ret += '"';
			ret += fz::replaced_substrings(arg, L"\"", L"\"\"");
			ret += '"';
		}
		else {
			ret += arg;
		}
	}

	return ret;
}

std::optional<std::wstring> UnquoteFirst(std::wstring_view & command)
{
	std::optional<std::wstring> ret;

	bool quoted{};
	size_t i = 0;
	for (; i < command.size(); ++i) {
		wchar_t const& c = command[i];

		if ((c == ' ' || c == '\t' || c == '\r' || c == '\n') && !quoted) {
			if (ret) {
				break;
			}
		}
		else {
			if (!ret) {
				ret = std::wstring();
			}
			if (c == '"') {
				if (!quoted) {
					quoted = true;
				}
				else if (i + 1 != command.size() && command[i + 1] == '"') {
					*ret += '"';
					++i;
				}
				else {
					quoted = false;
				}
			}
			else {
				ret->push_back(c);
			}
		}
	}
	if (quoted) {
		ret.reset();
	}

	if (ret) {
		for (; i < command.size(); ++i) {
			wchar_t const& c = command[i];
			if (!(c == ' ' || c == '\t' || c == '\r' || c == '\n')) {
				break;
			}
		}
		command = command.substr(i);
	}

	return ret;
}

std::vector<std::wstring> UnquoteCommand(std::wstring_view command)
{
	std::vector<std::wstring> ret;

	while (!command.empty()) {
		auto part = UnquoteFirst(command);
		if (!part) {
			break;
		}

		ret.emplace_back(std::move(*part));
	}

	if (!command.empty()) {
		ret.clear();
	}

	if (!ret.empty() && ret.front().empty()) {
		// Commands may have empty arguments, but themselves cannot be empty
		ret.clear();
	}

	return ret;
}

bool ProgramExists(std::wstring const& editor)
{
	if (wxFileName::FileExists(editor)) {
		return true;
	}

#ifdef __WXMAC__
	std::wstring_view e = editor;
	if (!e.empty() && e.back() == '/') {
		e = e.substr(0, e.size() - 1);
	}
	if (fz::ends_with(e, std::wstring_view(L".app")) && wxFileName::DirExists(editor)) {
		return true;
	}
#endif

	return false;
}

bool RenameFile(wxWindow* parent, wxString dir, wxString from, wxString to)
{
	if (dir.Right(1) != _T("\\") && dir.Right(1) != _T("/")) {
		dir += wxFileName::GetPathSeparator();
	}

#ifdef __WXMSW__
	to = to.Left(255);

	if ((to.Find('/') != -1) ||
		(to.Find('\\') != -1) ||
		(to.Find(':') != -1) ||
		(to.Find('*') != -1) ||
		(to.Find('?') != -1) ||
		(to.Find('"') != -1) ||
		(to.Find('<') != -1) ||
		(to.Find('>') != -1) ||
		(to.Find('|') != -1))
	{
		wxMessageBoxEx(_("Filenames may not contain any of the following characters: / \\ : * ? \" < > |"), _("Invalid filename"), wxICON_EXCLAMATION, parent);
		return false;
	}

	SHFILEOPSTRUCT op;
	memset(&op, 0, sizeof(op));

	from = dir + from + _T(" ");
	from.SetChar(from.Length() - 1, '\0');
	op.pFrom = from.wc_str();
	to = dir + to + _T(" ");
	to.SetChar(to.Length()-1, '\0');
	op.pTo = to.wc_str();
	op.hwnd = (HWND)parent->GetHandle();
	op.wFunc = FO_RENAME;
	op.fFlags = FOF_ALLOWUNDO;

	wxWindow * focused = wxWindow::FindFocus();

	bool res = SHFileOperation(&op) == 0;

	if (focused) {
		// Microsoft introduced a bug in Windows 10 1803: Calling SHFileOperation resets focus.
		focused->SetFocus();
	}
	
	return res;
#else
	if ((to.Find('/') != -1) ||
		(to.Find('*') != -1) ||
		(to.Find('?') != -1) ||
		(to.Find('<') != -1) ||
		(to.Find('>') != -1) ||
		(to.Find('|') != -1))
	{
		wxMessageBoxEx(_("Filenames may not contain any of the following characters: / * ? < > |"), _("Invalid filename"), wxICON_EXCLAMATION, parent);
		return false;
	}

	return wxRename(dir + from, dir + to) == 0;
#endif
}

#if defined __WXMAC__
char const* GetDownloadDirImpl();
#elif !defined(__WXMSW__)
wxString ShellUnescape(wxString const& path)
{
	wxString ret;

	const wxWX2MBbuf buf = path.mb_str();
	if (buf && *buf) {
		wordexp_t p;
		int res = wordexp(buf, &p, WRDE_NOCMD);
		if (!res && p.we_wordc == 1 && p.we_wordv) {
			ret = wxString(p.we_wordv[0], wxConvLocal);
		}
		wordfree(&p);
	}
	return ret;
}
#endif

CLocalPath GetDownloadDir()
{
#ifdef __WXMSW__
	PWSTR path;
	HRESULT result = SHGetKnownFolderPath(FOLDERID_Downloads, 0, 0, &path);
	if(result == S_OK) {
		std::wstring dir = path;
		CoTaskMemFree(path);
		return CLocalPath(dir);
	}
#elif defined(__WXMAC__)
	CLocalPath ret;
	char const* url = GetDownloadDirImpl();
	ret.SetPath(fz::to_wstring_from_utf8(url));
	return ret;
#else
	// Code copied from wx, but for downloads directory.
	// Also, directory is now unescaped.
	{
		wxLogNull logNull;
		wxString homeDir = wxFileName::GetHomeDir();
		wxString configPath;
		if (wxGetenv(wxT("XDG_CONFIG_HOME"))) {
			configPath = wxGetenv(wxT("XDG_CONFIG_HOME"));
		}
		else {
			configPath = homeDir + wxT("/.config");
		}
		wxString dirsFile = configPath + wxT("/user-dirs.dirs");
		if (wxFileExists(dirsFile)) {
			wxTextFile textFile;
			if (textFile.Open(dirsFile)) {
				size_t i;
				for (i = 0; i < textFile.GetLineCount(); ++i) {
					wxString line(textFile[i]);
					int pos = line.Find(wxT("XDG_DOWNLOAD_DIR"));
					if (pos != wxNOT_FOUND) {
						wxString value = line.AfterFirst(wxT('='));
						value = ShellUnescape(value);
						if (!value.empty() && wxDirExists(value)) {
							return CLocalPath(value.ToStdWstring());
						}
						else {
							break;
						}
					}
				}
			}
		}
	}
#endif
	return CLocalPath(wxStandardPaths::Get().GetDocumentsDir().ToStdWstring());
}

std::wstring GetExtension(std::wstring_view file)
{
	// Strip path if any
#ifdef FZ_WINDOWS
	size_t pos = file.find_last_of(L"/\\");
#else
	size_t pos = file.find_last_of(L"/");
#endif
	if (pos != std::wstring::npos) {
		file = file.substr(pos + 1);
	}

	// Find extension
	pos = file.find_last_of('.');
	if (!pos) {
		return std::wstring(L".");
	}
	else if (pos != std::wstring::npos) {
		return std::wstring(file.substr(pos + 1));
	}

	return std::wstring();
}

bool IsInvalidChar(wchar_t c, bool includeQuotesAndBreaks)
{
	switch (c)
	{
	case '/':
#ifdef __WXMSW__
	case '\\':
	case ':':
	case '*':
	case '?':
	case '"':
	case '<':
	case '>':
	case '|':
#endif
		return true;


	case '\'':
#ifndef __WXMSW__
	case '"':
	case '\\':
#endif
		return includeQuotesAndBreaks;

	default:
		if (c < 0x20) {
#ifdef __WXMSW__
			return true;
#else
			return includeQuotesAndBreaks;
#endif
		}
		return false;
	}
}
