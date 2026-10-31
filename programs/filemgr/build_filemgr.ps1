param(
    [string]$Compiler = (Join-Path $PSScriptRoot '..\..\..\..\Compiler_For_NARM\narmcc.exe'),
    [string]$Output = (Join-Path $PSScriptRoot '..\..\data\filemgr.narm')
)
$ErrorActionPreference = 'Stop'
$source = Join-Path $PSScriptRoot 'filemgr640.tc'
& $Compiler $source -I $PSScriptRoot -O3 --default-mode background --name 'FILEMGR 640' --stack-bytes 16384 --heap-bytes 16384 -o $Output
if ($LASTEXITCODE -ne 0) { throw 'FILEMGR compilation failed' }
Get-Item -LiteralPath $Output | Select-Object FullName, Length, LastWriteTime
