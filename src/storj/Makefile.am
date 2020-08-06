PREFIX_PATH = $(HOME)/prefix
STORJ_LIB = $(PREFIX_PATH)/lib

bin_PROGRAMS = fzstorj

fzstorj_SOURCES = \
		fzstorj.cpp

fzstorj_LDADD = $(LIBFILEZILLA_LIBS)

fzstorj_LDFLAGS = -L$(STORJ_LIB) -luplinkc

fzstorj_CPPFLAGS = $(LIBFILEZILLA_CFLAGS)

noinst_HEADERS = events.hpp require.h

if MACAPPBUNDLE
noinst_DATA = $(top_builddir)/FileZilla.app/Contents/MacOS/fzstorj$(EXEEXT)
endif

$(top_builddir)/FileZilla.app/Contents/MacOS/fzstorj$(EXEEXT): fzstorj
	mkdir -p $(top_builddir)/FileZilla.app/Contents/MacOS
	cp -f fzstorj $(top_builddir)/FileZilla.app/Contents/MacOS/fzstorj
