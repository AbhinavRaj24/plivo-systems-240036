# Experiment log

All experiments used a 30-second stream (1,500 frames), the supplied harness,
and `make`-built C++ binaries.

| Version | Profile | Seed | Delay | Misses | Miss rate | Overhead | Change / conclusion |
|---|---|---:|---:|---:|---:|---:|---|
| Naive pass-through | A | 1 | 100 ms | 34 | 2.27% | 1.02x | C++ port and UDP wiring worked, but every dropped media packet was lost. |
| DATA protocol only | A | 1 | 100 ms | 34 | 2.27% | 1.03x | The 5-byte custom header serialized and decoded correctly; all misses matched relay drops. |
| Pairwise XOR FEC | A | 1 | 120 ms | 3 | 0.20% | 1.55x | First valid run; one parity packet per two frames recovered isolated losses. |
| Pairwise XOR FEC | B | 1 | 120 ms | 12 | 0.80% | 1.55x | Valid on the harsher visible profile. |
| Pairwise XOR FEC | B | 2 | 120 ms | 8 | 0.53% | 1.55x | Additional-seed validation passed. |
| Pairwise XOR FEC | B | 3 | 120 ms | 7 | 0.47% | 1.55x | Additional-seed validation passed. |
| Pairwise XOR FEC | B | 1 | 100 ms | 12 | 0.80% | 1.55x | Lower-delay candidate remained valid. |
| Pairwise XOR FEC | B | 2 | 100 ms | 11 | 0.73% | 1.55x | Lower-delay candidate remained valid on a second seed. |
| Pairwise XOR FEC | A | 1 | 100 ms | 3 | 0.20% | 1.55x | Final visible-profile confirmation at the selected delay. |
| Pairwise XOR FEC | A | 1 | 80 ms | 3 | 0.20% | 1.55x | Mild profile passed, but this did not establish the harsher-profile limit. |
| Pairwise XOR FEC | B | 1 | 80 ms | 34 | 2.27% | 1.55x | Invalid: parity for the first pair member is generated 20 ms later. |
| Pairwise XOR FEC | B | 1 | 70 ms | 179 | 11.93% | 1.55x | Invalid: profile B itself can delay originals by up to 80 ms. |
| Selective duplication | B | 1 | 80 ms | 14 | 0.93% | 2.00x | Immediate redundancy removed parity latency, but one frame per block was exposed. |
| Hybrid duplicate + sparse XOR | B | 1 | 80 ms | 13 | 0.87% | 2.00x | Used the same packet budget while protecting the exposed frame. |
| Hybrid duplicate + sparse XOR | B | 2 | 80 ms | 6 | 0.40% | 2.00x | Valid multi-seed confirmation. |
| Hybrid duplicate + sparse XOR | B | 3 | 80 ms | 7 | 0.47% | 2.00x | Valid multi-seed confirmation. |
| Hybrid duplicate + sparse XOR | B | 4 | 80 ms | 3 | 0.20% | 2.00x | Valid multi-seed confirmation. |
| Hybrid duplicate + sparse XOR | B | 5 | 80 ms | 7 | 0.47% | 2.00x | Valid multi-seed confirmation. |
| Hybrid duplicate + sparse XOR | A | 1 | 80 ms | 2 | 0.13% | 2.00x | Valid mild-profile confirmation. |
| Hybrid duplicate + sparse XOR | B | 1 | 75 ms | 46 | 3.07% | 2.00x | Invalid; 80 ms is the measured visible-profile boundary. |
| (32,18) GF(256) erasure code | A | 1 | 70 ms | 0 | 0.00% | 2.00x | Fragment-level coding eliminated whole-packet deadline failures. |
| (32,18) GF(256) erasure code | B | 1 | 70 ms | 5 | 0.33% | 2.00x | Valid with substantial margin. |
| (32,18) GF(256) erasure code | B | 2 | 70 ms | 2 | 0.13% | 2.00x | Valid multi-seed confirmation. |
| (32,18) GF(256) erasure code | B | 3 | 70 ms | 4 | 0.27% | 2.00x | Valid multi-seed confirmation. |
| (32,18) GF(256) erasure code | B | 4 | 70 ms | 2 | 0.13% | 2.00x | Valid multi-seed confirmation. |
| (32,18) GF(256) erasure code | B | 5 | 70 ms | 0 | 0.00% | 2.00x | Valid multi-seed confirmation. |
| (32,18) GF(256) erasure code | B | 1 | 68 ms | 12 | 0.80% | 2.00x | Valid, but closer to the hidden-profile limit. |
| (32,18) GF(256) erasure code | B | 2 | 68 ms | 10 | 0.67% | 2.00x | Valid lower-delay probe. |
| (32,18) GF(256) erasure code | B | 3 | 68 ms | 12 | 0.80% | 2.00x | Valid lower-delay probe. |
| (32,18) GF(256) erasure code | B | 4 | 68 ms | 13 | 0.87% | 2.00x | Valid, but only two misses below the cap. |
| (32,18) GF(256) erasure code | B | 5 | 68 ms | 5 | 0.33% | 2.00x | Valid lower-delay probe. |
| (32,18) GF(256) erasure code | B | 1 | 65 ms | 46 | 3.07% | 2.00x | Invalid; too few fragments arrive before this deadline. |

The selected grading configuration is `--delay_ms 70`.  The erasure-coded
design sends exactly 480,000 relay bytes for 240,000 raw payload bytes =
2.0x.  Feedback is omitted because the forward erasure code uses the complete
byte budget and avoids a hostile-network round trip.
