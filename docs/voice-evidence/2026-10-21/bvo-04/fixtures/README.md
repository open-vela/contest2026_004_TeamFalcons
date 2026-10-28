BVO-04 fixtures: WAV PCM signed 16-bit little endian, 16000 Hz, stereo L=R,
16000 frames / 64000 payload bytes / 64044 file bytes / 1 second each.
Tone: generated 1 kHz sine, peak 2047, -24.09 dBFS.
Speech: existing TFLM micro_speech/testdata/yes_1000ms.wav, mono samples divided
by four (truncated toward zero), duplicated into left and right channels.
Source WAV SHA256: b40e137a9d25bbdf135e03fa0f6bff4696d0bfc203aef8bc73b27c9428ba5a92
Source repository: https://github.com/tensorflow/tflite-micro (Apache-2.0).
Actual local source checkout: e2ebfbfe176f422628b85ab5579df3b862d99fca
Reproduce: python3 scripts/gen_audio_fixtures.py
