param([Parameter(Mandatory=$true)][string]$Deck,
      [Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference='Stop'
# Cooperative mutex: another author can use the same name to serialize Office.
$guard=New-Object System.Threading.Mutex($false,'Local\VelaGuard.PresentationRender')
if (-not $guard.WaitOne(1000)) { throw 'Another presentation renderer holds the cooperative mutex.' }
$app=$null
$pres=$null
try {
    $app=New-Object -ComObject PowerPoint.Application
    $pres=$app.Presentations.Open($Deck,-1,0,0)
    $pres.Export($Output,'PNG',1600,900)
    Write-Output "CHATGPT render: $($pres.Slides.Count) slides"
} finally {
    if ($null -ne $pres) { $pres.Close(); [System.Runtime.InteropServices.Marshal]::ReleaseComObject($pres) | Out-Null }
    # Never close a shared PowerPoint application or another author's deck.
    if ($null -ne $app) { [System.Runtime.InteropServices.Marshal]::ReleaseComObject($app) | Out-Null }
    $guard.ReleaseMutex()
    $guard.Dispose()
}
