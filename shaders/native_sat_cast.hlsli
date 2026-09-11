/* Saturating hardware E4M3 casts. dx::linalg's Cast<F8_E4M3FN>() does not saturate: a value beyond +-448 becomes NaN, one NaN token
   spreads over its attention window and the rgb head clamps it to 0 (the black 8x8 blocks on bright flat regions with Magpie's 8-bit
   input, DevHistory 09-11). The exact chain saturates in F() = min(...,448); SAT8 does the same before every hardware cast of an
   unbounded matrix (residual streams, V rows, attention/FFN outputs). Bit-identical on the reference fixture (nothing exceeds 448 there).
   -D NATIVE_SAT_CAST=0 restores the raw casts. */
#ifndef NATIVE_SAT_CAST
#define NATIVE_SAT_CAST 1
#endif
#if NATIVE_SAT_CAST
#define SAT8(m) for(uint _si=0;_si<(m).Length();_si++)(m).Set(_si,clamp((m).Get(_si),-448.0,448.0))
#else
#define SAT8(m)
#endif
