#ifndef FILEZILLA_ENGINE_EXTERNALIPRESOLVER_HEADER
#define FILEZILLA_ENGINE_EXTERNALIPRESOLVER_HEADER

#include <libfilezilla/buffer.hpp>
#include <libfilezilla/socket.hpp>

struct external_ip_resolve_event_type;
typedef fz::simple_event<external_ip_resolve_event_type> CExternalIPResolveEvent;

class CExternalIPResolver final : public fz::event_handler
{
public:
	CExternalIPResolver(fz::thread_pool & pool, fz::event_handler & handler);
	virtual ~CExternalIPResolver();

	CExternalIPResolver(CExternalIPResolver const&) = delete;
	CExternalIPResolver& operator=(CExternalIPResolver const&) = delete;

	bool Done() const { return m_done; }
	bool Successful() const;
	std::string GetIP() const;

	void GetExternalIP(std::wstring const& resolver, fz::address_type protocol, bool force = false);

protected:

	void Close(bool successful);

	std::wstring m_address;
	fz::address_type m_protocol{};
	unsigned long m_port{80};
	fz::thread_pool & thread_pool_;
	fz::event_handler * m_handler{};

	bool m_done{};

	std::string m_data;

	std::unique_ptr<fz::socket> socket_;

	virtual void operator()(fz::event_base const& ev);
	void OnSocketEvent(fz::socket_event_source* source, fz::socket_event_flag t, int error);

	void OnConnect(int error);
	void OnReceive();
	void OnHeader();
	void OnData(unsigned char* buffer, size_t len);
	void OnChunkedData();
	void OnSend();

	std::string m_sendBuffer;

	fz::buffer recvBuffer_;

	// HTTP data
	void ResetHttpData();
	bool m_gotHeader{};
	int m_responseCode{};
	std::wstring m_location;
	int m_redirectCount{};

	enum transferEncodings {
		identity,
		chunked,
		unknown
	};

	transferEncodings m_transferEncoding{unknown};

	struct t_chunkData {
		bool getTrailer{};
		bool terminateChunk{};
		uint64_t size{};
	} m_chunkData;
};

#endif
