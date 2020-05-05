#include <filezilla.h>
#include "listingcomparison.h"
#include "filter.h"
#include "Options.h"
#include "state.h"

CComparableListing::CComparableListing(wxWindow* pParent)
{
	m_pComparisonManager = 0;
	m_pParent = pParent;

	// Init backgrounds for directory comparison
	wxColour background = m_pParent->GetBackgroundColour();
	if (background.Red() + background.Green() + background.Blue() >= 384) {
		// Light background
		m_comparisonBackgrounds[0].SetBackgroundColour(wxColour(255, 128, 128));
		m_comparisonBackgrounds[1].SetBackgroundColour(wxColour(255, 255, 128));
		m_comparisonBackgrounds[2].SetBackgroundColour(wxColour(128, 255, 128));
	}
	else {
		// Light background
		m_comparisonBackgrounds[0].SetBackgroundColour(wxColour(192, 64, 64));
		m_comparisonBackgrounds[1].SetBackgroundColour(wxColour(192, 192, 64));
		m_comparisonBackgrounds[2].SetBackgroundColour(wxColour(64, 192, 64));
	}

	m_pOther = 0;
}

bool CComparableListing::IsComparing() const
{
	if (!m_pComparisonManager) {
		return false;
	}

	return m_pComparisonManager->IsComparing();
}

void CComparableListing::ExitComparisonMode()
{
	if (!m_pComparisonManager) {
		return;
	}

	m_pComparisonManager->ExitComparisonMode();
}

void CComparableListing::RefreshComparison()
{
	if (!m_pComparisonManager) {
		return;
	}

	if (!IsComparing()) {
		return;
	}

	if (!CanStartComparison() || !GetOther() || !GetOther()->CanStartComparison()) {
		return;
	}

	m_pComparisonManager->CompareListings();
}

bool CComparisonManager::CompareListings()
{
	if (!m_pLeft || !m_pRight) {
		return false;
	}

	CFilterManager filters;
	if (filters.HasActiveFilters() && !filters.HasSameLocalAndRemoteFilters()) {
		m_state.NotifyHandlers(STATECHANGE_COMPARISON);
		wxMessageBoxEx(_("Cannot compare directories, different filters for local and remote directories are enabled"), _("Directory comparison failed"), wxICON_EXCLAMATION);
		return false;
	}

	m_isComparing = true;
	m_pLeft->m_pComparisonManager = this;
	m_pRight->m_pComparisonManager = this;

	m_state.NotifyHandlers(STATECHANGE_COMPARISON);

	if (!m_pLeft->CanStartComparison() || !m_pRight->CanStartComparison()) {
		return true;
	}

	fz::duration const threshold = fz::duration::from_minutes( COptions::Get()->GetOptionVal(OPTION_COMPARISON_THRESHOLD) );

	m_pLeft->StartComparison();
	m_pRight->StartComparison();

	std::wstring localPath, remotePath;
	std::wstring_view localFile, remoteFile;
	bool localDir = false;
	bool remoteDir = false;
	int64_t localSize, remoteSize;
	fz::datetime localDate, remoteDate;

	const int dirSortMode = COptions::Get()->GetOptionVal(OPTION_FILELIST_DIRSORT);

	bool gotLocal = m_pLeft->get_next_file(localFile, localPath, localDir, localSize, localDate);
	bool gotRemote = m_pRight->get_next_file(remoteFile, remotePath, remoteDir, remoteSize, remoteDate);

	while (gotLocal && gotRemote) {
		int cmp = CompareFiles(dirSortMode, localPath, localFile, remotePath, remoteFile, localDir, remoteDir);
		if (!cmp) {
			if (!m_comparisonMode) {
				const CComparableListing::t_fileEntryFlags flag = (localDir || localSize == remoteSize) ? CComparableListing::normal : CComparableListing::different;

				if (!m_hideIdentical || flag != CComparableListing::normal || localFile == L"..") {
					m_pLeft->CompareAddFile(flag);
					m_pRight->CompareAddFile(flag);
				}
			}
			else {
				if (localDate.empty() || remoteDate.empty()) {
					if (!m_hideIdentical || !localDate.empty() || !remoteDate.empty() || localFile == L"..") {
						const CComparableListing::t_fileEntryFlags flag = CComparableListing::normal;
						m_pLeft->CompareAddFile(flag);
						m_pRight->CompareAddFile(flag);
					}
				}
				else {
					CComparableListing::t_fileEntryFlags localFlag, remoteFlag;

					int dateCmp = localDate.compare(remoteDate);
					if (dateCmp < 0) {
						localDate += threshold;
					}
					else if (dateCmp > 0 ) {
						remoteDate += threshold;
					}
					int adjustedDateCmp = localDate.compare(remoteDate);
					if (dateCmp && dateCmp == -adjustedDateCmp) {
						dateCmp = 0;
					}

					localFlag = CComparableListing::normal;
					remoteFlag = CComparableListing::normal;
					if (dateCmp < 0 ) {
						remoteFlag = CComparableListing::newer;
					}
					else if (dateCmp > 0) {
						localFlag = CComparableListing::newer;
					}
					if (!m_hideIdentical || localFlag != CComparableListing::normal || remoteFlag != CComparableListing::normal || localFile == L"..") {
						m_pLeft->CompareAddFile(localFlag);
						m_pRight->CompareAddFile(remoteFlag);
					}
				}
			}
			gotLocal = m_pLeft->get_next_file(localFile, localPath, localDir, localSize, localDate);
			gotRemote = m_pRight->get_next_file(remoteFile, remotePath, remoteDir, remoteSize, remoteDate);
			continue;
		}

		if (cmp < 0) {
			m_pLeft->CompareAddFile(CComparableListing::lonely);
			m_pRight->CompareAddFile(CComparableListing::fill);
			gotLocal = m_pLeft->get_next_file(localFile, localPath, localDir, localSize, localDate);
		}
		else {
			m_pLeft->CompareAddFile(CComparableListing::fill);
			m_pRight->CompareAddFile(CComparableListing::lonely);
			gotRemote = m_pRight->get_next_file(remoteFile, remotePath, remoteDir, remoteSize, remoteDate);
		}
	}
	while (gotLocal) {
		m_pLeft->CompareAddFile(CComparableListing::lonely);
		m_pRight->CompareAddFile(CComparableListing::fill);
		gotLocal = m_pLeft->get_next_file(localFile, localPath, localDir, localSize, localDate);
	}
	while (gotRemote) {
		m_pLeft->CompareAddFile(CComparableListing::fill);
		m_pRight->CompareAddFile(CComparableListing::lonely);
		gotRemote = m_pRight->get_next_file(remoteFile, remotePath, remoteDir, remoteSize, remoteDate);
	}

	m_pRight->FinishComparison();
	m_pLeft->FinishComparison();

	return true;
}

int CComparisonManager::CompareFiles(const int dirSortMode, std::wstring_view const& local_path, std::wstring_view const& local, std::wstring_view const& remote_path, std::wstring_view const& remote, bool localDir, bool remoteDir)
{
	switch (dirSortMode)
	{
	default:
		if (localDir) {
			if (!remoteDir) {
				return -1;
			}
		}
		else if (remoteDir) {
			return 1;
		}
		break;
	case 2:
		// Inline
		break;
	}

#ifdef __WXMSW__
	auto cmp = fz::stricmp(local, remote);
	if (!cmp) {
		return fz::stricmp(local_path, remote_path);
	}
	return cmp;
#else
	auto cmp = local.compare(remote);
	if (!cmp) {
		return local_path.compare(remote_path);
	}
	return cmp;
#endif
}

CComparisonManager::CComparisonManager(CState& state)
	: m_state(state)
{
	m_comparisonMode = COptions::Get()->GetOptionVal(OPTION_COMPARISONMODE);
	m_hideIdentical = COptions::Get()->GetOptionVal(OPTION_COMPARE_HIDEIDENTICAL) != 0;
}

void CComparisonManager::SetListings(CComparableListing* pLeft, CComparableListing* pRight)
{
	wxASSERT((pLeft && pRight) || (!pLeft && !pRight));

	if (IsComparing()) {
		ExitComparisonMode();
	}

	if (m_pLeft) {
		m_pLeft->SetOther(0);
	}
	if (m_pRight) {
		m_pRight->SetOther(0);
	}

	m_pLeft = pLeft;
	m_pRight = pRight;

	if (m_pLeft) {
		m_pLeft->SetOther(m_pRight);
	}
	if (m_pRight) {
		m_pRight->SetOther(m_pLeft);
	}
}

void CComparisonManager::ExitComparisonMode()
{
	if (!IsComparing()) {
		return;
	}

	m_isComparing = false;
	if (m_pLeft) {
		m_pLeft->OnExitComparisonMode();
	}
	if (m_pRight) {
		m_pRight->OnExitComparisonMode();
	}

	m_state.NotifyHandlers(STATECHANGE_COMPARISON);
}
