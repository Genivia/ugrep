bin_PROGRAMS = ugrep ugrep-indexer
ugrep_CPPFLAGS = \
	-I$(top_srcdir)/include \
	$(EXTRA_CFLAGS) \
	$(SIMD_FLAGS) \
	$(PTHREAD_CFLAGS) \
	-DPLATFORM=\"$(PLATFORM)\" \
	-DGREP_PATH=\"$(GREP_PATH)\" \
	-DWITH_NO_INDENT
ugrep_SOURCES = \
	$(top_srcdir)/include/reflex/absmatcher.h \
	$(top_srcdir)/include/reflex/bits.h \
	$(top_srcdir)/include/reflex/convert.h \
	$(top_srcdir)/include/reflex/debug.h \
	$(top_srcdir)/include/reflex/error.h \
	$(top_srcdir)/include/reflex/fuzzymatcher.h \
	$(top_srcdir)/include/reflex/input.h \
	$(top_srcdir)/include/reflex/linematcher.h \
	$(top_srcdir)/include/reflex/matcher.h \
	$(top_srcdir)/include/reflex/pattern.h \
	$(top_srcdir)/include/reflex/pcre2matcher.h \
	$(top_srcdir)/include/reflex/posix.h \
	$(top_srcdir)/include/reflex/ranges.h \
	$(top_srcdir)/include/reflex/setop.h \
	$(top_srcdir)/include/reflex/simd.h \
	$(top_srcdir)/include/reflex/timer.h \
	$(top_srcdir)/include/reflex/traits.h \
	$(top_srcdir)/include/reflex/unicode.h \
	$(top_srcdir)/include/reflex/utf8.h \
	ugrep.cpp \
	cnf.hpp \
	cnf.cpp \
	flag.hpp \
	glob.hpp \
	glob.cpp \
	mmap.hpp \
	output.hpp \
	output.cpp \
	query.hpp \
	query.cpp \
	screen.hpp \
	screen.cpp \
	stats.hpp \
	stats.cpp \
	vkey.hpp \
	vkey.cpp \
	zstream.hpp \
	zopen.h \
	zopen.c
ugrep_LDADD = \
	$(PTHREAD_LIBS) \
	$(top_builddir)/lib/libreflex.a
ugrep_indexer_CPPFLAGS = \
	-I$(top_srcdir)/include \
	$(EXTRA_CFLAGS) \
	$(PTHREAD_CFLAGS) \
	-DPLATFORM=\"$(PLATFORM)\"
ugrep_indexer_SOURCES  = \
	$(top_srcdir)/include/reflex/input.h \
	$(top_srcdir)/include/reflex/utf8.h \
	$(top_srcdir)/lib/input.cpp \
	ugrep-indexer.cpp \
	glob.hpp \
	glob.cpp \
	zopen.h \
	zopen.c \
	zstream.hpp \
	zthread.hpp
ugrep_indexer_LDADD = \
	$(PTHREAD_LIBS)
