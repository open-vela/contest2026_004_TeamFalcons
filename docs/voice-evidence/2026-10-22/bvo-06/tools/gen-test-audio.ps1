# Generate synthetic Mandarin test audio with the Windows SAPI zh-CN voice.
# Output: 16000 Hz, 16-bit, mono PCM WAV. No personal recordings are used.
param(
    [Parameter(Mandatory=$true)][string]$Text,
    [Parameter(Mandatory=$true)][string]$OutFile,
    [int]$Rate = 0
)
Add-Type -AssemblyName System.Speech
$fmt = New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(16000, [System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen, [System.Speech.AudioFormat.AudioChannel]::Mono)
$synth = New-Object System.Speech.Synthesis.SpeechSynthesizer
$synth.SelectVoice('Microsoft Huihui Desktop')
$synth.Rate = $Rate
$synth.SetOutputToWaveFile($OutFile, $fmt)
$synth.Speak($Text)
$synth.Dispose()
Write-Output ("wrote " + $OutFile)
