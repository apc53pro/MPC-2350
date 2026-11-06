param(
    [string]$Compiler = (Join-Path $PSScriptRoot '..\..\..\Compiler_For_NARM\narmcc.exe'),
    [string]$Python = 'python',
    [string]$MinecraftJar = (Join-Path $env:APPDATA '.minecraft\versions\1.0\1.0.jar'),
    [string]$ResourceRoot = (Join-Path $env:APPDATA '.minecraft\versions\1.0\minecraft\src'),
    [string]$ReferenceSourceRoot = (Join-Path $env:APPDATA '.minecraft\versions\1.0\minecraft\src\net\minecraft\src'),
    [string]$Output = (Join-Path $PSScriptRoot 'MC10\mc10_narm.narm'),
    [string]$BuildName = ('mc10_narm_build_' + (Get-Date -Format 'yyyyMMdd_HHmmss_fff')),
    [switch]$SkipAssets,
    [switch]$AutoProfile,
    [switch]$StartupDiagnostic,
    [switch]$LegacyFont,
    [switch]$SamplerProfile,
    [switch]$TileProfile,
    [switch]$StageProfile,
    [switch]$GeometryProfile,
    [switch]$PreparationProfile,
    [switch]$CullProfile,
    [switch]$Interactive,
    [switch]$KeyboardOnly
)

$ErrorActionPreference = 'Stop'
if ($Interactive) {
    if ($AutoProfile -or $StartupDiagnostic -or $SamplerProfile -or $StageProfile -or $GeometryProfile -or $PreparationProfile -or $CullProfile) {
        throw 'Interactive is a normal-menu gameplay build, separate from automated profiler builds.'
    }
    # Retain the proven ordered tiled renderer without entering benchmark_main.
    $TileProfile = $true
    $LegacyFont = $true
    if (-not $PSBoundParameters.ContainsKey('BuildName')) {
        $BuildName = if ($KeyboardOnly) { 'mc10_narm_build31_keyboard' } else { 'mc10_narm_build31_mouse' }
    }
}
if ($CullProfile) { $PreparationProfile = $true }
if ($PreparationProfile) { $GeometryProfile = $true }
if ($GeometryProfile) {
    if ($StartupDiagnostic -or $SamplerProfile) { throw 'GeometryProfile is separate from StartupDiagnostic and SamplerProfile.' }
    $StageProfile = $true
    if (-not $PSBoundParameters.ContainsKey('BuildName')) {
        $BuildName = if ($CullProfile) { 'mc10_narm_build26_visibility_culling' } elseif ($PreparationProfile) { 'mc10_narm_build24_preparation_fixes' } else { 'mc10_narm_build23_geometry_optimization' }
    }
}
if ($StageProfile) {
    if ($StartupDiagnostic -or $SamplerProfile) { throw 'StageProfile is separate from StartupDiagnostic and SamplerProfile.' }
    $TileProfile = $true
    if (-not $GeometryProfile -and -not $PSBoundParameters.ContainsKey('BuildName')) { $BuildName = 'mc10_narm_build22_draw_stage_profile' }
}
if ($TileProfile) {
    if ($StartupDiagnostic -or $SamplerProfile) { throw 'TileProfile is separate from StartupDiagnostic and SamplerProfile.' }
    if (-not $Interactive) { $AutoProfile = $true }
    $LegacyFont = $true
}
if ($SamplerProfile) {
    if ($StartupDiagnostic) { throw 'SamplerProfile cannot be combined with StartupDiagnostic.' }
    $AutoProfile = $true
    $LegacyFont = $true
}
if ($StartupDiagnostic -and $LegacyFont) {
    throw 'StartupDiagnostic and LegacyFont are separate isolation builds; choose only one.'
}
if ($BuildName -notmatch '^mc10_narm_build[A-Za-z0-9_-]+$') {
    throw 'BuildName must start with mc10_narm_build and contain only letters, numbers, underscores or hyphens.'
}
$buildDirectory = Join-Path $PSScriptRoot $BuildName
if (Test-Path -LiteralPath $buildDirectory) {
    throw "Build folder already exists; choose a new BuildName to preserve it: $buildDirectory"
}
$source = Join-Path $PSScriptRoot 'mc10_narm.tc'
$assetTool = Join-Path $PSScriptRoot 'tools\mc10_asset_pack.py'
$assetOutput = Join-Path $PSScriptRoot 'MC10\assets.mc10'
$assetManifest = Join-Path $PSScriptRoot 'MC10\assets_manifest.json'
$sourceVerifier = Join-Path $PSScriptRoot 'tools\mc10_verify_source.py'
$sourceManifest = Join-Path $PSScriptRoot 'MC10\source_manifest.json'
$mathTool = Join-Path $PSScriptRoot 'tools\mc10_math_pack.py'
$mathOutput = Join-Path $PSScriptRoot 'MC10\math10.dat'
$worldAssets = @('sky10.dat','blocks10.dat','blocktint10.dat','biomecolor10.dat')
$outputDirectory = Split-Path -Parent $Output
$tests = @(
    (Join-Path $PSScriptRoot 'tests\test_java_random.py'),
    (Join-Path $PSScriptRoot 'tests\test_perlin_constructor.py'),
    (Join-Path $PSScriptRoot 'tests\test_chunk_seed.py'),
    (Join-Path $PSScriptRoot 'tests\test_menu_assets.py'),
    (Join-Path $PSScriptRoot 'tests\test_nbt.py'),
    (Join-Path $PSScriptRoot 'tests\test_world_info.py'),
    (Join-Path $PSScriptRoot 'tests\test_region.py'),
    (Join-Path $PSScriptRoot 'tests\test_world_runtime.py'),
    (Join-Path $PSScriptRoot 'tests\test_options.py'),
    (Join-Path $PSScriptRoot 'tests\test_menu_ui.py'),
    (Join-Path $PSScriptRoot 'tests\test_cursor_timing.py'),
    (Join-Path $PSScriptRoot 'tests\test_loading_screen.py'),
    (Join-Path $PSScriptRoot 'tests\test_player_foundation.py'),
    (Join-Path $PSScriptRoot 'tests\test_viewmodel.py'),
    (Join-Path $PSScriptRoot 'tests\test_streaming.py'),
    (Join-Path $PSScriptRoot 'tests\test_stream_render_cache.py'),
    (Join-Path $PSScriptRoot 'tests\test_stream_game_loop.py'),
    (Join-Path $PSScriptRoot 'tests\test_block_renderer.py'),
    (Join-Path $PSScriptRoot 'tests\test_fp64.py'),
    (Join-Path $PSScriptRoot 'tests\test_player.py'),
    (Join-Path $PSScriptRoot 'tests\test_game_loop.py'),
    (Join-Path $PSScriptRoot 'tests\test_render_cache.py'),
    (Join-Path $PSScriptRoot 'tests\test_profile.py'),
    (Join-Path $PSScriptRoot 'tests\test_benchmark.py'),
    (Join-Path $PSScriptRoot 'tests\test_sampler_profile.py'),
    (Join-Path $PSScriptRoot 'tests\test_tile_profile.py'),
    (Join-Path $PSScriptRoot 'tests\test_stage_profile.py'),
    (Join-Path $PSScriptRoot 'tests\test_geometry_profile.py'),
    (Join-Path $PSScriptRoot 'tests\test_preparation_profile.py'),
    (Join-Path $PSScriptRoot 'tests\test_cull_profile.py'),
    (Join-Path $PSScriptRoot 'tests\test_textured_list.py'),
    (Join-Path $PSScriptRoot 'tests\test_backface_culling.py'),
    (Join-Path $PSScriptRoot 'tests\test_font_batch.py'),
    (Join-Path $PSScriptRoot 'tests\test_startup_trace.py')
)

if (-not (Test-Path -LiteralPath $Compiler -PathType Leaf)) {
    throw "NARMCC was not found at: $Compiler"
}
if (-not (Test-Path -LiteralPath $outputDirectory -PathType Container)) {
    New-Item -ItemType Directory -Path $outputDirectory | Out-Null
}

& $Python $sourceVerifier --source-root $ReferenceSourceRoot --manifest $sourceManifest --quiet
if ($LASTEXITCODE -ne 0) { throw 'Minecraft 1.0 reference-source verification failed' }

if (-not $SkipAssets -or -not (Test-Path -LiteralPath $mathOutput)) {
    & $Python $mathTool --source (Join-Path $ReferenceSourceRoot 'MathHelper.java') --output $mathOutput
    if ($LASTEXITCODE -ne 0) { throw 'Minecraft MathHelper table conversion failed' }
}

foreach ($test in $tests) {
    & $Python $test
    if ($LASTEXITCODE -ne 0) { throw "Compatibility test failed: $test" }
}

if ($Interactive) {
    # Source-derived new data has its own format; existing assets.mc10 is not
    # rewritten when SkipAssets is selected. No decoder is added to the game.
    if (-not $SkipAssets -or -not (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'MC10\sky10.dat'))) {
        & $Python (Join-Path $PSScriptRoot 'tools\mc10_sky_pack.py') --source (Join-Path $ReferenceSourceRoot 'RenderGlobal.java') --jar $MinecraftJar --output (Join-Path $PSScriptRoot 'MC10\sky10.dat')
        if ($LASTEXITCODE -ne 0) { throw 'Owned sky conversion failed' }
    }
    if (-not $SkipAssets -or -not (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'MC10\blocks10.dat')) -or -not (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'MC10\blocktint10.dat'))) {
        $meshScratch = Join-Path ([IO.Path]::GetTempPath()) ('mc10-block-mesh-' + [guid]::NewGuid().ToString('N'))
        & $Python (Join-Path $PSScriptRoot 'tools\convert_block_meshes.py') --source $ReferenceSourceRoot --temp $meshScratch --output (Join-Path $PSScriptRoot 'MC10\blocks10.dat')
        if ($LASTEXITCODE -ne 0) { throw 'Owned block mesh conversion failed' }
    }
    if (-not $SkipAssets -or -not (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'MC10\biomecolor10.dat'))) {
        & $Python (Join-Path $PSScriptRoot 'tools\convert_biome_colors.py') --resources $MinecraftJar --output (Join-Path $PSScriptRoot 'MC10\biomecolor10.dat')
        if ($LASTEXITCODE -ne 0) { throw 'Owned biome color conversion failed' }
    }
    foreach ($testName in @('test_world_clock.py','test_sky.py','test_third_person.py','test_item_block_facts.py','test_world_render.py','test_world_game_loop.py')) {
        & $Python (Join-Path $PSScriptRoot ('tests\' + $testName))
        if ($LASTEXITCODE -ne 0) { throw "World-system test failed: $testName" }
    }
}

if (-not $SkipAssets) {
    & $Python $assetTool pack `
        --jar $MinecraftJar `
        --resources $ResourceRoot `
        --output $assetOutput `
        --manifest $assetManifest
    if ($LASTEXITCODE -ne 0) { throw 'MC10 Minecraft 1.0 asset conversion failed' }
    & $Python $assetTool inspect $assetOutput
    if ($LASTEXITCODE -ne 0) { throw 'MC10 asset-package validation failed' }
}

$profileCompilerFlags = @()
if ($Interactive) {
    $profileCompilerFlags += @('-DMC10_INTERACTIVE=1', '-DMC10_PLAYER_FOUNDATION=1', '-DMC10_WORLD_SYSTEMS=1')
    if (-not $KeyboardOnly) { $profileCompilerFlags += '-DMC10_MOUSE=1' }
}
if ($AutoProfile) { $profileCompilerFlags = @('-DMC10_AUTOPROFILE=1') }
if ($StartupDiagnostic) {
    $profileCompilerFlags = @('-DMC10_AUTOPROFILE=1', '-DMC10_STARTUP_DIAGNOSTIC=1')
}
if ($LegacyFont) { $profileCompilerFlags += '-DMC10_LEGACY_FONT=1' }
if ($SamplerProfile) { $profileCompilerFlags += '-DMC10_SAMPLER_PROFILE=1' }
if ($TileProfile) { $profileCompilerFlags += '-DMC10_TILE_PROFILE=1' }
if ($StageProfile) { $profileCompilerFlags += '-DMC10_STAGE_PROFILE=1' }
if ($GeometryProfile) { $profileCompilerFlags += '-DMC10_GEOMETRY_PROFILE=1' }
if ($PreparationProfile) { $profileCompilerFlags += '-DMC10_PREPARATION_PROFILE=1' }
if ($CullProfile) { $profileCompilerFlags += '-DMC10_CULL_PROFILE=1' }
& $Compiler $source -I $PSScriptRoot -O3 --legacy-float-literals @profileCompilerFlags `
    --default-mode foreground `
    --name 'Minecraft 1.0 NARM' `
    --stack-bytes 32768 `
    --heap-bytes 4096 `
    -o $Output
if ($LASTEXITCODE -ne 0) { throw 'MC10 NARM compilation failed' }

# Preserve every successful build independently of the current working image.
# Only deployable game files are packaged: never saves or options.txt.
$deployDirectory = Join-Path $buildDirectory 'MC10'
New-Item -ItemType Directory -Path $deployDirectory | Out-Null
Copy-Item -LiteralPath $Output -Destination (Join-Path $deployDirectory 'mc10_narm.narm')
Copy-Item -LiteralPath $Output -Destination (Join-Path $buildDirectory ($BuildName + '.narm'))
foreach ($deployAsset in @($mathOutput, $assetOutput)) {
    if (Test-Path -LiteralPath $deployAsset) {
        Copy-Item -LiteralPath $deployAsset -Destination $deployDirectory
    }
}
if ($Interactive) {
    foreach ($worldAsset in $worldAssets) {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot ('MC10\' + $worldAsset)) -Destination $deployDirectory
    }
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'README.md') -Destination $buildDirectory
$buildArchive = Join-Path $buildDirectory ($BuildName + '.zip')
$archiveContents = @($deployDirectory, (Join-Path $buildDirectory 'README.md'))
if ($AutoProfile) {
    $reportName = if ($CullProfile) { 'mc10_cull_report.py' } elseif ($GeometryProfile) { 'mc10_geometry_report.py' } elseif ($StageProfile) { 'mc10_stage_report.py' } elseif ($TileProfile) { 'mc10_tile_report.py' } elseif ($SamplerProfile) { 'mc10_sampler_report.py' } else { 'mc10_profile_report.py' }
    $profileReport = Join-Path $buildDirectory $reportName
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot ('tools\' + $reportName)) -Destination $profileReport
    $archiveContents += $profileReport
}
Compress-Archive -Path $archiveContents -DestinationPath $buildArchive

$artifacts = @($Output, $buildArchive)
if (Test-Path -LiteralPath $mathOutput) { $artifacts += $mathOutput }
if (Test-Path -LiteralPath $assetOutput) { $artifacts += $assetOutput }
if (Test-Path -LiteralPath $assetManifest) { $artifacts += $assetManifest }
if (Test-Path -LiteralPath $sourceManifest) { $artifacts += $sourceManifest }
Get-Item -LiteralPath $artifacts | Select-Object FullName, Length, LastWriteTime
Get-FileHash -Algorithm SHA256 -LiteralPath $artifacts | Select-Object Path, Hash

