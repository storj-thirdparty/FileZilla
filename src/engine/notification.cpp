#include <filezilla.h>

CDirectoryListingNotification::CDirectoryListingNotification(CServerPath const& path, bool const primary, bool const failed)
	: primary_(primary), m_failed(failed), m_path(path)
{
}

RequestId CFileExistsNotification::GetRequestID() const
{
	return reqId_fileexists;
}

CInteractiveLoginNotification::CInteractiveLoginNotification(type t, std::wstring const& challenge, bool repeated)
	: m_challenge(challenge)
	, m_type(t)
	, m_repeated(repeated)
{
}

RequestId CInteractiveLoginNotification::GetRequestID() const
{
	return reqId_interactiveLogin;
}

CActiveNotification::CActiveNotification(int direction)
	: m_direction(direction)
{
}

CTransferStatusNotification::CTransferStatusNotification(CTransferStatus const& status)
	: status_(status)
{
}

CTransferStatus const& CTransferStatusNotification::GetStatus() const
{
	return status_;
}

CHostKeyNotification::CHostKeyNotification(std::wstring const& host, int port, CSftpEncryptionDetails const& details, bool changed)
	: CSftpEncryptionDetails(details)
	, m_host(host)
	, m_port(port)
	, m_changed(changed)
{
}

RequestId CHostKeyNotification::GetRequestID() const
{
	return m_changed ? reqId_hostkeyChanged : reqId_hostkey;
}

std::wstring CHostKeyNotification::GetHost() const
{
	return m_host;
}

int CHostKeyNotification::GetPort() const
{
	return m_port;
}

CDataNotification::CDataNotification(char* pData, size_t len)
	: m_pData(pData), m_len(len)
{
}

CDataNotification::~CDataNotification()
{
	delete [] m_pData;
}

char* CDataNotification::Detach(size_t& len)
{
	len = m_len;
	char* pData = m_pData;
	m_pData = nullptr;
	return pData;
}

CCertificateNotification::CCertificateNotification(fz::tls_session_info&& info)
	: info_(info)
{
}

CInsecureConnectionNotification::CInsecureConnectionNotification(CServer const& server)
	: server_(server)
{
}
