noinst_LIBRARIES      = libreflex.a
libreflex_a_CPPFLAGS  = -include $(CONFIG_HEADER) -I$(top_srcdir)/include -DWITH_NO_INDENT -DWITH_NO_CODEGEN
libreflex_a_CXXFLAGS  = $(SIMD_FLAGS)
libreflex_a_SOURCES   = \
        convert.cpp \
        debug.cpp \
        error.cpp \
        input.cpp \
        matcher.cpp \
        pattern.cpp \
        posix.cpp \
        unicode.cpp \
        utf8.cpp \
        block_scripts.cpp \
        language_scripts.cpp \
        letter_scripts.cpp \
        composer.cpp

if ENABLE_AVX2
    libreflex_a_SOURCES   += \
        matcher_avx2.cpp \
        simd_avx2.cpp
endif
if ENABLE_AVX512_BW
    libreflex_a_SOURCES   += \
        matcher_avx512bw.cpp \
        simd_avx512bw.cpp
endif

# Tell automake to package these into the tarball
# FIXME: move these files to this directory, they belong with the source code.
# FIXME: Include them with "quotes", not angle <brackets>
noinst_INCLUDES = \
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
