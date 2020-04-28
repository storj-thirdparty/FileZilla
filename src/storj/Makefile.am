bin_PROGRAMS = fzstorj

fzstorj_SOURCES = \
		fzstorj.cpp

fzstorj_LDADD = $(LIBFILEZILLA_LIBS) libuplinkc.a

fzstorj_CPPFLAGS = $(LIBFILEZILLA_CFLAGS)

noinst_HEADERS = events.hpp uplink_definitions.h require.h libuplinkc.h

if MACAPPBUNDLE
noinst_DATA = $(top_builddir)/FileZilla.app/Contents/MacOS/fzstorj$(EXEEXT)
endif

$(top_builddir)/FileZilla.app/Contents/MacOS/fzstorj$(EXEEXT): fzstorj
	mkdir -p $(top_builddir)/FileZilla.app/Contents/MacOS
	cp -f fzstorj $(top_builddir)/FileZilla.app/Contents/MacOS/fzstorj
