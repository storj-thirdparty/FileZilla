#include <filezilla.h>
#include "auto_ascii_files.h"
#include "Options.h"

#include <libfilezilla/local_filesys.hpp>

std::vector<std::wstring> CAutoAsciiFiles::ascii_extensions_;

void CAutoAsciiFiles::SettingsChanged()
{
	ascii_extensions_.clear();
	std::wstring extensions = COptions::Get()->GetOption(OPTION_ASCIIFILES);
	std::wstring ext;
	size_t pos = extensions.find('|');
	while (pos != std::wstring::npos) {
		if (!pos) {
			if (!ext.empty()) {
				fz::replace_substrings(ext, L"\\\\", L"\\");
				ascii_extensions_.push_back(ext);
				ext.clear();
			}
		}
		else if (extensions[pos - 1] != '\\') {
			ext += extensions.substr(0, pos);
			fz::replace_substrings(ext, L"\\\\", L"\\");
			ascii_extensions_.push_back(ext);
			ext.clear();
		}
		else {
			ext += extensions.substr(0, pos - 1) + L"|";
		}
		extensions = extensions.substr(pos + 1);
		pos = extensions.find('|');
	}
	ext += extensions;
	fz::replace_substrings(ext, L"\\\\", L"\\");
	if (!ext.empty()) {
		ascii_extensions_.push_back(ext);
	}
}

// Defined in RemoteListView.cpp
std::wstring StripVMSRevision(std::wstring const& name);

bool CAutoAsciiFiles::TransferLocalAsAscii(std::wstring const& local_file, ServerType server_type)
{
	size_t pos = local_file.rfind(fz::local_filesys::path_separator);

	// Identical implementation, only difference is for the local one to strip path.
	return TransferRemoteAsAscii(
		(pos != std::wstring::npos) ? local_file.substr(pos + 1) : local_file,
		server_type
	);
}

bool CAutoAsciiFiles::TransferRemoteAsAscii(std::wstring const& remote_file, ServerType server_type)
{
	int mode = COptions::Get()->GetOptionVal(OPTION_ASCIIBINARY);
	if (mode == 1) {
		return true;
	}
	else if (mode == 2) {
		return false;
	}

	if (server_type == VMS) {
		return TransferRemoteAsAscii(StripVMSRevision(remote_file), DEFAULT);
	}

	if (!remote_file.empty() && remote_file[0] == '.') {
		return COptions::Get()->GetOptionVal(OPTION_ASCIIDOTFILE) != 0;
	}

	size_t pos = remote_file.rfind('.');
	if (pos == std::wstring::npos || pos + 1 == remote_file.size()) {
		return COptions::Get()->GetOptionVal(OPTION_ASCIINOEXT) != 0;
	}
	std::wstring ext = remote_file.substr(pos + 1);

	for (auto const& ascii_ext : ascii_extensions_) {
		if (fz::equal_insensitive_ascii(ext, ascii_ext)) {
			return true;
		}
	}

	return false;
}
