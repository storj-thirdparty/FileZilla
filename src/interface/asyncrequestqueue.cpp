#include <filezilla.h>

#include "asyncrequestqueue.h"
#include "defaultfileexistsdlg.h"
#include "fileexistsdlg.h"
#include "loginmanager.h"
#include "Mainfrm.h"
#include "Options.h"
#include "queue.h"
#include "verifycertdialog.h"
#include "verifyhostkeydialog.h"

#include <libfilezilla/translate.hpp>

DECLARE_EVENT_TYPE(fzEVT_PROCESSASYNCREQUESTQUEUE, -1)
DEFINE_EVENT_TYPE(fzEVT_PROCESSASYNCREQUESTQUEUE)

BEGIN_EVENT_TABLE(CAsyncRequestQueue, wxEvtHandler)
EVT_COMMAND(wxID_ANY, fzEVT_PROCESSASYNCREQUESTQUEUE, CAsyncRequestQueue::OnProcessQueue)
EVT_TIMER(wxID_ANY, CAsyncRequestQueue::OnTimer)
END_EVENT_TABLE()

CAsyncRequestQueue::CAsyncRequestQueue(wxTopLevelWindow *parent)
	: parent_(parent)
	, certStore_(std::make_unique<CertStore>())
{
	CContextManager::Get()->RegisterHandler(this, STATECHANGE_REMOVECONTEXT, false);
	m_timer.SetOwner(this);
}

CAsyncRequestQueue::~CAsyncRequestQueue()
{
	CContextManager::Get()->UnregisterHandler(this, STATECHANGE_REMOVECONTEXT);
}

bool CAsyncRequestQueue::ProcessDefaults(CFileZillaEngine *pEngine, std::unique_ptr<CAsyncRequestNotification> & pNotification)
{
	// Process notifications, see if we have defaults not requirering user interaction.
	switch (pNotification->GetRequestID())
	{
	case reqId_fileexists:
		{
			CFileExistsNotification *pFileExistsNotification = static_cast<CFileExistsNotification *>(pNotification.get());

			// Get the action, go up the hierarchy till one is found
			CFileExistsNotification::OverwriteAction action = pFileExistsNotification->overwriteAction;
			if (action == CFileExistsNotification::unknown) {
				action = CDefaultFileExistsDlg::GetDefault(pFileExistsNotification->download);
			}
			if (action == CFileExistsNotification::unknown) {
				int option = COptions::Get()->GetOptionVal(pFileExistsNotification->download ? OPTION_FILEEXISTS_DOWNLOAD : OPTION_FILEEXISTS_UPLOAD);
				if (option < CFileExistsNotification::unknown || option >= CFileExistsNotification::ACTION_COUNT) {
					action = CFileExistsNotification::unknown;
				}
				else {
					action = static_cast<CFileExistsNotification::OverwriteAction>(option);
				}
			}

			// Ask and rename options require user interaction
			if (action == CFileExistsNotification::unknown || action == CFileExistsNotification::ask || action == CFileExistsNotification::rename) {
				break;
			}

			if (action == CFileExistsNotification::resume && pFileExistsNotification->ascii) {
				// Check if resuming ascii files is allowed
				if (!COptions::Get()->GetOptionVal(OPTION_ASCIIRESUME)) {
					// Overwrite instead
					action = CFileExistsNotification::overwrite;
				}
			}

			pFileExistsNotification->overwriteAction = action;

			pEngine->SetAsyncRequestReply(std::move(pNotification));

			return true;
		}
	case reqId_hostkey:
	case reqId_hostkeyChanged:
		{
			auto & hostKeyNotification = static_cast<CHostKeyNotification&>(*pNotification.get());

			if (!CVerifyHostkeyDialog::IsTrusted(hostKeyNotification)) {
				break;
			}

			hostKeyNotification.m_trust = true;
			hostKeyNotification.m_alwaysTrust = false;

			pEngine->SetAsyncRequestReply(std::move(pNotification));

			return true;
		}
	case reqId_certificate:
		{
			auto & certNotification = static_cast<CCertificateNotification&>(*pNotification.get());

			if (!certStore_->IsTrusted(certNotification.info_)) {
				break;
			}

			certNotification.trusted_ = true;
			pEngine->SetAsyncRequestReply(std::move(pNotification));

			return true;
		}
		break;
	case reqId_insecure_connection:
		{
			auto & insecureNotification = static_cast<CInsecureConnectionNotification&>(*pNotification.get());
			if (!certStore_->IsInsecure(fz::to_utf8(insecureNotification.server_.GetHost()), insecureNotification.server_.GetPort())) {
				break;
			}

			insecureNotification.allow_ = true;
			pEngine->SetAsyncRequestReply(std::move(pNotification));

			return true;
		}
	default:
		break;
	}

	return false;
}

bool CAsyncRequestQueue::AddRequest(CFileZillaEngine *pEngine, std::unique_ptr<CAsyncRequestNotification> && pNotification)
{
	ClearPending(pEngine);

	if (ProcessDefaults(pEngine, pNotification)) {
		return false;
	}

	m_requestList.emplace_back(pEngine, std::move(pNotification));

	if (m_requestList.size() == 1) {
		QueueEvent(new wxCommandEvent(fzEVT_PROCESSASYNCREQUESTQUEUE));
	}

	return true;
}

bool CAsyncRequestQueue::ProcessNextRequest()
{
	if (m_requestList.empty()) {
		return true;
	}

	t_queueEntry &entry = m_requestList.front();

	if (!entry.pEngine || !entry.pEngine->IsPendingAsyncRequestReply(entry.pNotification)) {
		m_requestList.pop_front();
		return true;
	}

	if (entry.pNotification->GetRequestID() == reqId_fileexists) {
		if (!ProcessFileExistsNotification(entry)) {
			return false;
		}
	}
	else if (entry.pNotification->GetRequestID() == reqId_interactiveLogin) {
		auto & notification = static_cast<CInteractiveLoginNotification&>(*entry.pNotification.get());

		if (notification.IsRepeated()) {
			CLoginManager::Get().CachedPasswordFailed(notification.server, notification.GetChallenge());
		}
		bool canRemember = notification.GetType() == CInteractiveLoginNotification::keyfile;

		Site site(notification.server, notification.handle_, notification.credentials);
		if (CLoginManager::Get().GetPassword(site, true, notification.GetChallenge(), canRemember)) {
			notification.credentials = site.credentials;
			notification.passwordSet = true;
		}
		else {
			// Retry with prompt

			if (!CheckWindowState()) {
				return false;
			}

			if (CLoginManager::Get().GetPassword(site, false, notification.GetChallenge(), canRemember)) {
				notification.credentials = site.credentials;
				notification.passwordSet = true;
			}
		}

		SendReply(entry);
	}
	else if (entry.pNotification->GetRequestID() == reqId_hostkey || entry.pNotification->GetRequestID() == reqId_hostkeyChanged) {
		if (!CheckWindowState()) {
			return false;
		}

		auto & notification = static_cast<CHostKeyNotification&>(*entry.pNotification.get());

		if (CVerifyHostkeyDialog::IsTrusted(notification)) {
			notification.m_trust = true;
			notification.m_alwaysTrust = false;
		}
		else {
			CVerifyHostkeyDialog::ShowVerificationDialog(parent_, notification);
		}

		SendReply(entry);
	}
	else if (entry.pNotification->GetRequestID() == reqId_certificate) {
		if (!CheckWindowState()) {
			return false;
		}

		if (certStore_) {
			auto & notification = static_cast<CCertificateNotification&>(*entry.pNotification.get());
			CVerifyCertDialog::ShowVerificationDialog(*certStore_, notification);
		}

		SendReply(entry);
	}
	else if (entry.pNotification->GetRequestID() == reqId_insecure_connection) {
		if (!CheckWindowState()) {
			return false;
		}

		auto & notification = static_cast<CInsecureConnectionNotification&>(*entry.pNotification.get());

		ConfirmInsecureConection(parent_, *certStore_.get(), notification);

		SendReply(entry);
	}
	else {
		SendReply(entry);
	}

	m_requestList.pop_front();

	return true;
}

bool CAsyncRequestQueue::ProcessFileExistsNotification(t_queueEntry &entry)
{
	auto & notification = static_cast<CFileExistsNotification&>(*entry.pNotification.get());

	// Get the action, go up the hierarchy till one is found
	CFileExistsNotification::OverwriteAction action = notification.overwriteAction;
	if (action == CFileExistsNotification::unknown) {
		action = CDefaultFileExistsDlg::GetDefault(notification.download);
	}
	if (action == CFileExistsNotification::unknown) {
		int option = COptions::Get()->GetOptionVal(notification.download ? OPTION_FILEEXISTS_DOWNLOAD : OPTION_FILEEXISTS_UPLOAD);
		if (option <= CFileExistsNotification::unknown || option >= CFileExistsNotification::ACTION_COUNT) {
			action = CFileExistsNotification::ask;
		}
		else {
			action = static_cast<CFileExistsNotification::OverwriteAction>(option);
		}
	}

	if (action == CFileExistsNotification::ask) {
		if (!CheckWindowState()) {
			return false;
		}

		CFileExistsDlg dlg(&notification);
		dlg.Create(parent_);
		int res = dlg.ShowModal();

		if (res == wxID_OK) {
			action = dlg.GetAction();

			bool directionOnly, queueOnly;
			if (dlg.Always(directionOnly, queueOnly)) {
				if (!queueOnly) {
					if (notification.download || !directionOnly) {
						CDefaultFileExistsDlg::SetDefault(true, action);
					}

					if (!notification.download || !directionOnly) {
						CDefaultFileExistsDlg::SetDefault(false, action);
					}
				}
				else {
					// For the notifications already in the request queue, we have to set the queue action directly
					for (auto iter = ++m_requestList.begin(); iter != m_requestList.end(); ++iter) {
						if (!iter->pNotification || iter->pNotification->GetRequestID() != reqId_fileexists) {
							continue;
						}
						auto & p = static_cast<CFileExistsNotification&>(*iter->pNotification.get());

						if (!directionOnly || notification.download == p.download) {
							p.overwriteAction = CFileExistsNotification::OverwriteAction(action);
						}
					}

					TransferDirection direction;
					if (directionOnly) {
						if (notification.download) {
							direction = TransferDirection::download;
						}
						else {
							direction = TransferDirection::upload;
						}
					}
					else {
						direction = TransferDirection::both;
					}

					if (m_pQueueView) {
						m_pQueueView->SetDefaultFileExistsAction(action, direction);
					}
				}
			}
		}
		else {
			action = CFileExistsNotification::skip;
		}
	}

	if (action == CFileExistsNotification::unknown || action == CFileExistsNotification::ask) {
		action = CFileExistsNotification::skip;
	}

	if (action == CFileExistsNotification::resume && notification.ascii) {
		// Check if resuming ascii files is allowed
		if (!COptions::Get()->GetOptionVal(OPTION_ASCIIRESUME)) {
			// Overwrite instead
			action = CFileExistsNotification::overwrite;
		}
	}

	switch (action)
	{
		case CFileExistsNotification::rename:
		{
			if (!CheckWindowState()) {
				return false;
			}

			wxString msg;
			std::wstring defaultName;
			if (notification.download) {
				msg.Printf(_("The file %s already exists.\nPlease enter a new name:"), notification.localFile);
				CLocalPath fn(notification.localFile, &defaultName);
				if (fn.empty() || defaultName.empty()) {
					defaultName = fztranslate("new name");
				}
			}
			else {
				wxString fullName = notification.remotePath.FormatFilename(notification.remoteFile);
				msg.Printf(_("The file %s already exists.\nPlease enter a new name:"), fullName);
				defaultName = notification.remoteFile;
			}
			wxTextEntryDialog dlg(parent_, msg, _("Rename file"), defaultName);

			// Repeat until user cancels or enters a new name
			for (;;) {
				int res = dlg.ShowModal();
				if (res == wxID_OK) {
					if (dlg.GetValue().empty()) {
						continue; // Disallow empty names
					}
					if (dlg.GetValue() == defaultName) {
						wxMessageDialog dlg2(parent_, _("You did not enter a new name for the file. Overwrite the file instead?"), _("Filename unchanged"),
							wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION | wxCANCEL);
						int res2 = dlg2.ShowModal();

						if (res2 == wxID_CANCEL) {
							notification.overwriteAction = CFileExistsNotification::skip;
						}
						else if (res2 == wxID_NO) {
							continue;
						}
						else {
							notification.overwriteAction = CFileExistsNotification::skip;
						}
					}
					else {
						notification.overwriteAction = CFileExistsNotification::rename;
						notification.newName = dlg.GetValue().ToStdWstring();

						// If request got processed successfully, notify queue about filename change
						if (SendReply(entry) && m_pQueueView) {
							m_pQueueView->RenameFileInTransfer(entry.pEngine, dlg.GetValue().ToStdWstring(), notification.download);
						}
						return true;
					}
				}
				else {
					notification.overwriteAction = CFileExistsNotification::skip;
				}
				break;
			}
		}
		break;
		default:
			notification.overwriteAction = action;
			break;
	}

	SendReply(entry);
	return true;
}

void CAsyncRequestQueue::ClearPending(CFileZillaEngine const* const pEngine)
{
	if (!pEngine) {
		return;
	}

	for (auto iter = m_requestList.begin(); iter != m_requestList.end();) {
		if (iter->pEngine == pEngine) {
			if (m_inside_request && iter == m_requestList.begin()) {
				// Can't remove this entry as it displays a dialog at this moment, holding a reference.
				iter->pEngine = nullptr;
				++iter;
			}
			else {
				// Even though there _should_ be at most a single request per engine, in rare circumstances there may be additional requests
				iter = m_requestList.erase(iter);
			}
		}
		else {
			++iter;
		}
	}
}

void CAsyncRequestQueue::RecheckDefaults()
{
	std::list<t_queueEntry>::iterator it = m_requestList.begin();
	if (m_inside_request) {
		++it;
	}
	while (it != m_requestList.end()) {
		if (ProcessDefaults(it->pEngine, it->pNotification)) {
			it = m_requestList.erase(it);
		}
		else {
			++it;
		}
	}
}

void CAsyncRequestQueue::SetQueue(CQueueView *pQueue)
{
	m_pQueueView = pQueue;
}

void CAsyncRequestQueue::OnProcessQueue(wxCommandEvent &)
{
	if (m_inside_request) {
		return;
	}

	m_inside_request = true;
	bool success = ProcessNextRequest();
	m_inside_request = false;

	if (success) {
		RecheckDefaults();

		if (!m_requestList.empty()) {
			QueueEvent(new wxCommandEvent(fzEVT_PROCESSASYNCREQUESTQUEUE));
		}
	}
}

void CAsyncRequestQueue::TriggerProcessing()
{
	if (m_inside_request || m_requestList.empty()) {
		return;
	}

	QueueEvent(new wxCommandEvent(fzEVT_PROCESSASYNCREQUESTQUEUE));
}

bool CAsyncRequestQueue::CheckWindowState()
{
	m_timer.Stop();
	if (!wxDialogEx::CanShowPopupDialog(parent_)) {
		m_timer.Start(100, true);
		return false;
	}

#ifndef __WXMAC__
	if (parent_->IsIconized()) {
#ifndef __WXGTK__
		parent_->Show();
		parent_->Iconize(true);
		parent_->RequestUserAttention();
#endif
		return false;
	}

	wxWindow* pFocus = parent_->FindFocus();
	while (pFocus && pFocus != parent_) {
		pFocus = pFocus->GetParent();
	}
	if (!pFocus) {
		parent_->RequestUserAttention();
	}
#endif

	return true;
}

void CAsyncRequestQueue::OnTimer(wxTimerEvent&)
{
	TriggerProcessing();
}

bool CAsyncRequestQueue::SendReply(t_queueEntry& entry)
{
	if (!entry.pEngine) {
		return false;
	}

	return entry.pEngine->SetAsyncRequestReply(std::move(entry.pNotification));
}

void CAsyncRequestQueue::OnStateChange(CState* pState, t_statechange_notifications, std::wstring const&, const void*)
{
	if (pState) {
		ClearPending(pState->m_pEngine);
	}
}
