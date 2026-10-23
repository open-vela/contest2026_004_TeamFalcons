param([Parameter(Mandatory=$true)][string]$Cases,
      [Parameter(Mandatory=$true)][string]$OutDir)
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=[Text.Encoding]::UTF8
Add-Type -AssemblyName System.Speech
$items=Get-Content -LiteralPath $Cases -Raw -Encoding UTF8 | ConvertFrom-Json
if($items.Count -gt 30) { throw 'At most 30 synthetic samples per run' }
[IO.Directory]::CreateDirectory($OutDir) | Out-Null
$fmt=New-Object System.Speech.AudioFormat.SpeechAudioFormatInfo(16000,[System.Speech.AudioFormat.AudioBitsPerSample]::Sixteen,[System.Speech.AudioFormat.AudioChannel]::Mono)
foreach($item in $items) {
 if($item.id -notmatch '^[a-z]+[0-9]{2}$' -or $item.text.Length -gt 100) { throw 'Invalid sample' }
 $path=Join-Path $OutDir ($item.id+'.wav')
 $synth=New-Object System.Speech.Synthesis.SpeechSynthesizer
 try {
  $synth.SelectVoice('Microsoft Huihui Desktop')
  $synth.SetOutputToWaveFile($path,$fmt)
  $synth.Speak([string]$item.text)
 } finally { $synth.Dispose() }
 Write-Output ($item.id+' sha256='+(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash)
}
