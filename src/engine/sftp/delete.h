#ifndef FILEZILLA_ENGINE_SFTP_DELETE_HEADER
#define FILEZILLA_ENGINE_SFTP_DELETE_HEADER

#include "sftpcontrolsocket.h"

class CSftpDeleteOpData final : public COpData, public CSftpOpData
{
public:
	CSftpDeleteOpData(CSftpControlSocket & controlSocket)
		: COpData(Command::del, L"CSftpDeleteOpData")
		, CSftpOpData(controlSocket)
	{}

	virtual int Send() override;
	virtual int ParseResponse() override;
	virtual int SubcommandResult(int prevResult, COpData const&) override;
	virtual int Reset(int result) override;

	CServerPath path_;
	std::vector<std::wstring> files_;

	// Set to fz::datetime::Now initially and after
	// sending an updated listing to the UI.
	fz::datetime time_;

	bool needSendListing_{};

	// Set to true if deletion of at least one file failed
	bool deleteFailed_{};
};

#endif
