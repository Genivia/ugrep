bin_PROGRAMS   = ugrep
ugrep_CPPFLAGS = -I$(top_srcdir)/include $(PTHREAD_CFLAGS) -DPLATFORM=\"$(PLATFORM)\" -DGREP_PATH=\"$(GREP_PATH)\"
ugrep_SOURCES  = ugrep.cpp glob.cpp glob.hpp zstream.hpp
ugrep_LDADD    = $(PTHREAD_LIBS) $(top_srcdir)/lib/libreflex.a
