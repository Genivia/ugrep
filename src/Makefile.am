bin_PROGRAMS   = ugrep
ugrep_CPPFLAGS = -I$(top_srcdir)/include $(EXTRA_CFLAGS) $(SIMD_FLAGS) $(PTHREAD_CFLAGS) -DPLATFORM=\"$(PLATFORM)\" -DGREP_PATH=\"$(GREP_PATH)\" -DWITH_NO_INDENT
ugrep_SOURCES  = ugrep.cpp cnf.hpp cnf.cpp flag.hpp glob.hpp glob.cpp mmap.hpp output.hpp output.cpp query.hpp query.cpp screen.hpp screen.cpp stats.hpp stats.cpp vkey.hpp vkey.cpp zstream.hpp zopen.h zopen.c
ugrep_LDADD    = $(PTHREAD_LIBS) $(top_builddir)/lib/libreflex.a

if ENABLE_7ZIP
    ugrep_CPPFLAGS += -I${top_srcdir}/lzma/C -DWITH_7ZIP
    ugrep_LDADD += -L${top_builddir}/lzma/C -lviiz
endif
