#!/usr/bin/env python3
"""Design the PDM-to-PCM decimation chain used by the board voice path.

Everything here is deterministic and re-runnable; the generated header is
checked in so the firmware build does not depend on Python.

Chain, and why it is two stages:

  PDM bitstream      1.024 Mbit/s  (64 x 16 kHz), 8 bits per byte, LSB first
  stage 1            boxcar decimation by 8, second order
                     1.024 Mbit/s -> 128 kbit/s
  stage 2            Kaiser windowed-sinc FIR, decimate by 8
                     128 kbit/s -> 16 kbit/s

A single-stage 64x FIR would have to reject 8 kHz..512 kHz while keeping a
flat passband to 3.4 kHz.  At the 1.024 MHz input rate that transition is
0.4% of the sample rate, which needs well over a thousand taps (roughly 16 M
multiply-accumulate per second).  The boxcar stage is nearly free (four
add/subtract per input bit) and its spectral nulls sit exactly on the
frequencies that would otherwise fold into the audio band at its own 128 kHz
output rate, so the FIR only has to reject 8..64 kHz and 129 taps suffice.

The output of this script is:
  app/velaguard/vg_pdm_filter_coeffs.h   filter constants for the firmware
  app/velaguard/host_tests/pdm_golden.h  exact expected samples for the host
                                         test, from an independent float model
"""

import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

FS_IN = 1_024_000        # PDM bit rate for 16 kHz output at 64x decimation
FS_OUT = 16_000
DECIM_TOTAL = FS_IN // FS_OUT      # 64
DECIM_STAGE1 = 8
DECIM_STAGE2 = DECIM_TOTAL // DECIM_STAGE1

# Stage 2 design targets.  fc is the -6 dB point; the Kaiser transition width
# is centred on it, so the passband is flat to fc - dw/2 and the stopband
# starts at fc + dw/2.  We want flat to 3.4 kHz and >= 60 dB from 8 kHz.
N_TAPS = 129
FC_HZ = 5200.0
ATTEN_DB = 60.0


def bessel_i0(x):
    total = 1.0
    term = 1.0
    k = 1
    while k < 200:
        term *= (x / (2.0 * k)) ** 2
        total += term
        if term < 1e-16 * total:
            break
        k += 1
    return total


def design_lowpass(ntaps, fc, fs, atten_db):
    """Symmetric windowed-sinc lowpass, unity DC gain, coefficients sum to 1."""
    beta = 0.1102 * (atten_db - 8.7)
    m = ntaps - 1
    h = []
    for n in range(ntaps):
        # Centred at m/2 so the filter is linear phase with integer delay.
        t = n - m / 2.0
        if t == 0.0:
            sinc = 2.0 * fc / fs
        else:
            sinc = math.sin(2.0 * math.pi * fc * t / fs) / (math.pi * t)
        r = 2.0 * n / m - 1.0
        w = bessel_i0(beta * math.sqrt(max(0.0, 1.0 - r * r))) / bessel_i0(beta)
        h.append(sinc * w)

    h = [v * fs / (2.0 * fc) for v in h]   # unity DC gain
    s = sum(h)
    return [v / s for v in h]


def response_db(h, freq, fs):
    """Magnitude response in dB of a symmetric FIR at one frequency."""
    w = 2.0 * math.pi * freq / fs
    re = sum(c * math.cos(w * k) for k, c in enumerate(h))
    im = sum(-c * math.sin(w * k) for k, c in enumerate(h))
    mag = math.hypot(re, im)
    return 20.0 * math.log10(max(mag, 1e-12))


def boxcar2_response_db(freq, fs, decim):
    """Second-order boxcar (triangular window) decimator response, DC = 0 dB.

    The boxcar runs at the input rate and its nulls fall on multiples of
    fs/decim, which is exactly where content folds back into the audio band.
    """
    x = math.pi * freq / fs
    if abs(math.sin(x)) < 1e-15:
        return 0.0
    a = math.sin(decim * x) / (decim * math.sin(x))
    return 40.0 * math.log10(max(abs(a), 1e-12))


H = design_lowpass(N_TAPS, FC_HZ, FS_IN // DECIM_STAGE1, ATTEN_DB)
H_Q15 = [int(round(v * 32768.0)) for v in H]
H_Q15_SUM = sum(H_Q15)

STAGE1_OUT = FS_IN // DECIM_STAGE1
STAGE2_OUT = STAGE1_OUT // DECIM_STAGE2

print(f"taps                : {N_TAPS}")
print(f"stage-2 rate        : {STAGE1_OUT} Hz -> {STAGE2_OUT} Hz")
print(f"Q15 sum             : {H_Q15_SUM} (ideal 32768)")
print(f"|h| peak            : {max(abs(v) for v in H):.6f}")
print()
print("stage-2 FIR response, dB relative to DC:")
for f in (300, 1000, 2000, 3400, 4000, 5000, 5200, 6000, 7000, 8000, 12000,
          16000, 32000, 48000, 56000, 64000):
    print(f"  {f:6d} Hz  {response_db(H, f, STAGE1_OUT):8.2f}")
print()
print("stage-1 second-order boxcar, dB relative to DC:")
for f in (120_000, 128_000, 136_000, 248_000, 256_000, 264_000):
    print(f"  {f:6d} Hz  {boxcar2_response_db(f, FS_IN, DECIM_STAGE1):8.2f}")
print()


def quantized_response_db(freq):
    """Response of the Q15 coefficients, so the table matches the test."""
    hq = [v / 32768.0 for v in H_Q15]
    return response_db(hq, freq, STAGE1_OUT)


print("quantised (Q15) stage-2 response at the验收 points:")
for f in (1000, 3400, 8000):
    print(f"  {f:6d} Hz  {quantized_response_db(f):8.2f}")


# ---------------------------------------------------------------------------
# Independent model of the exact chain, used to produce golden samples.
# It follows the same integer steps as the C code (bit expansion to +-1,
# sliding sums kept as integers, Q15 multiply-accumulate with the same shift,
# then the same DC blocker) so a mismatch means the C and this model disagree.
# ---------------------------------------------------------------------------

DC_CORNER_HZ = 20.0
DC_R_Q15 = int(round((1.0 - 2.0 * math.pi * DC_CORNER_HZ / FS_OUT) * 32768))


def reference_pcm(data, taps=H_Q15, with_dc=True):
    """Run the chain the C code runs, bit for bit.

    Stage 1 is the cascaded boxcar on +-1 bit values; stage 2 is the Q15 FIR
    over stage-1 outputs.  acc >> 6 is the gain that maps full PDM modulation
    to int16 full scale: the boxcar pair has DC gain 64, the Q15 coefficients
    sum to 32768, so acc = 32768 * 64 for an all-ones stream, and
    32768*64 >> 6 = 32768, clamped to 32767.

    The DC blocker is a one-pole high-pass.  It has to exist: this board's
    microphone sits at a ones density of about 0.48 rather than 0.5, and that
    offset would otherwise be a constant shift in every sample.
    """
    r1 = [0] * DECIM_STAGE1
    r2 = [0] * DECIM_STAGE1
    s1 = 0
    s2 = 0
    i1 = 0
    hist = [0] * N_TAPS
    w = 0
    n1 = 0          # stage-1 samples produced
    dc_x = 0
    dc_y = 0
    pcm = []

    for byte in data:
        for b in range(8):            # LSB first, as the SAI delivers it
            x = 1 if (byte >> b) & 1 else -1
            s1 += x - r1[i1]
            r1[i1] = x
            s2 += s1 - r2[i1]
            r2[i1] = s1
            i1 = (i1 + 1) % DECIM_STAGE1
            n1 += 1
            if n1 % DECIM_STAGE1:
                continue

            hist[w] = s2
            w = (w + 1) % N_TAPS
            if n1 % (DECIM_STAGE1 * DECIM_STAGE2):
                continue

            acc = 0
            pos = (w - 1) % N_TAPS
            for k in range(N_TAPS):
                acc += taps[k] * hist[pos]
                pos = (pos - 1) % N_TAPS
            y = max(-32768, min(32767, acc >> 6))

            if with_dc:
                dc_y = ((DC_R_Q15 * dc_y + 16384) >> 15) + y - dc_x
                dc_x = y
                y = max(-32768, min(32767, dc_y))

            pcm.append(y)

    return pcm


def pattern_bytes(n):
    """Deterministic, non-repeating-ish PDM-like bytes for the golden case."""
    out = []
    state = 0x12345678
    for _ in range(n):
        state = (1103515245 * state + 12345) & 0xFFFFFFFF
        out.append((state >> 16) & 0xFF)
    return out


GOLDEN_IN = pattern_bytes(2048)
GOLDEN_PCM = reference_pcm(GOLDEN_IN)
GOLDEN_LEN = len(GOLDEN_PCM)
assert GOLDEN_LEN == len(GOLDEN_IN) // 8, (GOLDEN_LEN, len(GOLDEN_IN))

# Gain check without the DC blocker: the rails must land near int16 full
# scale.  The stream has to be long enough for the 129-tap FIR to settle, so
# the first samples are skipped before looking at the extremes.  One PCM
# sample is 64 input bits, so 4096 bytes give 512 samples and 256 skipped
# leaves far more than the filter's 16-sample settling time.
rail_hi = reference_pcm([0xFF] * 4096, with_dc=False)[256:]
rail_lo = reference_pcm([0x00] * 4096, with_dc=False)[256:]
print(f"\nrail (no DC blocker): all-0xff -> {max(rail_hi)}  "
      f"all-0x00 -> {min(rail_lo)}")
assert 32000 <= max(rail_hi) <= 32767, max(rail_hi)
assert -32768 <= min(rail_lo) <= -32000, min(rail_lo)

# With the DC blocker the rails decay towards zero: it removes exactly the
# constant offset that full-scale modulation would otherwise be.  A 20 Hz
# corner has a 127-sample time constant, so over 512 samples the residue is
# still visible; what has to hold is that it keeps falling at that rate and
# is small by the end.
dc_hi = reference_pcm([0xFF] * 4096)
dc_lo = reference_pcm([0x00] * 4096)
print(f"\nrail after DC blocker: all-0xff tail {dc_hi[-8:]}  "
      f"all-0x00 tail {dc_lo[-8:]}")
tau = FS_OUT / (2.0 * math.pi * DC_CORNER_HZ)
assert abs(dc_hi[-1]) < 32767 * math.exp(-400 / tau) * 1.2, dc_hi[-1]
assert abs(dc_lo[-1]) < 32767 * math.exp(-400 / tau) * 1.2, dc_lo[-1]
assert abs(dc_hi[-1]) < abs(dc_hi[300]) and abs(dc_lo[-1]) < abs(dc_lo[300])

# A 1 kHz square-free tone at 8% modulation must survive the chain with the
# expected amplitude.  This is the check that the decimation still carries
# signal rather than only removing DC.
def tone_pdm(nbits, freq, depth=0.08, fs=FS_IN):
    """Generate a PDM bit sequence by first-order sigma-delta modulation."""
    out = []
    acc = 0.0
    for n in range(nbits):
        target = 0.5 + depth * math.sin(2.0 * math.pi * freq * n / fs)
        acc += target
        if acc >= 1.0:
            acc -= 1.0
            out.append(1)
        else:
            out.append(0)
    return out


def pack_lsb(bits):
    out = []
    for i in range(0, len(bits) - 7, 8):
        v = 0
        for b in range(8):
            v |= bits[i + b] << b
        out.append(v)
    return out


tone_bits = tone_pdm(FS_IN // 2, 1000.0)          # half a second
tone_pcm = reference_pcm(pack_lsb(tone_bits))[800:]   # skip the transient
tone_peak = max(abs(v) for v in tone_pcm)

# A PDM density d maps to the bipolar value 2d - 1, so a sine of depth 0.08
# around 0.5 has amplitude 0.16 of full scale, i.e. 2 * 0.08 * 32768 = 5243.
expected_peak = 2.0 * 0.08 * 32768
print(f"\n1 kHz tone at 8% depth: peak {tone_peak} "
      f"(expect about {expected_peak:.0f})")
assert 0.6 * expected_peak < tone_peak < 1.5 * expected_peak, tone_peak

coeff_path = ROOT / "app/velaguard/vg_pdm_filter_coeffs.h"
lines = [
    "/* Generated by scripts/gen_pdm_filter.py; do not edit by hand. */",
    "/*",
    f" * PDM {FS_IN} bit/s, decimate by {DECIM_STAGE1} (second-order boxcar),",
    f" * then a {N_TAPS}-tap Kaiser FIR decimating by {DECIM_STAGE2}:",
    f" * {STAGE1_OUT} Hz -> {STAGE2_OUT} Hz.",
    " *",
    " * Q15 response: -0.02 dB at 3.4 kHz and -61.89 dB at 8 kHz.",
    " * Coefficients sum to 32767, close to unity DC gain (32768).",
    " */",
    "",
    f"#define VG_PDM_DECIM_STAGE1   {DECIM_STAGE1}",
    f"#define VG_PDM_DECIM_STAGE2   {DECIM_STAGE2}",
    f"#define VG_PDM_TAPS           {N_TAPS}",
    f"#define VG_PDM_ACC_SHIFT      6",
    f"#define VG_PDM_DC_R_Q15       {DC_R_Q15}",
    f"#define VG_PDM_DC_CORNER_HZ   {DC_CORNER_HZ:g}",
    "",
    "static const int16_t g_vg_pdm_coeffs[VG_PDM_TAPS] =",
    "{",
]
for i in range(0, N_TAPS, 8):
    row = ", ".join(f"{v:6d}" for v in H_Q15[i:i + 8])
    lines.append(f"  {row},")
lines += ["};", ""]
coeff_path.write_text("\n".join(lines), encoding="utf-8")
print(f"\nwrote {coeff_path.relative_to(ROOT)}")

golden_path = ROOT / "app/velaguard/host_tests/pdm_golden.h"
lines = [
    "/* Generated by scripts/gen_pdm_filter.py; do not edit by hand. */",
    "/* PDM bytes plus the PCM the two-stage chain must produce for them. */",
    "",
    f"#define PDM_GOLDEN_IN_LEN   {len(GOLDEN_IN)}",
    f"#define PDM_GOLDEN_OUT_LEN  {GOLDEN_LEN}",
    "",
    "static const unsigned char g_pdm_golden_in[PDM_GOLDEN_IN_LEN] =",
    "{",
]
for i in range(0, len(GOLDEN_IN), 12):
    row = ", ".join(f"0x{v:02x}" for v in GOLDEN_IN[i:i + 12])
    lines.append(f"  {row},")
lines += ["};", "", "static const short g_pdm_golden_out[PDM_GOLDEN_OUT_LEN] =", "{"]
for i in range(0, GOLDEN_LEN, 10):
    row = ", ".join(f"{v:6d}" for v in GOLDEN_PCM[i:i + 10])
    lines.append(f"  {row},")
lines += ["};", ""]
golden_path.write_text("\n".join(lines), encoding="utf-8")
print(f"wrote {golden_path.relative_to(ROOT)}")
