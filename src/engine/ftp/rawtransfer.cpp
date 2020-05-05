#include <filezilla.h>

#include "rawtransfer.h"
#include "servercapabilities.h"
#include "transfersocket.h"

#include <libfilezilla/iputils.hpp>

#include <assert.h>

int CFtpRawTransferOpData::Send()
{
	if (!controlSocket_.m_pTransferSocket) {
		log(logmsg::debug_info, L"Empty m_pTransferSocket");
		return FZ_REPLY_INTERNALERROR;
	}

	std::wstring cmd;
	bool measureRTT = false;
	switch (opState)
	{
	case rawtransfer_init:
		if ((pOldData->binary && controlSocket_.m_lastTypeBinary == 1) ||
			(!pOldData->binary && controlSocket_.m_lastTypeBinary == 0))
		{
			opState = rawtransfer_port_pasv;
		}
		else {
			opState = rawtransfer_type;
		}

		if (controlSocket_.proxy_layer_) {
			// Only passive supported
			// Theoretically could use reverse proxy ability in SOCKS5, but
			// it is too fragile to set up with all those broken routers and
			// firewalls sabotaging connections. Regular active mode is hard
			// enough already
			bPasv = true;
			bTriedActive = true;
		}
		else {
			switch (currentServer_.GetPasvMode())
			{
			case MODE_PASSIVE:
				bPasv = true;
				break;
			case MODE_ACTIVE:
				bPasv = false;
				break;
			default:
				bPasv = engine_.GetOptions().GetOptionVal(OPTION_USEPASV) != 0;
				break;
			}
		}

		return FZ_REPLY_CONTINUE;
	case rawtransfer_type:
		controlSocket_.m_lastTypeBinary = -1;
		if (pOldData->binary) {
			cmd = L"TYPE I";
		}
		else {
			cmd = L"TYPE A";
		}
		measureRTT = true;
		break;
	case rawtransfer_port_pasv:
		if (bPasv) {
			cmd = GetPassiveCommand();
		}
		else {
			std::string address;
			int res = controlSocket_.GetExternalIPAddress(address);
			if (res == FZ_REPLY_WOULDBLOCK) {
				return res;
			}
			else if (res == FZ_REPLY_OK) {
				std::wstring portArgument = controlSocket_.m_pTransferSocket->SetupActiveTransfer(address);
				if (!portArgument.empty()) {
					bTriedActive = true;
					if (controlSocket_.socket_->address_family() == fz::address_type::ipv6) {
						cmd = L"EPRT " + portArgument;
					}
					else {
						cmd = L"PORT " + portArgument;
					}
					break;
				}
			}

			if (!engine_.GetOptions().GetOptionVal(OPTION_ALLOW_TRANSFERMODEFALLBACK) || bTriedPasv) {
				log(logmsg::error, _("Failed to create listening socket for active mode transfer"));
				return FZ_REPLY_ERROR;
			}
			log(logmsg::debug_warning, _("Failed to create listening socket for active mode transfer"));
			bTriedActive = true;
			bPasv = true;
			cmd = GetPassiveCommand();
		}
		break;
	case rawtransfer_rest:
		cmd = L"REST " + std::to_wstring(pOldData->resumeOffset);
		if (pOldData->resumeOffset > 0) {
			controlSocket_.m_sentRestartOffset = true;
		}
		measureRTT = true;
		break;
	case rawtransfer_transfer:
		if (bPasv) {
			if (!controlSocket_.m_pTransferSocket->SetupPassiveTransfer(host_, port_)) {
				log(logmsg::error, _("Could not establish connection to server"));
				return FZ_REPLY_ERROR;
			}
		}

		cmd = cmd_;
		pOldData->tranferCommandSent = true;

		engine_.transfer_status_.SetStartTime();
		controlSocket_.m_pTransferSocket->SetActive();
		break;
	case rawtransfer_waitfinish:
	case rawtransfer_waittransferpre:
	case rawtransfer_waittransfer:
	case rawtransfer_waitsocket:
		break;
	default:
		log(logmsg::debug_warning, L"invalid opstate");
		return FZ_REPLY_INTERNALERROR;
	}
	if (!cmd.empty()) {
		return controlSocket_.SendCommand(cmd, false, measureRTT);
	}

	return FZ_REPLY_WOULDBLOCK;
}

int CFtpRawTransferOpData::ParseResponse()
{
	if (opState == rawtransfer_init) {
		return FZ_REPLY_ERROR;
	}

	int const code = controlSocket_.GetReplyCode();

	bool error = false;
	switch (opState)
	{
	case rawtransfer_type:
		if (code != 2 && code != 3) {
			error = true;
		}
		else {
			opState = rawtransfer_port_pasv;
			controlSocket_.m_lastTypeBinary = pOldData->binary ? 1 : 0;
		}
		break;
	case rawtransfer_port_pasv:
		if (code != 2 && code != 3) {
			if (!engine_.GetOptions().GetOptionVal(OPTION_ALLOW_TRANSFERMODEFALLBACK)) {
				error = true;
				break;
			}

			if (bTriedPasv) {
				if (bTriedActive) {
					error = true;
				}
				else {
					bPasv = false;
				}
			}
			else {
				bPasv = true;
			}
			break;
		}
		if (bPasv) {
			bool parsed;
			if (GetPassiveCommand() == L"EPSV") {
				parsed = ParseEpsvResponse();
			}
			else {
				parsed = ParsePasvResponse();
			}
			if (!parsed) {
				if (!engine_.GetOptions().GetOptionVal(OPTION_ALLOW_TRANSFERMODEFALLBACK)) {
					error = true;
					break;
				}

				if (!bTriedActive) {
					bPasv = false;
				}
				else {
					error = true;
				}
				break;
			}
		}
		if (pOldData->resumeOffset > 0 || controlSocket_.m_sentRestartOffset) {
			opState = rawtransfer_rest;
		}
		else {
			opState = rawtransfer_transfer;
		}
		break;
	case rawtransfer_rest:
		if (pOldData->resumeOffset <= 0) {
			controlSocket_.m_sentRestartOffset = false;
		}
		if (pOldData->resumeOffset > 0 && code != 2 && code != 3) {
			error = true;
		}
		else {
			opState = rawtransfer_transfer;
		}
		break;
	case rawtransfer_transfer:
		if (code == 1) {
			opState = rawtransfer_waitfinish;
		}
		else if (code == 2 || code == 3) {
			// A few broken servers omit the 1yz reply.
			opState = rawtransfer_waitsocket;
		}
		else {
			if (pOldData->transferEndReason == TransferEndReason::successful) {
				pOldData->transferEndReason = TransferEndReason::transfer_command_failure_immediate;
			}
			error = true;
		}
		break;
	case rawtransfer_waittransferpre:
		if (code == 1) {
			opState = rawtransfer_waittransfer;
		}
		else if (code == 2 || code == 3) {
			// A few broken servers omit the 1yz reply.
			if (pOldData->transferEndReason != TransferEndReason::successful) {
				error = true;
				break;
			}

			return FZ_REPLY_OK;
		}
		else {
			if (pOldData->transferEndReason == TransferEndReason::successful) {
				pOldData->transferEndReason = TransferEndReason::transfer_command_failure_immediate;
			}
			error = true;
		}
		break;
	case rawtransfer_waitfinish:
		if (code != 2 && code != 3) {
			if (pOldData->transferEndReason == TransferEndReason::successful) {
				pOldData->transferEndReason = TransferEndReason::transfer_command_failure;
			}
			error = true;
		}
		else {
			opState = rawtransfer_waitsocket;
		}
		break;
	case rawtransfer_waittransfer:
		if (code != 2 && code != 3) {
			if (pOldData->transferEndReason == TransferEndReason::successful) {
				pOldData->transferEndReason = TransferEndReason::transfer_command_failure;
			}
			error = true;
		}
		else {
			if (pOldData->transferEndReason != TransferEndReason::successful) {
				error = true;
				break;
			}

			return FZ_REPLY_OK;
		}
		break;
	case rawtransfer_waitsocket:
		log(logmsg::debug_warning, L"Extra reply received during rawtransfer_waitsocket.");
		error = true;
		break;
	default:
		log(logmsg::debug_warning, L"Unknown op state");
		error = true;
	}
	if (error) {
		return FZ_REPLY_ERROR;
	}

	return FZ_REPLY_CONTINUE;
}

bool CFtpRawTransferOpData::ParseEpsvResponse()
{
	size_t pos = controlSocket_.m_Response.find(L"(|||");
	if (pos == std::wstring::npos) {
		return false;
	}

	size_t pos2 = controlSocket_.m_Response.find(L"|)", pos + 4);
	if (pos2 == std::wstring::npos || pos2 == pos + 4) {
		return false;
	}

	std::wstring number = controlSocket_.m_Response.substr(pos + 4, pos2 - pos - 4);
	auto port = fz::to_integral<unsigned int>(number);

	if (port == 0 || port > 65535) {
		return false;
	}

	port_ = port;

	if (controlSocket_.proxy_layer_) {
		host_ = currentServer_.GetHost();
	}
	else {
		host_ = fz::to_wstring(controlSocket_.socket_->peer_ip());
	}
	return true;
}

bool CFtpRawTransferOpData::ParsePasvResponse()
{
	// Validate ip address
	if (!controlSocket_.m_pasvReplyRegex) {
		std::wstring digit = L"0*[0-9]{1,3}";
		wchar_t const* const  dot = L",";
		std::wstring exp = L"( |\\()(" + digit + dot + digit + dot + digit + dot + digit + dot + digit + dot + digit + L")( |\\)|$)";
		controlSocket_.m_pasvReplyRegex = std::make_unique<std::wregex>(exp);
	}

	std::wsmatch m;
	if (!std::regex_search(controlSocket_.m_Response, m, *controlSocket_.m_pasvReplyRegex)) {
		return false;
	}

	host_ = m[2].str();

	size_t i = host_.rfind(',');
	if (i == std::wstring::npos) {
		return false;
	}
	auto number = fz::to_integral<unsigned int>(host_.substr(i + 1));
	if (number > 255) {
		return false;
	}

	port_ = number; //get ls byte of server socket
	host_ = host_.substr(0, i);
	i = host_.rfind(',');
	if (i == std::string::npos) {
		return false;
	}
	number = fz::to_integral<unsigned int>(host_.substr(i + 1));
	if (number > 255) {
		return false;
	}

	port_ += 256 * number; //add ms byte of server socket
	host_ = host_.substr(0, i);
	fz::replace_substrings(host_, L",", L".");

	if (controlSocket_.proxy_layer_) {
		// We do not have any information about the proxy's inner workings
		return true;
	}

	std::wstring const peerIP = fz::to_wstring(controlSocket_.socket_->peer_ip());
	if (!fz::is_routable_address(host_) && fz::is_routable_address(peerIP)) {
		if (engine_.GetOptions().GetOptionVal(OPTION_PASVREPLYFALLBACKMODE) != 1 || bTriedActive) {
			log(logmsg::status, _("Server sent passive reply with unroutable address. Using server address instead."));
			log(logmsg::debug_info, L"  Reply: %s, peer: %s", host_, peerIP);
			host_ = peerIP;
		}
		else {
			log(logmsg::status, _("Server sent passive reply with unroutable address. Passive mode failed."));
			log(logmsg::debug_info, L"  Reply: %s, peer: %s", host_, peerIP);
			return false;
		}
	}
	else if (engine_.GetOptions().GetOptionVal(OPTION_PASVREPLYFALLBACKMODE) == 2) {
		// Always use server address
		host_ = peerIP;
	}

	return true;
}

std::wstring CFtpRawTransferOpData::GetPassiveCommand()
{
	std::wstring ret = L"PASV";

	assert(bPasv);
	bTriedPasv = true;

	if (controlSocket_.proxy_layer_) {
		// We don't actually know the address family the other end of the proxy uses to reach the server. Hence prefer EPSV
		// if the server supports it.
		if (CServerCapabilities::GetCapability(currentServer_, epsv_command) == yes) {
			ret = L"EPSV";
		}
	}
	else if (controlSocket_.socket_->address_family() == fz::address_type::ipv6) {
		// EPSV is mandatory for IPv6, don't check capabilities
		ret = L"EPSV";
	}
	return ret;
}
