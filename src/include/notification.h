#ifndef FILEZILLA_ENGINE_NOTIFICATION_HEADER
#define FILEZILLA_ENGINE_NOTIFICATION_HEADER

// Notification overview
// ---------------------

// To inform the application about what's happening, the engine sends
// some notifications to the application.
// The handler needs to derive from EngineNotificationHandler and implement
// the OnEngineEvent method which takes the engine as parameter.
// Whenever you get a notification event,
// CFileZillaEngine::GetNextNotification has to be called until it returns 0,
// or you will lose important notifications or your memory will fill with
// pending notifications.
//
// Note: It may be called from a worker thread.

// A special class of notifications are the asynchronous requests. These
// requests have to be answered. Once processed, call
// CFileZillaEngine::SetAsyncRequestReply to continue the current operation.

#include "commands.h"
#include "local_path.h"
#include "server.h"

#include <logging.h>

#include <libfilezilla/time.hpp>
#include <libfilezilla/tls_info.hpp>

class CFileZillaEngine;

class EngineNotificationHandler
{
public:
	virtual ~EngineNotificationHandler() {}

	virtual void OnEngineEvent(CFileZillaEngine* engine) = 0;
};

enum NotificationId
{
	nId_logmsg,				// notification about new messages for the message log
	nId_operation,			// operation reply codes
	nId_connection,			// connection information: connects, disconnects, timeouts etc..
	nId_transferstatus,		// transfer information: bytes transferred, transfer speed and such
	nId_listing,			// directory listings
	nId_asyncrequest,		// asynchronous request
	nId_active,				// sent if data gets either received or sent
	nId_data,				// for memory downloads, indicates that new data is available.
	nId_sftp_encryption,	// information about key exchange, encryption algorithms and so on for SFTP
	nId_local_dir_created,	// local directory has been created
	nId_serverchange		// With some protocols, actual server identity isn't known until after logon
};

// Async request IDs
enum RequestId
{
	reqId_fileexists,         // Target file already exists, awaiting further instructions
	reqId_interactiveLogin,   // gives a challenge prompt for a password
	reqId_hostkey,            // used only by SSH/SFTP to indicate new host key
	reqId_hostkeyChanged,     // used only by SSH/SFTP to indicate changed host key
	reqId_certificate,        // sent after a successful TLS handshake to allow certificate
	                          // validation.
	reqId_insecure_connection // If using opportunistic FTP over TLS, or a completely
	                          // unprotected protocol ask user whether he really wants
	                          // to use a plaintext connection.
};

class CNotification
{
public:
	virtual ~CNotification() = default;
	virtual NotificationId GetID() const = 0;

protected:
	CNotification() = default;
	CNotification(CNotification const&) = default;
	CNotification& operator=(CNotification const&) = default;
};

template<NotificationId id>
class CNotificationHelper : public CNotification
{
public:
	virtual NotificationId GetID() const final { return id; }

protected:
	CNotificationHelper<id>() = default;
	CNotificationHelper<id>(CNotificationHelper<id> const&) = default;
	CNotificationHelper<id>& operator=(CNotificationHelper<id> const&) = default;
};

class CLogmsgNotification final : public CNotificationHelper<nId_logmsg>
{
public:
	explicit CLogmsgNotification(logmsg::type t)
		: msgType(t)
	{}

	template<typename String>
	CLogmsgNotification(logmsg::type t, String && m, fz::datetime const& time)
		: msg(std::forward<String>(m))
		, time_(time)
		, msgType(t)
	{
	}

	std::wstring msg;
	fz::datetime time_;
	logmsg::type msgType{logmsg::status}; // Type of message, see logging.h for details
};

// If CFileZillaEngine does return with FZ_REPLY_WOULDBLOCK, you will receive
// a nId_operation notification once the operation ends.
class COperationNotification final : public CNotificationHelper<nId_operation>
{
public:
	int nReplyCode{};
	Command commandId{Command::none};
};

// You get this type of notification everytime a directory listing has been
// requested explicitely or when a directory listing was retrieved implicitely
// during another operation, e.g. file transfers.
//
// Primary notifications are those resulting from a CListCommand, other ones
// can happen spontanously through other actions.
class CDirectoryListing;
class CDirectoryListingNotification final : public CNotificationHelper<nId_listing>
{
public:
	explicit CDirectoryListingNotification(CServerPath const& path, bool const primary, bool const failed = false);
	bool Primary() const { return primary_; }
	bool Failed() const { return m_failed; }
	const CServerPath GetPath() const { return m_path; }

protected:
	bool const primary_{};
	bool m_failed{};
	CServerPath m_path;
};

class CAsyncRequestNotification : public CNotificationHelper<nId_asyncrequest>
{
public:
	virtual RequestId GetRequestID() const = 0;
	unsigned int requestNumber{}; // Do never change this

protected:
	CAsyncRequestNotification() = default;
	CAsyncRequestNotification(CAsyncRequestNotification const&) = default;
	CAsyncRequestNotification& operator=(CAsyncRequestNotification const&) = default;
};

class CFileExistsNotification final : public CAsyncRequestNotification
{
public:
	virtual RequestId GetRequestID() const;

	bool download{};

	std::wstring localFile;
	int64_t localSize{-1};
	fz::datetime localTime;

	std::wstring remoteFile;
	CServerPath remotePath;
	int64_t remoteSize{-1};
	fz::datetime remoteTime;

	bool ascii{};

	bool canResume{};

	// overwriteAction will be set by the request handler
	enum OverwriteAction : signed char
	{
		unknown = -1,
		ask,
		overwrite,
		overwriteNewer,	// Overwrite if source file is newer than target file
		overwriteSize,	// Overwrite if source file is is different in size than target file
		overwriteSizeOrNewer,	// Overwrite if source file is different in size or newer than target file
		resume, // Overwrites if cannot be resumed
		rename,
		skip,

		ACTION_COUNT
	};

	// Set overwriteAction to the desired action
	OverwriteAction overwriteAction{unknown};

	// Set to new filename if overwriteAction is rename. Might trigger further
	// file exists notifications if new target file exists as well.
	std::wstring newName;
};

class CInteractiveLoginNotification final : public CAsyncRequestNotification
{
public:
	enum type {
		interactive,
		keyfile
	};

	CInteractiveLoginNotification(type t, std::wstring const& challenge, bool repeated);
	virtual RequestId GetRequestID() const;

	// Set to true if you have set a password
	bool passwordSet{};

	CServer server;
	ServerHandle handle_;
	Credentials credentials;

	std::wstring const& GetChallenge() const { return m_challenge; }

	type GetType() const { return m_type; }

	bool IsRepeated() const { return m_repeated; }

protected:
	// Password prompt string as given by the server
	std::wstring const m_challenge;

	type const m_type;

	bool const m_repeated;
};

// Indicate network action.
class CActiveNotification final : public CNotificationHelper<nId_active>
{
public:
	explicit CActiveNotification(int direction);

	int GetDirection() const { return m_direction; }
protected:
	const int m_direction;
};

class CTransferStatus final
{
public:
	CTransferStatus() {}
	CTransferStatus(int64_t total, int64_t start, bool l)
		: totalSize(total)
		, startOffset(start)
		, currentOffset(start)
		, list(l)
	{}

	fz::datetime started;
	int64_t totalSize{-1};		// Total size of the file to transfer, -1 if unknown
	int64_t startOffset{-1};
	int64_t currentOffset{-1};

	void clear() { startOffset = -1; }
	bool empty() const { return startOffset < 0; }

	explicit operator bool() const { return !empty(); }

	// True on download notifications iff currentOffset != startOffset.
	// True on FTP upload notifications iff currentOffset != startOffset
	// AND after the first accepted data after the first EWOULDBLOCK.
	// SFTP uploads: Set to true if currentOffset >= startOffset + 65536.
	bool madeProgress{};

	bool list{};
};

class CTransferStatusNotification final : public CNotificationHelper<nId_transferstatus>
{
public:
	CTransferStatusNotification() {}
	CTransferStatusNotification(CTransferStatus const& status);

	CTransferStatus const& GetStatus() const;

protected:
	CTransferStatus const status_;
};

class CSftpEncryptionDetails
{
public:
	virtual ~CSftpEncryptionDetails() = default;

	std::wstring hostKeyAlgorithm;
	std::wstring hostKeyFingerprintMD5;
	std::wstring hostKeyFingerprintSHA256;
	std::wstring kexAlgorithm;
	std::wstring kexHash;
	std::wstring kexCurve;
	std::wstring cipherClientToServer;
	std::wstring cipherServerToClient;
	std::wstring macClientToServer;
	std::wstring macServerToClient;
};

// Notification about new or changed hostkeys, only used by SSH/SFTP transfers.
// GetRequestID() returns either reqId_hostkey or reqId_hostkeyChanged
class CHostKeyNotification final : public CAsyncRequestNotification, public CSftpEncryptionDetails
{
public:
	CHostKeyNotification(std::wstring const& host, int port, CSftpEncryptionDetails const& details, bool changed = false);

	virtual RequestId GetRequestID() const;

	std::wstring GetHost() const;
	int GetPort() const;

	// Set to true if you trust the server
	bool m_trust{};

	// If m_truest is true, set this to true to always trust this server
	// in future.
	bool m_alwaysTrust{};

protected:

	const std::wstring m_host;
	const int m_port;
	const bool m_changed;
};

class CDataNotification final : public CNotificationHelper<nId_data>
{
public:
	CDataNotification(char* pData, size_t len);
	virtual ~CDataNotification();

	CDataNotification(CDataNotification const&) = delete;
	CDataNotification& operator=(CDataNotification const&) = delete;

	char* Detach(size_t& len);

	char const* data() const { return m_pData; }
	size_t size() const { return m_len; }

protected:
	char* m_pData;
	size_t m_len;
};

class CCertificateNotification final : public CAsyncRequestNotification
{
public:
	CCertificateNotification(fz::tls_session_info && info);
	virtual RequestId GetRequestID() const { return reqId_certificate; }

	fz::tls_session_info info_;

	bool trusted_{};

private:
};

class CSftpEncryptionNotification final : public CNotificationHelper<nId_sftp_encryption>, public CSftpEncryptionDetails
{
};

class CLocalDirCreatedNotification final : public CNotificationHelper<nId_local_dir_created>
{
public:
	CLocalPath dir;
};

class CInsecureConnectionNotification final : public CAsyncRequestNotification
{
public:
	CInsecureConnectionNotification(CServer const& server);
	virtual RequestId GetRequestID() const { return reqId_insecure_connection; }

	CServer const server_;
	bool allow_{};
};

class ServerChangeNotification final : public CNotificationHelper<nId_serverchange>
{
public:
	ServerChangeNotification() = default;

	explicit ServerChangeNotification(CServer const& server)
	    : newServer_(server)
	{}

	CServer newServer_;
};

#endif
