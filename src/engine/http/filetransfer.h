#ifndef FILEZILLA_ENGINE_HTTP_FILETRANSFER_HEADER
#define FILEZILLA_ENGINE_HTTP_FILETRANSFER_HEADER

#include "httpcontrolsocket.h"

#include <libfilezilla/file.hpp>

class CServerPath;

class CHttpFileTransferOpData final : public CFileTransferOpData, public CHttpOpData
{
public:
	CHttpFileTransferOpData(CHttpControlSocket & controlSocket, bool is_download, std::wstring const& local_file, std::wstring const& remote_file, CServerPath const& remote_path, CFileTransferCommand::t_transferSettings const& settings);
	CHttpFileTransferOpData(CHttpControlSocket & controlSocket, fz::uri const& uri, std::string const& verb, std::string const& body);

	virtual int Send() override;
	virtual int ParseResponse() override { return FZ_REPLY_INTERNALERROR; }
	virtual int SubcommandResult(int prevResult, COpData const& previousOperation) override;

private:
	int OpenFile();

	int OnHeader();
	int OnData(unsigned char const* data, unsigned int len);

	HttpRequestResponse rr_;
	fz::file file_;

	int redirectCount_{};
};

#endif
