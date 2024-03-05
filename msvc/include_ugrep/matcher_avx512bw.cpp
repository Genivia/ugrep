#if defined(HAVE_AVX512_BW)

#if !defined(__AVX512BW__)
#error matcher_avx512bw.cpp must be compiled with /arch:avx512.
#endif

#define COMPILE_AVX512BW
#include "../../lib/matcher.cpp"

#endif // HAVE_AVX512_BW
