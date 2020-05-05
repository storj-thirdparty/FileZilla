#ifndef FILEZILLA_ENGINE_ENGINE_HEADER
#define FILEZILLA_ENGINE_ENGINE_HEADER

#include "commands.h"

#include <notification.h>

class CAsyncRequestNotification;
class CFileZillaEngineContext;
class CFileZillaEnginePrivate;
class CNotification;
class EngineNotificationHandler;

class CFileZillaEngine final
{
public:
	CFileZillaEngine(CFileZillaEngineContext& engine_context, EngineNotificationHandler& notificationHandler);
	~CFileZillaEngine();

	CFileZillaEngine(CFileZillaEngine const&) = delete;
	CFileZillaEngine& operator=(CFileZillaEngine const&) = delete;

	// Execute the given command. See commands.h for a list of the available
	// commands and reply codes.
	int Execute(CCommand const& command);

	// Cancels the current command
	int Cancel();

	bool IsBusy() const;
	bool IsConnected() const;

	// IsActive returns true only if data has been transferred in the
	// given direction since the last time IsActive was called with
	// the same argument.
	enum _direction
	{
		send,
		recv
	};
	static bool IsActive(_direction direction);

	// Returns the next pending notification.
	// It is mandatory to call this function until it returns a nullptr each time you
	// get the pending notifications event, or you'll either lose notifications
	// or your memory will fill with pending notifications.
	// See notification.h for details.
	std::unique_ptr<CNotification> GetNextNotification();

	// Sets the reply to an async request, e.g. a file exists request.
	// See notifiction.h for details.
	bool IsPendingAsyncRequestReply(std::unique_ptr<CAsyncRequestNotification> const& pNotification);

	// Sets the reply to the asynchronous request. Takes ownership of the pointer.
	bool SetAsyncRequestReply(std::unique_ptr<CAsyncRequestNotification> && pNotification);

	// Get a progress update about the current transfer. changed will be set
	// to true if the data has been updated compared to the last time
	// GetTransferStatus was called.
	CTransferStatus GetTransferStatus(bool &changed);

	int CacheLookup(CServerPath const& path, CDirectoryListing& listing);

private:
	CFileZillaEnginePrivate* const impl_;
};

#endif
