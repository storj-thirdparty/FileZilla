#ifndef FILEZILLA_ENGINE_STORJ_LIST_HEADER
#define FILEZILLA_ENGINE_STORJ_LIST_HEADER

#include "storjcontrolsocket.h"

class CStorjListOpData final : public COpData, public CStorjOpData
{
public:
	CStorjListOpData(CStorjControlSocket & controlSocket, CServerPath const& path, std::wstring const& subDir, int)
		: COpData(Command::list, L"CStorjListOpData")
		, CStorjOpData(controlSocket)
		, path_(path)
		, subDir_(subDir)
	{
	}

	virtual int Send() override;
	virtual int ParseResponse() override;
	virtual int SubcommandResult(int prevResult, COpData const& previousOperation) override;

	int ParseEntry(std::wstring && name, std::wstring const& size, std::wstring && id, std::wstring const& created);

	std::wstring GetPathId() const { return pathId_; }

private:
	CServerPath path_;
	std::wstring subDir_;

	std::vector<fz::shared_value<CDirentry>> entries_;

	fz::monotonic_clock time_before_locking_;

	std::wstring bucket_;
	std::wstring pathId_;
};

#endif
