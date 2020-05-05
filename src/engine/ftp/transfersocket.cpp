#include <filezilla.h>
#include "directorylistingparser.h"
#include "engineprivate.h"
#include "ftp/ftpcontrolsocket.h"
#include "iothread.h"
#include "optionsbase.h"
#include "proxy.h"
#include "servercapabilities.h"
#include "transfersocket.h"

#include <libfilezilla/rate_limited_layer.hpp>
#include <libfilezilla/tls_layer.hpp>
#include <libfilezilla/util.hpp>

#include <assert.h>

CTransferSocket::CTransferSocket(CFileZillaEnginePrivate & engine, CFtpControlSocket & controlSocket, TransferMode transferMode)
: fz::event_handler(controlSocket.event_loop_)
, engine_(engine)
, controlSocket_(controlSocket)
, m_transferMode(transferMode)
{
}

CTransferSocket::~CTransferSocket()
{
	remove_handler();
	if (m_transferEndReason == TransferEndReason::none) {
		m_transferEndReason = TransferEndReason::successful;
	}
	ResetSocket();

	if (m_transferMode == TransferMode::upload || m_transferMode == TransferMode::download) {
		if (ioThread_) {
			if (m_transferMode == TransferMode::download) {
				FinalizeWrite();
			}
			ioThread_->SetEventHandler(nullptr);
		}
	}
}

void CTransferSocket::ResetSocket()
{
	socketServer_.reset();

	active_layer_ = nullptr;

	tls_layer_.reset();
	proxy_layer_.reset();
	ratelimit_layer_.reset();
	socket_.reset();
}

std::wstring CTransferSocket::SetupActiveTransfer(std::string const& ip)
{
	ResetSocket();
	socketServer_ = CreateSocketServer();

	if (!socketServer_) {
		controlSocket_.log(logmsg::debug_warning, L"CreateSocketServer failed");
		return std::wstring();
	}

	int error;
	int port = socketServer_->local_port(error);
	if (port == -1)	{
		ResetSocket();

		controlSocket_.log(logmsg::debug_warning, L"GetLocalPort failed: %s", fz::socket_error_description(error));
		return std::wstring();
	}

	if (engine_.GetOptions().GetOptionVal(OPTION_LIMITPORTS)) {
		port += static_cast<int>(engine_.GetOptions().GetOptionVal(OPTION_LIMITPORTS_OFFSET));
		if (port <= 0 || port >= 65536) {
			controlSocket_.log(logmsg::debug_warning, L"Port outside valid range");
			return std::wstring();
		}
	}

	std::wstring portArguments;
	if (socketServer_->address_family() == fz::address_type::ipv6) {
		portArguments = fz::sprintf(L"|2|%s|%d|", ip, port);
	}
	else {
		portArguments = fz::to_wstring(ip);
		fz::replace_substrings(portArguments, L".", L",");
		portArguments += fz::sprintf(L",%d,%d", port / 256, port % 256);
	}

	return portArguments;
}

void CTransferSocket::OnSocketEvent(fz::socket_event_source* source, fz::socket_event_flag t, int error)
{
	if (socketServer_) {
		if (t == fz::socket_event_flag::connection) {
			OnAccept(error);
		}
		else {
			controlSocket_.log(logmsg::debug_info, L"Unhandled socket event %d from listening socket", t);
		}
		return;
	}

	switch (t)
	{
	case fz::socket_event_flag::connection:
		if (error) {
			if (source == proxy_layer_.get()) {
				controlSocket_.log(logmsg::error, _("Proxy handshake failed: %s"), fz::socket_error_description(error));
			}
			else {
				controlSocket_.log(logmsg::error, _("The data connection could not be established: %s"), fz::socket_error_description(error));
			}
			TransferEnd(TransferEndReason::transfer_failure);
		}
		else {
			OnConnect();
		}
		break;
	case fz::socket_event_flag::read:
		if (error) {
			OnSocketError(error);
		}
		else {
			OnReceive();
		}
		break;
	case fz::socket_event_flag::write:
		if (error) {
			OnSocketError(error);
		}
		else {
			OnSend();
		}
		break;
	default:
		// Uninteresting
		break;
	}
}

void CTransferSocket::OnAccept(int error)
{
	controlSocket_.SetAlive();
	controlSocket_.log(logmsg::debug_verbose, L"CTransferSocket::OnAccept(%d)", error);

	if (!socketServer_) {
		controlSocket_.log(logmsg::debug_warning, L"No socket server in OnAccept", error);
		return;
	}

	socket_ = socketServer_->accept(error);
	if (!socket_) {
		if (error == EAGAIN) {
			controlSocket_.log(logmsg::debug_verbose, L"No pending connection");
		}
		else {
			controlSocket_.log(logmsg::status, _("Could not accept connection: %s"), fz::socket_error_description(error));
			TransferEnd(TransferEndReason::transfer_failure);
		}
		return;
	}
	socketServer_.reset();

	if (!InitLayers(true)) {
		TransferEnd(TransferEndReason::transfer_failure);
		return;
	}

	if (active_layer_->get_state() == fz::socket_state::connected) {
		OnConnect();
	}
}

void CTransferSocket::OnConnect()
{
	controlSocket_.SetAlive();
	controlSocket_.log(logmsg::debug_verbose, L"CTransferSocket::OnConnect");

	if (!socket_) {
		controlSocket_.log(logmsg::debug_verbose, L"CTransferSocket::OnConnect called without socket");
		return;
	}

	if (tls_layer_) {
		// Re-enable Nagle algorithm
		socket_->set_flags(fz::socket::flag_nodelay, false);
	}

#ifdef FZ_WINDOWS
	if (m_transferMode == TransferMode::upload) {
		// For send buffer tuning
		add_timer(fz::duration::from_seconds(1), false);
	}
#endif

	if (m_bActive) {
		TriggerPostponedEvents();
	}
}

void CTransferSocket::OnReceive()
{
	controlSocket_.log(logmsg::debug_debug, L"CTransferSocket::OnReceive(), m_transferMode=%d", m_transferMode);

	if (!m_bActive) {
		controlSocket_.log(logmsg::debug_verbose, L"Postponing receive, m_bActive was false.");
		m_postponedReceive = true;
		return;
	}

	if (m_transferEndReason == TransferEndReason::none) {
		if (m_transferMode == TransferMode::list) {
			for (;;) {
				char *pBuffer = new char[4096];
				int error;
				int numread = active_layer_->read(pBuffer, 4096, error);
				if (numread < 0) {
					delete [] pBuffer;
					if (error != EAGAIN) {
						controlSocket_.log(logmsg::error, L"Could not read from transfer socket: %s", fz::socket_error_description(error));
						TransferEnd(TransferEndReason::transfer_failure);
					}
					return;
				}

				if (numread > 0) {
					if (!m_pDirectoryListingParser->AddData(pBuffer, numread)) {
						TransferEnd(TransferEndReason::transfer_failure);
						return;
					}

					controlSocket_.SetActive(CFileZillaEngine::recv);
					if (!m_madeProgress) {
						m_madeProgress = 2;
						engine_.transfer_status_.SetMadeProgress();
					}
					engine_.transfer_status_.Update(numread);
				}
				else {
					delete [] pBuffer;
					TransferEnd(TransferEndReason::successful);
					return;
				}
			}
			return;
		}
		else if (m_transferMode == TransferMode::download) {
			int error;
			int numread;

			// Only do a certain number of iterations in one go to keep the event loop going.
			// Otherwise this behaves like a livelock on very large files written to a very fast
			// SSD downloaded from a very fast server.
			for (int i = 0; i < 100; ++i) {
				if (!CheckGetNextWriteBuffer()) {
					return;
				}

				numread = active_layer_->read(m_pTransferBuffer, m_transferBufferLen, error);
				if (numread <= 0) {
					break;
				}

				controlSocket_.SetActive(CFileZillaEngine::recv);
				if (!m_madeProgress) {
					m_madeProgress = 2;
					engine_.transfer_status_.SetMadeProgress();
				}
				engine_.transfer_status_.Update(numread);

				m_pTransferBuffer += numread;
				m_transferBufferLen -= numread;
			}

			if (numread < 0) {
				if (error != EAGAIN) {
					controlSocket_.log(logmsg::error, L"Could not read from transfer socket: %s", fz::socket_error_description(error));
					TransferEnd(TransferEndReason::transfer_failure);
				}
			}
			else if (!numread) {
				FinalizeWrite();
			}
			else {
				send_event<fz::socket_event>(active_layer_, fz::socket_event_flag::read, 0);
			}
			return;
		}
		else if (m_transferMode == TransferMode::resumetest) {
			for (;;) {
				char buffer[2];
				int error;
				int numread = active_layer_->read(buffer, 2, error);
				if (numread < 0) {
					if (error != EAGAIN) {
						controlSocket_.log(logmsg::error, L"Could not read from transfer socket: %s", fz::socket_error_description(error));
						TransferEnd(TransferEndReason::transfer_failure);
					}
					return;
				}

				if (!numread) {
					if (m_transferBufferLen == 1) {
						TransferEnd(TransferEndReason::successful);
					}
					else {
						controlSocket_.log(logmsg::debug_warning, L"Server incorrectly sent %d bytes", m_transferBufferLen);
						TransferEnd(TransferEndReason::failed_resumetest);
					}
					return;
				}
				m_transferBufferLen += numread;

				if (m_transferBufferLen > 1) {
					controlSocket_.log(logmsg::debug_warning, L"Server incorrectly sent %d bytes", m_transferBufferLen);
					TransferEnd(TransferEndReason::failed_resumetest);
					return;
				}
			}
			return;
		}
	}

	char discard[1024];
	int error;
	int numread = active_layer_->read(discard, 1024, error);

	if (m_transferEndReason == TransferEndReason::none) {
		// If we get here we're uploading

		if (numread > 0) {
			controlSocket_.log(logmsg::error, L"Received data from the server during an upload");
			TransferEnd(TransferEndReason::transfer_failure);
		}
		else if (numread < 0 && error != EAGAIN) {
			controlSocket_.log(logmsg::error, L"Could not read from transfer socket: %s", fz::socket_error_description(error));
			TransferEnd(TransferEndReason::transfer_failure);
		}
	}
	else {
		if (!numread || (numread < 0 && error != EAGAIN)) {
			ResetSocket();
		}
	}
}

void CTransferSocket::OnSend()
{
	if (!active_layer_) {
		controlSocket_.log(logmsg::debug_verbose, L"OnSend called without backend. Ignoring event.");
		return;
	}

	if (!m_bActive) {
		controlSocket_.log(logmsg::debug_verbose, L"Postponing send");
		m_postponedSend = true;
		return;
	}

	if (m_transferMode != TransferMode::upload || m_transferEndReason != TransferEndReason::none) {
		return;
	}

	int error;
	int written;

	// Only do a certain number of iterations in one go to keep the event loop going.
	// Otherwise this behaves like a livelock on very large files read from a very fast
	// SSD uploaded to a very fast server.
	for (int i = 0; i < 100; ++i) {
		if (!CheckGetNextReadBuffer()) {
			return;
		}

		written = active_layer_->write(m_pTransferBuffer, m_transferBufferLen, error);
		if (written <= 0) {
			break;
		}

		controlSocket_.SetActive(CFileZillaEngine::send);
		if (m_madeProgress == 1) {
			controlSocket_.log(logmsg::debug_debug, L"Made progress in CTransferSocket::OnSend()");
			m_madeProgress = 2;
			engine_.transfer_status_.SetMadeProgress();
		}
		engine_.transfer_status_.Update(written);

		m_pTransferBuffer += written;
		m_transferBufferLen -= written;
	}

	if (written < 0) {
		if (error == EAGAIN) {
			if (!m_madeProgress) {
				controlSocket_.log(logmsg::debug_debug, L"First EAGAIN in CTransferSocket::OnSend()");
				m_madeProgress = 1;
				engine_.transfer_status_.SetMadeProgress();
			}
		}
		else {
			controlSocket_.log(logmsg::error, L"Could not write to transfer socket: %s", fz::socket_error_description(error));
			TransferEnd(TransferEndReason::transfer_failure);
		}
	}
	else if (written > 0) {
		send_event<fz::socket_event>(active_layer_, fz::socket_event_flag::write, 0);
	}
}

void CTransferSocket::OnSocketError(int error)
{
	controlSocket_.log(logmsg::debug_verbose, L"CTransferSocket::OnSocketError(%d)", error);

	if (m_transferEndReason != TransferEndReason::none) {
		return;
	}

	controlSocket_.log(logmsg::error, _("Transfer connection interrupted: %s"), fz::socket_error_description(error));
	TransferEnd(TransferEndReason::transfer_failure);
}

bool CTransferSocket::SetupPassiveTransfer(std::wstring const& host, int port)
{
	std::string ip = fz::to_utf8(host);

	ResetSocket();

	socket_ = std::make_unique<fz::socket>(engine_.GetThreadPool(), nullptr);

	SetSocketBufferSizes(*socket_);

	// Try to bind the source IP of the data connection to the same IP as the control connection.
	// We can do so either if
	// 1) the destination IP of the data connection matches peer IP of the control connection or
	// 2) we are using a proxy.
	//
	// In case destination IPs of control and data connection are different, do not bind to the
	// same source.

	std::string bindAddress;
	if (controlSocket_.proxy_layer_) {
		bindAddress = controlSocket_.socket_->local_ip();
		controlSocket_.log(logmsg::debug_info, L"Binding data connection source IP to control connection source IP %s", bindAddress);
		socket_->bind(bindAddress);
	}
	else {
		if (controlSocket_.socket_->peer_ip(true) == ip || controlSocket_.socket_->peer_ip(false) == ip) {
			bindAddress = controlSocket_.socket_->local_ip();
			controlSocket_.log(logmsg::debug_info, L"Binding data connection source IP to control connection source IP %s", bindAddress);
			socket_->bind(bindAddress);
		}
		else {
			controlSocket_.log(logmsg::debug_warning, L"Destination IP of data connection does not match peer IP of control connection. Not binding source address of data connection.");
		}
	}

	if (!InitLayers(false)) {
		ResetSocket();
		return false;
	}

	int res = active_layer_->connect(fz::to_native(ip), port, fz::address_type::unknown);
	if (res) {
		ResetSocket();
		return false;
	}

	return true;
}

bool CTransferSocket::InitLayers(bool active)
{
	ratelimit_layer_ = std::make_unique<fz::rate_limited_layer>(nullptr, *socket_, &engine_.GetRateLimiter());
	active_layer_ = ratelimit_layer_.get();

	if (controlSocket_.proxy_layer_ && !active) {
		fz::native_string proxy_host = controlSocket_.proxy_layer_->next().peer_host();
		int error;
		int proxy_port = controlSocket_.proxy_layer_->next().peer_port(error);

		if (proxy_host.empty() || proxy_port < 1) {
			controlSocket_.log(logmsg::debug_warning, L"Could not get peer address of control connection.");
			return false;
		}

		proxy_layer_ = std::make_unique<CProxySocket>(nullptr, *active_layer_, &controlSocket_, controlSocket_.proxy_layer_->GetProxyType(), proxy_host, proxy_port, controlSocket_.proxy_layer_->GetUser(), controlSocket_.proxy_layer_->GetPass());
		active_layer_ = proxy_layer_.get();
	}

	if (controlSocket_.m_protectDataChannel) {
		// Disable Nagle's algorithm during TLS handshake
		socket_->set_flags(fz::socket::flag_nodelay, true);

		tls_layer_ = std::make_unique<fz::tls_layer>(controlSocket_.event_loop_, nullptr, *active_layer_, nullptr, controlSocket_.logger_);
		active_layer_ = tls_layer_.get();

		if (!tls_layer_->client_handshake(controlSocket_.tls_layer_->get_raw_certificate(), controlSocket_.tls_layer_->get_session_parameters(), controlSocket_.tls_layer_->peer_host())) {
			return false;
		}
	}

	active_layer_->set_event_handler(this);

	return true;
}

void CTransferSocket::SetActive()
{
	if (m_transferEndReason != TransferEndReason::none) {
		return;
	}
	if (m_transferMode == TransferMode::download || m_transferMode == TransferMode::upload) {
		if (ioThread_) {
			ioThread_->SetEventHandler(this);
		}
	}

	m_bActive = true;
	if (!socket_) {
		return;
	}

	if (socket_->is_connected()) {
		TriggerPostponedEvents();
	}
}

void CTransferSocket::TransferEnd(TransferEndReason reason)
{
	controlSocket_.log(logmsg::debug_verbose, L"CTransferSocket::TransferEnd(%d)", reason);

	if (m_transferEndReason != TransferEndReason::none) {
		return;
	}
	m_transferEndReason = reason;

	if (reason != TransferEndReason::successful) {
		ResetSocket();
	}
	else {
		active_layer_->shutdown();
	}

	controlSocket_.send_event<TransferEndEvent>();
}

std::unique_ptr<fz::listen_socket> CTransferSocket::CreateSocketServer(int port)
{
	auto socket = std::make_unique<fz::listen_socket>(engine_.GetThreadPool(), this);
	int res = socket->listen(controlSocket_.socket_->address_family(), port);
	if (res) {
		controlSocket_.log(logmsg::debug_verbose, L"Could not listen on port %d: %s", port, fz::socket_error_description(res));
		socket.reset();
	}
	else {
		SetSocketBufferSizes(*socket);
	}

	return socket;
}

std::unique_ptr<fz::listen_socket> CTransferSocket::CreateSocketServer()
{
	if (!engine_.GetOptions().GetOptionVal(OPTION_LIMITPORTS)) {
		// Ask the systen for a port
		return CreateSocketServer(0);
	}

	// Try out all ports in the port range.
	// Upon first call, we try to use a random port fist, after that
	// increase the port step by step

	// Windows only: I think there's a bug in the socket implementation of
	// Windows: Even if using SO_REUSEADDR, using the same local address
	// twice will fail unless there are a couple of minutes between the
	// connection attempts. This may cause problems if transferring lots of
	// files with a narrow port range.

	static int start = 0;

	int low = engine_.GetOptions().GetOptionVal(OPTION_LIMITPORTS_LOW);
	int high = engine_.GetOptions().GetOptionVal(OPTION_LIMITPORTS_HIGH);
	if (low > high) {
		low = high;
	}

	if (start < low || start > high) {
		start = static_cast<decltype(start)>(fz::random_number(low, high));
		assert(start >= low && start <= high);
	}

	std::unique_ptr<fz::listen_socket> server;

	int count = high - low + 1;
	while (count--) {
		server = CreateSocketServer(start++);
		if (server) {
			break;
		}
		if (start > high) {
			start = low;
		}
	}

	return server;
}

bool CTransferSocket::CheckGetNextWriteBuffer()
{
	if (!m_transferBufferLen) {
		int res = ioThread_->GetNextWriteBuffer(&m_pTransferBuffer);

		if (res == IO_Again) {
			return false;
		}
		else if (res == IO_Error) {
			std::wstring error = ioThread_->GetError();
			if (error.empty()) {
				controlSocket_.log(logmsg::error, _("Can't write data to file."));
			}
			else {
				controlSocket_.log(logmsg::error, _("Can't write data to file: %s"), error);
			}
			TransferEnd(TransferEndReason::transfer_failure_critical);
			return false;
		}

		m_transferBufferLen = BUFFERSIZE;
	}

	return true;
}

bool CTransferSocket::CheckGetNextReadBuffer()
{
	if (!m_transferBufferLen) {
		int res = ioThread_->GetNextReadBuffer(&m_pTransferBuffer);
		if (res == IO_Again) {
			return false;
		}
		else if (res == IO_Error) {
			controlSocket_.log(logmsg::error, _("Can't read from file"));
			TransferEnd(TransferEndReason::transfer_failure);
			return false;
		}
		else if (res == IO_Success) {
			int res = active_layer_->shutdown();
			if (res && res != EAGAIN) {
				TransferEnd(TransferEndReason::transfer_failure);
				return false;
			}
			TransferEnd(TransferEndReason::successful);
			return false;
		}
		m_transferBufferLen = res;
	}

	return true;
}

void CTransferSocket::OnIOThreadEvent()
{
	if (!m_bActive || m_transferEndReason != TransferEndReason::none) {
		return;
	}

	if (m_transferMode == TransferMode::download) {
		OnReceive();
	}
	else if (m_transferMode == TransferMode::upload) {
		OnSend();
	}
}

void CTransferSocket::FinalizeWrite()
{
	bool res = ioThread_->Finalize(BUFFERSIZE - m_transferBufferLen);
	m_transferBufferLen = BUFFERSIZE;

	if (m_transferEndReason != TransferEndReason::none) {
		return;
	}

	if (res) {
		TransferEnd(TransferEndReason::successful);
	}
	else {
		std::wstring error = ioThread_->GetError();
		if (error.empty()) {
			controlSocket_.log(logmsg::error, _("Can't write data to file."));
		}
		else {
			controlSocket_.log(logmsg::error, _("Can't write data to file: %s"), error);
		}
		TransferEnd(TransferEndReason::transfer_failure_critical);
	}
}

void CTransferSocket::TriggerPostponedEvents()
{
	assert(m_bActive);

	if (m_postponedReceive) {
		controlSocket_.log(logmsg::debug_verbose, L"Executing postponed receive");
		m_postponedReceive = false;
		OnReceive();
		if (m_transferEndReason != TransferEndReason::none) {
			return;
		}
	}
	if (m_postponedSend) {
		controlSocket_.log(logmsg::debug_verbose, L"Executing postponed send");
		m_postponedSend = false;
		OnSend();
	}
}

void CTransferSocket::SetSocketBufferSizes(fz::socket_base& socket)
{
	const int size_read = engine_.GetOptions().GetOptionVal(OPTION_SOCKET_BUFFERSIZE_RECV);
#if FZ_WINDOWS
	const int size_write = -1;
#else
	const int size_write = engine_.GetOptions().GetOptionVal(OPTION_SOCKET_BUFFERSIZE_SEND);
#endif
	socket.set_buffer_sizes(size_read, size_write);
}

void CTransferSocket::operator()(fz::event_base const& ev)
{
	fz::dispatch<fz::socket_event, CIOThreadEvent, fz::timer_event>(ev, this,
		&CTransferSocket::OnSocketEvent,
		&CTransferSocket::OnIOThreadEvent,
		&CTransferSocket::OnTimer);
}

void CTransferSocket::OnTimer(fz::timer_id)
{
	if (socket_ && socket_->is_connected()) {
		int const ideal_send_buffer = socket_->ideal_send_buffer_size();
		if (ideal_send_buffer != -1) {
			socket_->set_buffer_sizes(-1, ideal_send_buffer);
		}
	}
}
