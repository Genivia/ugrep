bin_PROGRAMS   = ugrep
ugrep_CPPFLAGS = -I$(top_srcdir)/include $(EXTRA_CFLAGS) $(PTHREAD_CFLAGS) -DPLATFORM=\"$(PLATFORM)\" -DGREP_PATH=\"$(GREP_PATH)\" -DWITH_NO_INDENT
ugrep_SOURCES  = ugrep.cpp glob.cpp glob.hpp zstream.hpp zopen.c zopen.h
ugrep_LDADD    = $(PTHREAD_LIBS) $(top_srcdir)/lib/libreflex.a
