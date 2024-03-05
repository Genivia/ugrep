#if defined(HAVE_AVX2) || defined(HAVE_AVX512_BW)

#if !defined(__AVX2__) || defined(__AVX512BW__)
#error matcher_avx2.cpp must be compiled with /arch:avx2.
#endif

#define COMPILE_AVX2
#include "../../lib/matcher.cpp"

#endif // HAVE_AVX2
