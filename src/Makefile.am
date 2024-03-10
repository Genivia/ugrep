CLEANFILES =
dist_noinst_SCRIPTS =

# ############################################################
#                       UGREP Program                        #
# ############################################################

bin_PROGRAMS   = ugrep
ugrep_CPPFLAGS = -I$(top_srcdir)/include $(EXTRA_CFLAGS) $(PTHREAD_CFLAGS) -DPLATFORM=\"$(PLATFORM)\" -DGREP_PATH=\"$(GREP_PATH)\" -DWITH_NO_INDENT
ugrep_SOURCES  = \
        ugrep.cpp \
        cnf.cpp \
        glob.cpp \
        output.cpp \
        query.hpp \
        query.cpp \
        screen.cpp \
        stats.hpp \
        stats.cpp \
        vkey.cpp \
        zopen.c
ugrep_LDADD    = $(PTHREAD_LIBS) libreflex.a

noinst_HEADERS = \
        ugrep.hpp \
        cnf.hpp \
        flag.hpp \
        glob.hpp \
        mmap.hpp \
        output.hpp \
        screen.hpp \
        vkey.hpp \
        zstream.hpp \
        zopen.h

# Try installing hardlinks on filesystems that support it, otherwise
# install stripped copies using the install program
install-exec-hook:
	( cd $(DESTDIR)$(bindir) && \
		ln ugrep$(EXEEXT) ugrep+$(EXEEXT) && \
		ln ugrep$(EXEEXT) ug$(EXEEXT) && \
		ln ugrep$(EXEEXT) ug+$(EXEEXT)) || \
	( \
		$(INSTALL) ugrep$(EXEEXT) "$(bindir)/ugrep+" && \
		$(INSTALL) ugrep$(EXEEXT) "$(bindir)/ug$(EXEEXT)" && \
		$(INSTALL) ugrep$(EXEEXT) "$(bindir)/ug+" )

uninstall-hook:
	rm -f \
	        $(DESTDIR)$(bindir)/ug$(EXEEXT) \
		$(DESTDIR)$(bindir)/ug+ \
	        $(DESTDIR)$(bindir)/ugrep+

# ############################################################
#                       Shell completions                    #
# ############################################################

if ENABLE_FISH_COMPLETION
fishcompletiondir = $(FISH_COMPLETION_DIR)
fishcompletion_DATA = \
	ugrep.fish \
	ug.fish \
	ugrep+.fish \
	ug+.fish
CLEANFILES += $(fishcompletion_DATA)
endif

if ENABLE_ZSH_COMPLETION
zshcompletiondir = $(ZSH_COMPLETION_DIR)
zshcompletion_DATA = \
	_ugrep \
	_ug \
	_ugrep+ \
	_ug+
CLEANFILES += $(zshcompletion_DATA)
endif

# This is a GNU-make feature
.DELETE_ON_ERROR:

dist_noinst_SCRIPTS += $(top_srcdir)/completions/fish/compgen
$(fishcompletion_DATA): ugrep
	target=$@ ; cmdname=$${target##*/} ; cmdname=$${cmdname%%.fish} ; \
	$(top_srcdir)/completions/fish/compgen ./$< $$cmdname > $@

dist_noinst_SCRIPTS += $(top_srcdir)/completions/zsh/compgen
$(zshcompletion_DATA): ugrep
	target=$@ ; cmdname=$${target##*/_} ; \
	$(top_srcdir)/completions/zsh/compgen ./$< $$cmdname > $@

# ############################################################
#                       RE/Flex Library                      #
# ############################################################

noinst_LIBRARIES      = libreflex.a
libreflex_a_CPPFLAGS  = -include $(CONFIG_HEADER) -I$(top_srcdir)/include -DWITH_NO_INDENT -DWITH_NO_CODEGEN
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
libreflex_a_LIBADD =

if ENABLE_SSE2
noinst_LIBRARIES      += libreflex-sse2.a
libreflex_sse2_a_CPPFLAGS  = $(libreflex_a_CPPFLAGS)
libreflex_sse2_a_CXXFLAGS  = $(SIMD_FLAGS_SSE2)
libreflex_sse2_a_SOURCES   = \
        ../lib/matcher_sse2.cpp
libreflex_a_LIBADD += $(libreflex_sse2_a_OBJECTS)
endif

if ENABLE_AVX2
noinst_LIBRARIES      += libreflex-avx2.a
libreflex_avx2_a_CPPFLAGS  = $(libreflex_a_CPPFLAGS)
libreflex_avx2_a_CXXFLAGS  = $(SIMD_FLAGS_AVX2)
libreflex_avx2_a_SOURCES   = \
        ../lib/matcher_avx2.cpp \
        ../lib/simd_avx2.cpp
libreflex_a_LIBADD += $(libreflex_avx2_a_OBJECTS)
endif

if ENABLE_AVX512_BW
noinst_LIBRARIES      += libreflex-avx512bw.a
libreflex_avx512bw_a_CPPFLAGS  = $(libreflex_a_CPPFLAGS)
libreflex_avx512bw_a_CXXFLAGS  = $(SIMD_FLAGS_AVX512BW)
libreflex_avx512bw_a_SOURCES   = \
        ../lib/matcher_avx512bw.cpp \
        ../lib/simd_avx512bw.cpp
libreflex_a_LIBADD += $(libreflex_avx512bw_a_OBJECTS)
endif

if ENABLE_NEON
noinst_LIBRARIES      += libreflex-neon.a
libreflex_neon_a_CPPFLAGS  = $(libreflex_a_CPPFLAGS)
libreflex_neon_a_CXXFLAGS  = $(SIMD_FLAGS_NEON)
libreflex_neon_a_SOURCES   = \
        ../lib/matcher_neon.cpp
libreflex_a_LIBADD += $(libreflex_neon_a_OBJECTS)
endif

# Tell automake to package these into the tarball
# FIXME: move these files to this directory, they belong with the source code.
# FIXME: Include them with "quotes", not angle <brackets>
noinst_HEADERS += \
        $(top_srcdir)/include/reflex/abslexer.h \
        $(top_srcdir)/include/reflex/absmatcher.h \
        $(top_srcdir)/include/reflex/bits.h \
        $(top_srcdir)/include/reflex/boostmatcher.h \
        $(top_srcdir)/include/reflex/convert.h \
        $(top_srcdir)/include/reflex/debug.h \
        $(top_srcdir)/include/reflex/error.h \
        $(top_srcdir)/include/reflex/flexlexer.h \
        $(top_srcdir)/include/reflex/fuzzymatcher.h \
        $(top_srcdir)/include/reflex/input.h \
        $(top_srcdir)/include/reflex/linematcher.h \
        $(top_srcdir)/include/reflex/matcher.h \
        $(top_srcdir)/include/reflex/pattern.h \
        $(top_srcdir)/include/reflex/posix.h \
        $(top_srcdir)/include/reflex/pcre2matcher.h \
        $(top_srcdir)/include/reflex/ranges.h \
        $(top_srcdir)/include/reflex/setop.h \
        $(top_srcdir)/include/reflex/simd.h \
        $(top_srcdir)/include/reflex/stdmatcher.h \
        $(top_srcdir)/include/reflex/timer.h \
        $(top_srcdir)/include/reflex/traits.h \
        $(top_srcdir)/include/reflex/unicode.h \
        $(top_srcdir)/include/reflex/utf8.h


# ############################################################
#                         7-zip Library                      #
# ############################################################

if ENABLE_7ZIP
    ugrep_CPPFLAGS += -I${top_srcdir}/lzma/C -DWITH_7ZIP
    ugrep_LDADD += libviiz.a
    noinst_LIBRARIES   += libviiz.a
endif

libviiz_a_CPPFLAGS    = \
        -DZ7_PPMD_SUPPORT \
        -DZ7_EXTRACT_ONLY \
        -DNDEBUG \
        -D_REENTRANT \
        -D_FILE_OFFSET_BITS=64 \
        -D_LARGEFILE_SOURCE
libviiz_a_SOURCES     = \
        ../lzma/C/viizip.c \
        ../lzma/C/7zAlloc.c \
        ../lzma/C/7zArcIn.c \
        ../lzma/C/7zBuf.c \
        ../lzma/C/7zBuf2.c \
        ../lzma/C/7zCrc.c \
        ../lzma/C/7zCrcOpt.c \
        ../lzma/C/7zDec.c \
        ../lzma/C/7zFile.c \
        ../lzma/C/7zStream.c \
        ../lzma/C/Bcj2.c \
        ../lzma/C/Bra.c \
        ../lzma/C/Bra86.c \
        ../lzma/C/BraIA64.c \
        ../lzma/C/CpuArch.c \
        ../lzma/C/Delta.c \
        ../lzma/C/Lzma2Dec.c \
        ../lzma/C/LzmaDec.c \
        ../lzma/C/Ppmd7.c \
        ../lzma/C/Ppmd7Dec.c

noinst_HEADERS += \
        $(top_srcdir)/lzma/C/viizip.h \
        $(top_srcdir)/lzma/C/7z.h \
        $(top_srcdir)/lzma/C/7zAlloc.h \
        $(top_srcdir)/lzma/C/7zBuf.h \
        $(top_srcdir)/lzma/C/7zCrc.h \
        $(top_srcdir)/lzma/C/7zFile.h \
        $(top_srcdir)/lzma/C/7zTypes.h \
        $(top_srcdir)/lzma/C/Bcj2.h \
        $(top_srcdir)/lzma/C/Bra.h \
        $(top_srcdir)/lzma/C/Compiler.h \
        $(top_srcdir)/lzma/C/CpuArch.h \
        $(top_srcdir)/lzma/C/Delta.h \
        $(top_srcdir)/lzma/C/Lzma2Dec.h \
        $(top_srcdir)/lzma/C/LzmaDec.h \
        $(top_srcdir)/lzma/C/Ppmd.h \
        $(top_srcdir)/lzma/C/Ppmd7.h \
        $(top_srcdir)/lzma/C/Precomp.h \
        $(top_srcdir)/lzma/C/RotateDefs.h
