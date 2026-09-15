param(
    [Parameter(Mandatory = $true)][string]$InputDocx,
    [Parameter(Mandatory = $true)][string]$OutputPdf
)

$ErrorActionPreference = 'Stop'
$inputPath = (Get-Item -LiteralPath $InputDocx).FullName
$outputPath = [System.IO.Path]::GetFullPath($OutputPdf)
if (Test-Path -LiteralPath $outputPath) {
    throw "PDF output must be a fresh path: $outputPath"
}
$outputDir = [System.IO.Path]::GetDirectoryName($outputPath)
if (-not (Test-Path -LiteralPath $outputDir -PathType Container)) {
    throw "PDF output directory does not exist: $outputDir"
}

$word = $null
$document = $null
try {
    $word = New-Object -ComObject Word.Application
    $word.Visible = $false
    $word.DisplayAlerts = 0
    $word.AutomationSecurity = 3
    $document = $word.Documents.Open($inputPath, $false, $true)
    [void]$document.Fields.Update()
    $document.Repaginate()
    $document.ExportAsFixedFormat($outputPath, 17)
    if (-not (Test-Path -LiteralPath $outputPath -PathType Leaf)) {
        throw 'Word did not produce a PDF'
    }
    if ((Get-Item -LiteralPath $outputPath).Length -lt 1000) {
        throw 'Word produced an empty or incomplete PDF'
    }
    Write-Output "PDF exported; pages=$($document.ComputeStatistics(2))"
}
finally {
    if ($null -ne $document) {
        $document.Close(0)
        [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($document)
    }
    if ($null -ne $word) {
        $word.Quit()
        [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($word)
    }
}
