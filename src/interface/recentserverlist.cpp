#include <filezilla.h>
#include "recentserverlist.h"
#include "ipcmutex.h"
#include "filezillaapp.h"
#include "Options.h"
#include "xmlfunctions.h"

const std::deque<Site> CRecentServerList::GetMostRecentServers(bool lockMutex)
{
	std::deque<Site> mostRecentServers;

	CInterProcessMutex mutex(MUTEX_MOSTRECENTSERVERS, false);
	if (lockMutex) {
		mutex.Lock();
	}

	CXmlFile xmlFile(wxGetApp().GetSettingsFile(_T("recentservers")));
	auto element = xmlFile.Load();
	if (!element || !(element = element.child("RecentServers"))) {
		return mostRecentServers;
	}

	bool modified = false;
	auto xServer = element.child("Server");
	while (xServer) {
		Site site;
		if (!GetServer(xServer, site) || mostRecentServers.size() >= 10) {
			auto xRemove = xServer;
			xServer = xServer.next_sibling("Server");
			element.remove_child(xRemove);
			modified = true;
		}
		else {
			std::deque<Site>::const_iterator iter;
			for (iter = mostRecentServers.begin(); iter != mostRecentServers.end(); ++iter) {
				if (*iter == site) {
					break;
				}
			}
			if (iter == mostRecentServers.end()) {
				mostRecentServers.push_back(site);
			}
			xServer = xServer.next_sibling("Server");
		}
	}

	if (modified) {
		xmlFile.Save(false);
	}

	return mostRecentServers;
}

void CRecentServerList::SetMostRecentServer(Site const& site)
{
	CInterProcessMutex mutex(MUTEX_MOSTRECENTSERVERS);

	// Make sure list is initialized
	auto mostRecentServers = GetMostRecentServers(false);

	bool relocated = false;
	for (auto iter = mostRecentServers.begin(); iter != mostRecentServers.end(); ++iter) {
		if (iter->server == site.server) {
			mostRecentServers.erase(iter);
			mostRecentServers.push_front(site);
			relocated = true;
			break;
		}
	}
	if (!relocated) {
		mostRecentServers.push_front(site);
		if (mostRecentServers.size() > 10) {
			mostRecentServers.pop_back();
		}
	}

	if (COptions::Get()->GetOptionVal(OPTION_DEFAULT_KIOSKMODE) == 2) {
		return;
	}

	SetMostRecentServers(mostRecentServers, false);
}

void CRecentServerList::SetMostRecentServers(std::deque<Site> const& sites, bool lockMutex)
{
	CInterProcessMutex mutex(MUTEX_MOSTRECENTSERVERS, false);
	if (lockMutex) {
		mutex.Lock();
	}

	if (COptions::Get()->GetOptionVal(OPTION_DEFAULT_KIOSKMODE) == 2) {
		return;
	}

	CXmlFile xmlFile(wxGetApp().GetSettingsFile(_T("recentservers")));
	auto element = xmlFile.CreateEmpty();
	if (!element) {
		return;
	}

	auto serversNode = element.child("RecentServers");
	if (!serversNode) {
		serversNode = element.append_child("RecentServers");
	}

	for (auto const& site : sites) {
		auto node = serversNode.append_child("Server");
		SetServer(node, site);
	}

	xmlFile.Save(true);
}

void CRecentServerList::Clear()
{
	CInterProcessMutex mutex(MUTEX_MOSTRECENTSERVERS);

	CXmlFile xmlFile(wxGetApp().GetSettingsFile(_T("recentservers")));
	xmlFile.CreateEmpty();
	xmlFile.Save(true);
}
