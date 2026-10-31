param(
    [string]$Compiler = (Join-Path $PSScriptRoot '..\..\..\..\Compiler_For_NARM\narmcc.exe'),
    [string]$Python = 'python',
    [string]$Output = (Join-Path $PSScriptRoot 'Tinycraft\tinycraft_v59.narm'),
    [switch]$SkipAssets
)

$ErrorActionPreference = 'Stop'
$source = Join-Path $PSScriptRoot 'tinycraft_v59.tc'
$assetTool = Join-Path $PSScriptRoot 'tools\tc_asset_pack.py'
$assetSource = Join-Path $PSScriptRoot 'assets'
$assetOutput = Join-Path $PSScriptRoot 'Tinycraft\assets.tca'
$outputDirectory = Split-Path -Parent $Output

if (-not (Test-Path -LiteralPath $Compiler -PathType Leaf)) {
    throw "NARMCC was not found at: $Compiler"
}
if (-not (Test-Path -LiteralPath $outputDirectory -PathType Container)) {
    New-Item -ItemType Directory -Path $outputDirectory | Out-Null
}

if (-not $SkipAssets) {
    & $Python $assetTool pack $assetSource -o $assetOutput
    if ($LASTEXITCODE -ne 0) { throw 'TinyCraft asset-cache build failed' }
    & $Python $assetTool inspect $assetOutput
    if ($LASTEXITCODE -ne 0) { throw 'TinyCraft asset-cache validation failed' }
}

& $Compiler $source -I $PSScriptRoot -O3 `
    --default-mode foreground `
    --name 'TinyCraft v59' `
    --stack-bytes 32768 `
    --heap-bytes 4096 `
    -o $Output
if ($LASTEXITCODE -ne 0) { throw 'TinyCraft NARM compilation failed' }

Get-Item -LiteralPath $Output, $assetOutput |
    Select-Object FullName, Length, LastWriteTime
Get-FileHash -Algorithm SHA256 -LiteralPath $Output, $assetOutput |
    Select-Object Path, Hash
