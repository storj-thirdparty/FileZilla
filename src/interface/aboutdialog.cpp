#include <filezilla.h>

#include "aboutdialog.h"
#include "buildinfo.h"
#include "Options.h"
#include "themeprovider.h"

#include <misc.h>

#include <wx/hyperlink.h>
#include <wx/clipbrd.h>
#include <wx/statline.h>
#include <wx/statbmp.h>
#include <wx/statbox.h>

bool CAboutDialog::Create(wxWindow* parent)
{
	wxDialogEx::Create(parent, -1, _("About FileZilla"));

	auto & lay = layout();
	auto main = lay.createMain(this, 1);

	auto top = lay.createFlex(2);
	main->Add(top);

	top->Add(new wxStaticBitmap(this, -1, CThemeProvider::Get()->CreateBitmap("ART_FILEZILLA", wxString(), CThemeProvider::GetIconSize(iconSizeLarge))), 0, wxALL, lay.border);

	auto topRight = lay.createFlex(1);
	top->Add(topRight);

	std::wstring version = L"FileZilla " + CBuildInfo::GetVersion();
	if (CBuildInfo::GetBuildType() == L"nightly") {
		version += L"-nightly";
	}
	topRight->Add(new wxStaticText(this, -1, version));
	topRight->Add(new wxStaticText(this, -1, L"Copyright (C) 2004-2020  Tim Kosse"));

	auto homepage = lay.createFlex(2);
	homepage->Add(new wxStaticText(this, -1, _("Homepage:")), lay.valign);
	homepage->Add(new wxHyperlinkCtrl(this, -1, L"https://filezilla-project.org/", L"https://filezilla-project.org/"), lay.valign);
	topRight->Add(homepage);

	{
		auto [box, inner] = lay.createStatBox(main, _("Build information"), 2);

		std::wstring host = CBuildInfo::GetHostname();
		if (!host.empty()) {
			inner->Add(new wxStaticText(box, -1, _("Compiled for:")));
			inner->Add(new wxStaticText(box, -1, host));
		}

		std::wstring build = CBuildInfo::GetBuildSystem();
		if (!build.empty()) {
			inner->Add(new wxStaticText(box, -1, _("Compiled on:")));
			inner->Add(new wxStaticText(box, -1, build));
		}
		inner->Add(new wxStaticText(box, -1, _("Build date:")));
		inner->Add(new wxStaticText(box, -1, CBuildInfo::GetBuildDateString()));

		inner->Add(new wxStaticText(box, -1, _("Compiled with:")));
		wxString cc = CBuildInfo::GetCompiler();
		WrapText(this, cc, 300);
		inner->Add(new wxStaticText(box, -1, cc));

		wxString compilerFlags = CBuildInfo::GetCompilerFlags();
		if (!compilerFlags.empty()) {
			inner->Add(new wxStaticText(box, -1, _("Compiler flags:")));
			WrapText(this, compilerFlags, 300);
			inner->Add(new wxStaticText(box, -1, compilerFlags));
		}
	}

	{
		auto [box, inner] = lay.createStatBox(main, _("Linked against"), 2);
		inner->Add(new wxStaticText(box, -1, _("wxWidgets:")));
		inner->Add(new wxStaticText(box, -1, GetDependencyVersion(gui_lib_dependency::wxwidgets)));
		inner->Add(new wxStaticText(box, -1, _("GnuTLS:")));
		inner->Add(new wxStaticText(box, -1, GetDependencyVersion(lib_dependency::gnutls)));
		inner->Add(new wxStaticText(box, -1, _("SQLite:")));
		inner->Add(new wxStaticText(box, -1, GetDependencyVersion(gui_lib_dependency::sqlite)));
	}

	{
		auto [box, inner] = lay.createStatBox(main, _("System details"), 2);
		auto os = wxGetOsDescription();
		if (!os.empty()) {
			inner->Add(new wxStaticText(box, -1, _("Operating System:")));
			inner->Add(new wxStaticText(box, -1, os));
		}

		int major, minor;
		if (GetRealOsVersion(major, minor)) {
			inner->Add(new wxStaticText(box, -1, _("OS version:")));

			wxString osVersion = wxString::Format(_T("%d.%d"), major, minor);
			int fakeMajor, fakeMinor;
			if (wxGetOsVersion(&fakeMajor, &fakeMinor) != wxOS_UNKNOWN && (fakeMajor != major || fakeMinor != minor)) {
				osVersion += ' ';
				osVersion += wxString::Format(_("(app-compat is set to %d.%d)"), fakeMajor, fakeMinor);
			}
			inner->Add(new wxStaticText(box, -1, osVersion));
		}
#ifdef __WXMSW__
		inner->Add(new wxStaticText(box, -1, _("Platform:")));
		if (::wxIsPlatform64Bit()) {
			inner->Add(new wxStaticText(box, -1, _("64-bit system")));
		}
		else {
			inner->Add(new wxStaticText(box, -1, _("32-bit system")));
		}
#endif

		wxString cpuCaps = CBuildInfo::GetCPUCaps(' ');
		if (!cpuCaps.empty()) {
			inner->Add(new wxStaticText(box, -1, _("CPU features:")));
			WrapText(this, cpuCaps, 300);
			inner->Add(new wxStaticText(box, -1, cpuCaps));
		}

		inner->Add(new wxStaticText(box, -1, _("Settings directory:")));
		inner->Add(new wxStaticText(box, -1, COptions::Get()->GetOption(OPTION_DEFAULT_SETTINGSDIR)));
	}

	main->Add(new wxStaticLine(this), lay.grow);
	auto buttons = lay.createFlex(3);
	main->Add(buttons, lay.grow);

	auto copy = new wxButton(this, -1, _("&Copy to clipboard"));
	copy->Bind(wxEVT_BUTTON, [this](wxEvent const&){ OnCopy(); });
	buttons->Add(copy, lay.valign);
	buttons->AddStretchSpacer();
	buttons->AddGrowableCol(1);

	auto ok = new wxButton(this, -1, _("OK"));
	ok->Bind(wxEVT_BUTTON, [this](wxEvent const&){ EndModal(wxID_OK); });
	ok->SetDefault();
	ok->SetFocus();
	buttons->Add(ok, lay.valign);

	GetSizer()->Fit(this);
	GetSizer()->SetSizeHints(this);

	return true;
}

void CAboutDialog::OnCopy()
{
	wxString text = _T("FileZilla Client\n");
	text += _T("----------------\n\n");

	text += _T("Version:          ") + CBuildInfo::GetVersion();
	if (CBuildInfo::GetBuildType() == _T("nightly")) {
		text += _T("-nightly");
	}
	text += '\n';

	text += _T("\nBuild information:\n");

	wxString host = CBuildInfo::GetHostname();
	if (!host.empty()) {
		text += _T("  Compiled for:   ") + host + _T("\n");
	}

	wxString build = CBuildInfo::GetBuildSystem();
	if (!build.empty()) {
		text += _T("  Compiled on:    ") + build + _T("\n");
	}

	text += _T("  Build date:     ") + CBuildInfo::GetBuildDateString() + _T("\n");

	text += _T("  Compiled with:  ") + CBuildInfo::GetCompiler() + _T("\n");

	wxString compilerFlags = CBuildInfo::GetCompilerFlags();
	if (!compilerFlags.empty()) {
		text += _T("  Compiler flags: ") + compilerFlags + _T("\n");
	}

	text += _T("\nLinked against:\n");
	for (int i = 0; i < static_cast<int>(gui_lib_dependency::count); ++i) {
		text += wxString::Format(_T("  % -15s %s\n"),
			GetDependencyName(gui_lib_dependency(i)) + _T(":"),
			GetDependencyVersion(gui_lib_dependency(i)));
	}
	for (int i = 0; i < static_cast<int>(lib_dependency::count); ++i) {
		text += wxString::Format(_T("  % -15s %s\n"),
			GetDependencyName(lib_dependency(i)) + _T(":"),
			GetDependencyVersion(lib_dependency(i)));
	}

	text += _T("\nOperating system:\n");
	wxString os = wxGetOsDescription();
	if (!os.empty()) {
		text += _T("  Name:           ") + os + _T("\n");
	}

	int major, minor;
	if (GetRealOsVersion(major, minor)) {
		wxString version = wxString::Format(_T("%d.%d"), major, minor);
		int fakeMajor, fakeMinor;
		if (wxGetOsVersion(&fakeMajor, &fakeMinor) != wxOS_UNKNOWN && (fakeMajor != major || fakeMinor != minor)) {
			version += _T(" ");
			version += wxString::Format(_("(app-compat is set to %d.%d)"), fakeMajor, fakeMinor);
		}
		text += wxString::Format(_T("  Version:        %s\n"), version);
	}

#ifdef __WXMSW__
	if (::wxIsPlatform64Bit()) {
		text += _T("  Platform:       64-bit system\n");
	}
	else {
		text += _T("  Platform:       32-bit system\n");
	}
#endif

	wxString cpuCaps = CBuildInfo::GetCPUCaps(' ');
	if (!cpuCaps.empty()) {
		text += _T("  CPU features:   ") + cpuCaps + _T("\n");
	}

	text += _T("  Settings dir:   ") + COptions::Get()->GetOption(OPTION_DEFAULT_SETTINGSDIR) + _T("\n");

#ifdef __WXMSW__
	text.Replace(_T("\n"), _T("\r\n"));
#endif

	if (!wxTheClipboard->Open()) {
		wxMessageBoxEx(_("Could not open clipboard"), _("Could not copy data"), wxICON_EXCLAMATION);
		return;
	}

	wxTheClipboard->SetData(new wxTextDataObject(text));
	wxTheClipboard->Flush();
	wxTheClipboard->Close();
}
