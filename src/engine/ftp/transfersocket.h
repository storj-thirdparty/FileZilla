#ifndef FILEZILLA_ENGINE_FTP_TRANSFERSOCKET_HEADER
#define FILEZILLA_ENGINE_FTP_TRANSFERSOCKET_HEADER

#include "iothread.h"
#include "controlsocket.h"

class CFileZillaEnginePrivate;
class CFtpControlSocket;
class CDirectoryListingParser;

enum class TransferMode
{
	list,
	upload,
	download,
	resumetest
};

class CIOThread;

namespace fz {
class tls_layer;
}

class CTransferSocket final : public fz::event_handler
{
public:
	CTransferSocket(CFileZillaEnginePrivate & engine, CFtpControlSocket & controlSocket, TransferMode transferMode);
	virtual ~CTransferSocket();

	std::wstring SetupActiveTransfer(std::string const& ip);
	bool SetupPassiveTransfer(std::wstring const& host, int port);

	void SetActive();

	CDirectoryListingParser *m_pDirectoryListingParser{};

	bool m_binaryMode{true};

	TransferEndReason GetTransferEndreason() const { return m_transferEndReason; }

	void SetIOThread(CIOThread* ioThread) { ioThread_ = ioThread; }

protected:
	bool CheckGetNextWriteBuffer();
	bool CheckGetNextReadBuffer();
	void FinalizeWrite();

	void TransferEnd(TransferEndReason reason);

	bool InitLayers(bool active);

	void ResetSocket();

	void OnSocketEvent(fz::socket_event_source* source, fz::socket_event_flag t, int error);
	void OnConnect();
	void OnAccept(int error);
	void OnReceive();
	void OnSend();
	void OnSocketError(int error);
	void OnTimer(fz::timer_id);

	// Create a socket server
	std::unique_ptr<fz::listen_socket> CreateSocketServer();
	std::unique_ptr<fz::listen_socket> CreateSocketServer(int port);

	void SetSocketBufferSizes(fz::socket_base & socket);

	virtual void operator()(fz::event_base const& ev);
	void OnIOThreadEvent();

	// Will be set only while creating active mode connections
	std::unique_ptr<fz::listen_socket> socketServer_;

	CFileZillaEnginePrivate & engine_;
	CFtpControlSocket & controlSocket_;

	bool m_bActive{};
	TransferEndReason m_transferEndReason{TransferEndReason::none};

	TransferMode const m_transferMode;

	char *m_pTransferBuffer{};
	int m_transferBufferLen{};

	bool m_postponedReceive{};
	bool m_postponedSend{};
	void TriggerPostponedEvents();

	std::unique_ptr<fz::socket> socket_;
	std::unique_ptr<fz::rate_limited_layer> ratelimit_layer_;
	std::unique_ptr<CProxySocket> proxy_layer_;
	std::unique_ptr<fz::tls_layer> tls_layer_;

	fz::socket_layer* active_layer_{};


	// Needed for the madeProgress field in CTransferStatus
	// Initially 0, 2 if made progress
	// On uploads, 1 after first WSAE_WOULDBLOCK
	int m_madeProgress{};

	CIOThread* ioThread_{};
};

#endif
