# Candidate domain for probability-only removal of MODE toggles

Only cw_pack8 Site4 (normalized positive softmax probabilities) changes. No activation, feature, input or output pack bypasses saturation.

The exponent bit-map clamps to positive normal half values [2^-14,9.75]. A denominator is a sum of64 nonnegative exponents, accumulated with the existing ones-WMMA order, and includes each numerator. No subtraction/cancellation occurs; finite positive sums stay in [1/256,624]. The original bounded reciprocal helper is retained. Therefore the intended normalized values remain close to [0,1], far from the FP8 saturation boundary448. All multiplication/rounding and converters are retained; only the overflow-saturation MODE wrapper is bypassed at this site. This is a domain argument, not a claim that arbitrary site inputs are safe.

Acceptance still requires full-network Gold and runtime correctness on actual code, since compiler scheduling across previously volatile MODE regions changes.
