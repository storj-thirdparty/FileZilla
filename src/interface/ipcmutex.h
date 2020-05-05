#ifndef FILEZILLA_INTERFACE_IPCMUTEX_HEADER
#define FILEZILLA_INTERFACE_IPCMUTEX_HEADER

/*
 * Unfortunately wxWidgets does not provide interprocess mutexes, so I've to
 * use platform specific code here.
 * CInterProcessMutex represents an interprocess mutex. The mutex will be
 * created and locked in the constructor and released in the destructor.
 * Under Windows we use the normal Windows mutexes, under all other platforms
 * we use lockfiles using fcntl advisory locks.
 */

enum t_ipcMutexType
{
	// Important: Never ever change a value.
	// Otherwise this will cause interesting effects between different
	// versions of FileZilla
	MUTEX_OPTIONS = 1,
	MUTEX_SITEMANAGER = 2,
	MUTEX_SITEMANAGERGLOBAL = 3,
	MUTEX_QUEUE = 4,
	MUTEX_FILTERS = 5,
	MUTEX_LAYOUT = 6,
	MUTEX_MOSTRECENTSERVERS = 7,
	MUTEX_TRUSTEDCERTS = 8,
	MUTEX_GLOBALBOOKMARKS = 9,
	MUTEX_SEARCHCONDITIONS = 10,
	MUTEX_MAC_SANDBOX_USERDIRS = 11, // Only used if configured with --enable-mac-sandbox
	MUTEX_RESERVED = 12
};

class CInterProcessMutex final
{
public:
	CInterProcessMutex(t_ipcMutexType mutexType, bool initialLock = true);
	~CInterProcessMutex();

	bool Lock();
	int TryLock(); // Tries to lock the mutex. Returns 1 on success, 0 if held by other process, -1 on failure
	void Unlock();

	bool IsLocked() const { return m_locked; }

	t_ipcMutexType GetType() const { return m_type; }

private:

#ifdef __WXMSW__
	// Under windows use normal mutexes
	HANDLE hMutex;
#else
	// Use a lockfile under all other systems
	static int m_fd;
	static int m_instanceCount;
#endif
	t_ipcMutexType m_type;

	bool m_locked;
};

class CReentrantInterProcessMutexLocker final
{
public:
	CReentrantInterProcessMutexLocker(t_ipcMutexType mutexType);
	~CReentrantInterProcessMutexLocker();

protected:
	struct t_data final
	{
		CInterProcessMutex* pMutex;
		unsigned int lockCount;
	};
	static std::vector<t_data> m_mutexes;

	t_ipcMutexType m_type;
};

#endif
