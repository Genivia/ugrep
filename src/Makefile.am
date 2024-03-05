bin_PROGRAMS   = ugrep
ugrep_CPPFLAGS = -I$(top_srcdir)/include $(EXTRA_CFLAGS) $(PTHREAD_CFLAGS) -DPLATFORM=\"$(PLATFORM)\" -DGREP_PATH=\"$(GREP_PATH)\" -DWITH_NO_INDENT
ugrep_SOURCES  = ugrep.cpp cnf.hpp cnf.cpp flag.hpp glob.hpp glob.cpp mmap.hpp output.hpp output.cpp query.hpp query.cpp screen.hpp screen.cpp stats.hpp stats.cpp vkey.hpp vkey.cpp zstream.hpp zopen.h zopen.c
ugrep_LDADD    = $(PTHREAD_LIBS) libreflex.a

if ENABLE_7ZIP
    ugrep_CPPFLAGS += -I${top_srcdir}/lzma/C -DWITH_7ZIP
    ugrep_LDADD += -L${top_builddir}/lzma/C -lviiz
endif

noinst_LIBRARIES      = libreflex.a
libreflex_a_CPPFLAGS  = -include $(CONFIG_HEADER) -I$(top_srcdir)/include -DWITH_NO_INDENT -DWITH_NO_CODEGEN
libreflex_a_CXXFLAGS  = $(SIMD_FLAGS)
libreflex_a_SOURCES   = \
        ../lib/convert.cpp \
        ../lib/debug.cpp \
        ../lib/error.cpp \
        ../lib/input.cpp \
        ../lib/matcher.cpp \
        ../lib/pattern.cpp \
        ../lib/posix.cpp \
        ../lib/unicode.cpp \
        ../lib/utf8.cpp \
        ../lib/block_scripts.cpp \
        ../lib/language_scripts.cpp \
        ../lib/letter_scripts.cpp \
        ../lib/composer.cpp

if ENABLE_SIMD
    libreflex_a_SOURCES   += \
        ../lib/matcher_sse2.cpp
endif
if ENABLE_AVX2
    libreflex_a_SOURCES   += \
        ../lib/matcher_avx2.cpp \
        ../lib/simd_avx2.cpp
else
if ENABLE_SIMD
# simd_avx2.cpp contains SSE2 implementation.
# Add it even if AVX2 is diabled, but SSE2 enabled
    libreflex_a_SOURCES   += \
        ../lib/simd_avx2.cpp
endif
endif
if ENABLE_AVX512_BW
    libreflex_a_SOURCES   += \
        ../lib/matcher_avx512bw.cpp \
        ../lib/simd_avx512bw.cpp
endif

# Tell automake to package these into the tarball
# FIXME: move these files to this directory, they belong with the source code.
# FIXME: Include them with "quotes", not angle <brackets>
noinst_HEADERS = \
        $(top_srcdir)/include/reflex/abslexer.h \
        $(top_srcdir)/include/reflex/absmatcher.h \
        $(top_srcdir)/include/reflex/bits.h \
        $(top_srcdir)/include/reflex/boostmatcher.h \
        $(top_srcdir)/include/reflex/convert.h \
        $(top_srcdir)/include/reflex/debug.h \
        $(top_srcdir)/include/reflex/error.h \
        $(top_srcdir)/include/reflex/flexlexer.h \
        $(top_srcdir)/include/reflex/input.h \
        $(top_srcdir)/include/reflex/matcher.h \
        $(top_srcdir)/include/reflex/pattern.h \
        $(top_srcdir)/include/reflex/posix.h \
        $(top_srcdir)/include/reflex/ranges.h \
        $(top_srcdir)/include/reflex/setop.h \
        $(top_srcdir)/include/reflex/simd.h \
        $(top_srcdir)/include/reflex/stdmatcher.h \
        $(top_srcdir)/include/reflex/timer.h \
        $(top_srcdir)/include/reflex/traits.h \
        $(top_srcdir)/include/reflex/unicode.h \
        $(top_srcdir)/include/reflex/utf8.h
