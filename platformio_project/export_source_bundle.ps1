# export_source_bundle.ps1
[CmdletBinding()]
param()

# Script directory (where this script lives)
$scriptDir = if ($PSScriptRoot) { $PSScriptRoot } else { (Get-Location).ProviderPath }

# Source directory relative to script
$srcDir = Join-Path $scriptDir "src"

# Output file placed next to the script (project root when script is in root)
$outPath = Join-Path $scriptDir "codes.txt"

# Name of this script (used to exclude it if script is placed inside src)
$selfName = (Split-Path -Leaf $MyInvocation.MyCommand.Path)

# Remove old output if present
if (Test-Path $outPath) { Remove-Item $outPath -Force }

# Header
"Generated on: $(Get-Date -Format o)" | Out-File -FilePath $outPath -Encoding UTF8

if (-Not (Test-Path $srcDir)) {
    "ERROR: 'src' directory not found next to the script ($scriptDir)" | Out-File -FilePath $outPath -Encoding UTF8 -Append
    Write-Output "Source directory not found: $srcDir"
    return
}

# Gather all files under src recursively, excluding this script and the output file
$all = Get-ChildItem -Path $srcDir -Recurse -File |
    Where-Object {
        ($_.FullName -ne (Join-Path $scriptDir $selfName)) -and
        ($_.FullName -ne $outPath)
    }

# Identify main.* files (e.g., main.cpp, main.ino) - case-insensitive
$mainFiles = $all | Where-Object { $_.Name -match '^main\.[^\\\/]+$' } | Sort-Object FullName

# Remaining files in alphabetical order by path
$otherFiles = $all | Where-Object { $_.Name -notmatch '^main\.[^\\\/]+$' } | Sort-Object FullName

# Compose ordered list: mains first, then others
$ordered = @()
if ($mainFiles) { $ordered += $mainFiles }
if ($otherFiles) { $ordered += $otherFiles }

foreach ($f in $ordered) {
    # relative path from script directory for readability
    $rel = Resolve-Path -LiteralPath $f.FullName | ForEach-Object {
        $_.Path.Substring($scriptDir.Length).TrimStart('\','/')
    }
    "----- FILE: $rel -----" | Out-File -FilePath $outPath -Encoding UTF8 -Append
    Get-Content -Raw -LiteralPath $f.FullName | Out-File -FilePath $outPath -Encoding UTF8 -Append
    "" | Out-File -FilePath $outPath -Encoding UTF8 -Append
}

Write-Output "Bundle generated: $outPath"