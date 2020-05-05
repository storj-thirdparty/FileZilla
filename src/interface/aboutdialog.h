#ifndef FILEZILLA_INTERFACE_ABOUTDIALOG_HEADER
#define FILEZILLA_INTERFACE_ABOUTDIALOG_HEADER

#include "dialogex.h"

class CAboutDialog final : public wxDialogEx
{
public:
	CAboutDialog() = default;

	bool Create(wxWindow* parent);

protected:
	void OnCopy();
};

#endif
