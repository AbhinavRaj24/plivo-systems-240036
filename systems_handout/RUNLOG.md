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

The selected grading configuration is `--delay_ms 100`.  I stopped before
adding feedback because the simple FEC design remained valid and feedback
would consume the remaining bandwidth margin while adding round-trip latency.
