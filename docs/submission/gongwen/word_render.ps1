param(
    [Parameter(Mandatory=$true)][string]$Docx,
    [Parameter(Mandatory=$true)][string]$Pdf,
    [switch]$NoSave
)

$ErrorActionPreference = 'Stop'
$word = New-Object -ComObject Word.Application
$word.Visible = $false
$word.DisplayAlerts = 0

try {
    $doc = $word.Documents.Open($Docx, [ref]$false, [ref]$false)

    # update TOC fields
    foreach ($toc in $doc.TablesOfContents) { $toc.Update() }
    # update body fields
    $doc.Fields.Update() | Out-Null
    # update header/footer page-number fields
    foreach ($sec in $doc.Sections) {
        foreach ($h in $sec.Headers) { if ($h.Exists) { $h.Range.Fields.Update() | Out-Null } }
        foreach ($f in $sec.Footers) { if ($f.Exists) { $f.Range.Fields.Update() | Out-Null } }
    }
    $doc.Repaginate()

    $pages = $doc.ComputeStatistics(2)
    Write-Output "PAGES=$pages"

    if (-not $NoSave) { $doc.Save() }

    $doc.ExportAsFixedFormat($Pdf, 17)
    Write-Output "PDF=$Pdf"
    $doc.Close([ref]$false)
}
finally {
    $word.Quit()
    [System.Runtime.InteropServices.Marshal]::ReleaseComObject($word) | Out-Null
}
