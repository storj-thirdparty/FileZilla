#include <filezilla.h>

#include "connect.h"
#include "event.h"
#include "input_thread.h"
#include "proxy.h"

#include <libfilezilla/process.hpp>
#include <libfilezilla/uri.hpp>

int CStorjConnectOpData::Send()
{
	switch (opState)
	{
	case connect_init:
		{
			log(logmsg::status, _("Connecting to %s..."), currentServer_.Format(ServerFormat::with_optional_port, controlSocket_.credentials_));

			auto executable = fz::to_native(engine_.GetOptions().GetOption(OPTION_FZSTORJ_EXECUTABLE));
			if (executable.empty()) {
				executable = fzT("fzstorj");
			}
			log(logmsg::debug_verbose, L"Going to execute %s", executable);

			std::vector<fz::native_string> args;
			controlSocket_.process_ = std::make_unique<fz::process>();
			if (!controlSocket_.process_->spawn(executable, args)) {
				log(logmsg::debug_warning, L"Could not create process");
				return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
			}

			controlSocket_.input_thread_ = std::make_unique<CStorjInputThread>(controlSocket_, *controlSocket_.process_);
			if (!controlSocket_.input_thread_->spawn(engine_.GetThreadPool())) {
				log(logmsg::debug_warning, L"Thread creation failed");
				controlSocket_.input_thread_.reset();
				return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
			}
		}
		return FZ_REPLY_WOULDBLOCK;
	case connect_timeout:
		return controlSocket_.SendCommand(fz::sprintf(L"timeout %d", engine_.GetOptions().GetOptionVal(OPTION_TIMEOUT)));
	case connect_proxy:
		{
			fz::uri proxy_uri;
			switch (engine_.GetOptions().GetOptionVal(OPTION_PROXY_TYPE))
			{
			case 0:
				opState = connect_host;
				return FZ_REPLY_CONTINUE;
			case static_cast<int>(ProxyType::HTTP):
				proxy_uri.scheme_ = "http";
				break;
			case static_cast<int>(ProxyType::SOCKS5):
				proxy_uri.scheme_ = "socks5h";
				break;
			case static_cast<int>(ProxyType::SOCKS4):
				proxy_uri.scheme_ = "socks4a";
				break;
			default:
				log(logmsg::debug_warning, L"Unsupported proxy type");
				return FZ_REPLY_INTERNALERROR | FZ_REPLY_DISCONNECTED;
			}

			proxy_uri.host_ = fz::to_utf8(engine_.GetOptions().GetOption(OPTION_PROXY_HOST));
			proxy_uri.port_ = engine_.GetOptions().GetOptionVal(OPTION_PROXY_PORT);
			proxy_uri.user_ = fz::to_utf8(engine_.GetOptions().GetOption(OPTION_PROXY_USER));
			proxy_uri.pass_ = fz::to_utf8(engine_.GetOptions().GetOption(OPTION_PROXY_PASS));

			auto cmd = L"proxy " + fz::to_wstring(proxy_uri.to_string());
			proxy_uri.pass_.clear();
			auto show = L"proxy " + fz::to_wstring(proxy_uri.to_string());
			return controlSocket_.SendCommand(cmd, show);
		}
	case connect_host:
		return controlSocket_.SendCommand(fz::sprintf(L"host %s", currentServer_.Format(ServerFormat::with_optional_port)));
	case connect_user:
		return (controlSocket_.credentials_.logonType_ == LogonType::anonymous) ? FZ_REPLY_OK : controlSocket_.SendCommand(fz::sprintf(L"user %s", currentServer_.GetUser()));
	case connect_pass:
		{
			if(controlSocket_.credentials_.logonType_ != LogonType::anonymous) {
				std::wstring pass = controlSocket_.credentials_.GetPass();
				size_t pos = pass.rfind('|');
				if (pos == std::wstring::npos) {
					log(logmsg::error, _("Password or encryption key is not set"));
					return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
				}
				pass = pass.substr(0, pos);
				return controlSocket_.SendCommand(fz::sprintf(L"pass %s", pass), fz::sprintf(L"pass %s", std::wstring(pass.size(), '*')));
			}
		}
	case connect_key:
		{
			if(controlSocket_.credentials_.logonType_ != LogonType::anonymous) {
				std::wstring key = controlSocket_.credentials_.GetPass();
				size_t pos = key.rfind('|');
				if (pos == std::wstring::npos) {
					log(logmsg::error, _("Password or encryption key is not set"));
					return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
				}
				key = key.substr(pos + 1);
				return controlSocket_.SendCommand(fz::sprintf(L"key %s", key), fz::sprintf(L"key %s", std::wstring(key.size(), '*')));
			}
		}
	default:
		log(logmsg::debug_warning, L"Unknown op state: %d", opState);
		break;
	}

	return FZ_REPLY_INTERNALERROR | FZ_REPLY_DISCONNECTED;
}

int CStorjConnectOpData::ParseResponse()
{
	if (controlSocket_.result_ != FZ_REPLY_OK) {
		return FZ_REPLY_ERROR | FZ_REPLY_DISCONNECTED;
	}

	switch (opState)
	{
	case connect_init:
		if (controlSocket_.response_ != fz::sprintf(L"fzStorj started, protocol_version=%d", FZSTORJ_PROTOCOL_VERSION)) {
			log(logmsg::error, _("fzstorj belongs to a different version of FileZilla"));
			return FZ_REPLY_INTERNALERROR | FZ_REPLY_DISCONNECTED;
		}
		opState = connect_timeout;
		break;
	case connect_timeout:
		opState = connect_host;
		break;
	case connect_proxy:
		opState = connect_host;
		break;
	case connect_host:
		opState = connect_user;
		break;
	case connect_user:
		opState = connect_pass;
		break;
	case connect_pass:
		opState = connect_key;
		break;
	case connect_key:
		return FZ_REPLY_OK;
	default:
		log(logmsg::debug_warning, L"Unknown op state: %d", opState);
		return FZ_REPLY_INTERNALERROR | FZ_REPLY_DISCONNECTED;
	}

	return FZ_REPLY_CONTINUE;
}
