#!/usr/bin/env python3
"""Normalize a PCM WAV to an exact duration and/or a different rate/channel count.

Uses only the Python standard library. Linear interpolation is used for rate
conversion; this is adequate for ASR input checks and is not a production
resampler.
"""
import argparse
import sys
import wave


def read_wav(path):
    with wave.open(path, "rb") as w:
        if w.getsampwidth() != 2:
            raise SystemExit("only 16-bit PCM is supported")
        ch, sr, n = w.getnchannels(), w.getframerate(), w.getnframes()
        frames = w.readframes(n)
    # split into per-channel int16 lists
    total = len(frames) // 2
    import array

    a = array.array("h")
    a.frombytes(frames)
    if sys.byteorder == "big":
        a.byteswap()
    if ch == 1:
        return [list(a)], sr
    chans = [[] for _ in range(ch)]
    for i in range(total):
        chans[i % ch].append(a[i])
    return chans, sr


def resample(samples, sr_in, sr_out):
    if sr_in == sr_out or not samples:
        return samples
    n_out = int(round(len(samples) * sr_out / sr_in))
    out = []
    step = (len(samples) - 1) / max(1, n_out - 1) if n_out > 1 else 0.0
    for i in range(n_out):
        pos = i * step
        i0 = int(pos)
        i1 = min(i0 + 1, len(samples) - 1)
        frac = pos - i0
        out.append(int(round(samples[i0] * (1 - frac) + samples[i1] * frac)))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--seconds", type=float)
    ap.add_argument("--rate", type=int)
    ap.add_argument("--channels", type=int, choices=(1, 2))
    args = ap.parse_args()

    chans, sr = read_wav(args.src)
    if args.rate and args.rate != sr:
        chans = [resample(c, sr, args.rate) for c in chans]
        sr = args.rate

    if args.seconds is not None:
        target = int(round(args.seconds * sr))
        for i, c in enumerate(chans):
            if len(c) < target:
                c.extend([0] * (target - len(c)))
            elif len(c) > target:
                chans[i] = c[:target]

    out_ch = args.channels or len(chans)
    if out_ch == 2 and len(chans) == 1:
        chans = [chans[0], list(chans[0])]
    elif out_ch == 1 and len(chans) > 1:
        chans = [chans[0]]

    n = min(len(c) for c in chans)
    import array

    inter = array.array("h")
    for i in range(n):
        for c in chans:
            inter.append(c[i])
    if sys.byteorder == "big":
        inter.byteswap()

    with wave.open(args.dst, "wb") as w:
        w.setnchannels(len(chans))
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(inter.tobytes())
    print(f"{args.dst}: {len(chans)}ch {sr}Hz 16-bit {n / sr:.3f}s {n * len(chans) * 2} bytes")


if __name__ == "__main__":
    main()
