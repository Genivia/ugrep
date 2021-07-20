noinst_LIBRARIES      = libreflex.a
libreflex_a_CPPFLAGS  = -I$(top_srcdir)/include $(SIMD_FLAGS)
libreflex_a_SOURCES   = $(top_srcdir)/include/reflex/abslexer.h $(top_srcdir)/include/reflex/absmatcher.h $(top_srcdir)/include/reflex/bits.h $(top_srcdir)/include/reflex/boostmatcher.h $(top_srcdir)/include/reflex/convert.h $(top_srcdir)/include/reflex/debug.h $(top_srcdir)/include/reflex/error.h $(top_srcdir)/include/reflex/flexlexer.h $(top_srcdir)/include/reflex/input.h $(top_srcdir)/include/reflex/matcher.h $(top_srcdir)/include/reflex/pattern.h $(top_srcdir)/include/reflex/posix.h $(top_srcdir)/include/reflex/ranges.h $(top_srcdir)/include/reflex/setop.h $(top_srcdir)/include/reflex/stdmatcher.h $(top_srcdir)/include/reflex/timer.h $(top_srcdir)/include/reflex/traits.h $(top_srcdir)/include/reflex/unicode.h $(top_srcdir)/include/reflex/utf8.h convert.cpp debug.cpp error.cpp input.cpp matcher.cpp pattern.cpp posix.cpp simd_avx2.cpp simd_avx512bw.cpp unicode.cpp utf8.cpp block_scripts.cpp language_scripts.cpp letter_scripts.cpp

libreflex_a-simd_avx2.$(OBJEXT) : CXXFLAGS += $(SIMD_AVX2_FLAGS)
libreflex_a-simd_avx512bw.$(OBJEXT) : CXXFLAGS += $(SIMD_AVX512BW_FLAGS)
