#!/usr/bin/env python3
"""Inspect a WAV file or a headerless PCM file and report decodability facts."""
import argparse
import array
import sys
import wave


def stats(samples):
    if not samples:
        return 0, 0, 0.0
    peak = max(abs(s) for s in samples)
    rms = (sum(s * s for s in samples) / len(samples)) ** 0.5
    zc = sum(1 for i in range(1, len(samples)) if (samples[i - 1] < 0) != (samples[i] < 0))
    return peak, round(rms, 1), round(zc / len(samples) * 100, 2)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--pcm", action="store_true", help="headerless PCM16LE")
    ap.add_argument("--rate", type=int, default=24000)
    ap.add_argument("--channels", type=int, default=1)
    args = ap.parse_args()

    raw = open(args.path, "rb").read()
    if args.pcm:
        sr, ch, sw = args.rate, args.channels, 2
        data = raw
        print(f"{args.path}: headerless PCM16LE assumed {sr}Hz {ch}ch, {len(raw)} bytes, {len(raw)/(sr*ch*sw):.3f}s")
    else:
        with wave.open(args.path, "rb") as w:
            ch, sr, sw, n = w.getnchannels(), w.getframerate(), w.getsampwidth(), w.getnframes()
            data = w.readframes(n)
        print(f"{args.path}: WAV {sr}Hz {ch}ch {sw*8}bit, {len(raw)} bytes, {n/sr:.3f}s, header={raw[:4]!r}/{raw[8:12]!r}")

    a = array.array("h")
    a.frombytes(data[: len(data) // 2 * 2])
    if sys.byteorder == "big":
        a.byteswap()
    mono = list(a[::ch]) if ch > 1 else list(a)
    peak, rms, zcr = stats(mono)
    print(f"  decoded_samples={len(mono)} peak={peak} rms={rms} zcr_percent={zcr}")


if __name__ == "__main__":
    main()
