AUTOMAKE_OPTIONS = subdir-objects

noinst_LIBRARIES = libengine.a

libengine_a_CPPFLAGS = -I$(srcdir)/../include
libengine_a_CPPFLAGS += $(LIBFILEZILLA_CFLAGS)

libengine_a_SOURCES = \
		commands.cpp \
		controlsocket.cpp \
		directorycache.cpp \
		directorylisting.cpp \
		directorylistingparser.cpp \
		engine_context.cpp \
		engineprivate.cpp \
		externalipresolver.cpp \
		FileZillaEngine.cpp \
		ftp/chmod.cpp \
		ftp/cwd.cpp \
		ftp/delete.cpp \
		ftp/filetransfer.cpp \
		ftp/ftpcontrolsocket.cpp \
		ftp/list.cpp \
		ftp/logon.cpp \
		ftp/mkd.cpp \
		ftp/rawcommand.cpp \
		ftp/rawtransfer.cpp \
		ftp/rename.cpp \
		ftp/rmd.cpp \
		ftp/transfersocket.cpp \
		http/digest.cpp \
		http/filetransfer.cpp \
		http/httpcontrolsocket.cpp \
		http/internalconnect.cpp \
		http/request.cpp \
		iothread.cpp \
		local_path.cpp \
		logging.cpp \
		lookup.cpp \
		misc.cpp \
		notification.cpp \
		oplock_manager.cpp \
		option_change_event_handler.cpp \
		pathcache.cpp \
		proxy.cpp \
		rtt.cpp \
		server.cpp \
		servercapabilities.cpp \
		serverpath.cpp\
		sftp/chmod.cpp \
		sftp/connect.cpp \
		sftp/cwd.cpp \
		sftp/delete.cpp \
		sftp/filetransfer.cpp \
		sftp/input_thread.cpp \
		sftp/list.cpp \
		sftp/mkd.cpp \
		sftp/rename.cpp \
		sftp/rmd.cpp \
		sftp/sftpcontrolsocket.cpp \
		sizeformatting_base.cpp \
		xmlutils.cpp

noinst_HEADERS = \
		controlsocket.h \
		directorycache.h \
		directorylistingparser.h \
		engineprivate.h \
		filezilla.h \
		ftp/chmod.h \
		ftp/cwd.h \
		ftp/delete.h \
		ftp/filetransfer.h \
		ftp/ftpcontrolsocket.h \
		ftp/list.h \
		ftp/logon.h \
		ftp/mkd.h \
		ftp/rename.h \
		ftp/rawcommand.h \
		ftp/rawtransfer.h \
		ftp/rmd.h \
		ftp/transfersocket.h \
		http/connect.h \
		http/digest.h \
		http/filetransfer.h \
		http/httpcontrolsocket.h \
		http/internalconnect.h \
		http/request.h \
		iothread.h \
		logging_private.h \
		lookup.h \
		oplock_manager.h \
		pathcache.h \
		proxy.h \
		rtt.h \
		servercapabilities.h \
		sftp/chmod.h \
		sftp/connect.h \
		sftp/cwd.h \
		sftp/delete.h \
		sftp/event.h \
		sftp/filetransfer.h \
		sftp/input_thread.h \
		sftp/list.h \
		sftp/mkd.h \
		sftp/rename.h \
		sftp/rmd.h \
		sftp/sftpcontrolsocket.h

if ENABLE_STORJ
libengine_a_SOURCES += \
		storj/connect.cpp \
		storj/delete.cpp \
		storj/file_transfer.cpp \
		storj/input_thread.cpp \
		storj/list.cpp \
		storj/mkd.cpp \
		storj/resolve.cpp \
		storj/rmd.cpp \
		storj/storjcontrolsocket.cpp

noinst_HEADERS += \
		storj/connect.h \
		storj/delete.h \
		storj/event.h \
		storj/file_transfer.h \
		storj/input_thread.h \
		storj/list.h \
		storj/mkd.h \
		storj/resolve.h \
		storj/rmd.h \
		storj/storjcontrolsocket.h
endif

dist_noinst_DATA = engine.vcxproj

CLEANFILES = filezilla.h.gch
DISTCLEANFILES = ./$(DEPDIR)/filezilla.Po

if ENABLE_PRECOMP

BUILT_SOURCES = filezilla.h.gch

-include ./$(DEPDIR)/filezilla.Po

filezilla.h.gch: filezilla.h
	if $(CXX) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(libengine_a_CPPFLAGS) $(CPPFLAGS) $(libengine_a_CXXFLAGS) $(CXXFLAGS) -MT $@ -MD -MP -MF "$(DEPDIR)/filezilla.Tpo" -c -o $@ $<; \
	then mv -f "$(DEPDIR)/filezilla.Tpo" "$(DEPDIR)/filezilla.Po"; else rm -f "$(DEPDIR)/filezilla.Tpo"; exit 1; fi

endif

