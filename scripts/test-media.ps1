param([Parameter(Mandatory)][string]$FFmpegBin, [string]$ObsBin = 'C:\Program Files\obs-studio\bin\64bit')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$outputDir = Join-Path $projectRoot 'build\RelWithDebInfo'
$fixtureDir = Join-Path $projectRoot 'tests\fixtures'
New-Item -ItemType Directory -Path $fixtureDir -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $projectRoot 'validation') -Force | Out-Null
# Stage runtime dependencies beside the test DLL. No changes to the OBS installation.
foreach ($name in @('avcodec-61.dll','avformat-61.dll','avutil-59.dll','swscale-8.dll','swresample-5.dll','libx264-164.dll','zlib.dll','librist.dll','srt.dll','libcurl.dll')) {
    Copy-Item -LiteralPath (Join-Path $FFmpegBin $name) -Destination $outputDir
}
$env:PATH = "$ObsBin;$FFmpegBin;" + $env:PATH
$fixtures = @(
    @{ Name='red24.mp4'; Filter='color=c=red:s=1920x1080:r=24:d=0.25' },
    @{ Name='green25.mkv'; Filter='color=c=lime:s=1920x1080:r=25:d=0.24' },
    @{ Name='blue60.mp4'; Filter='color=c=blue:s=1920x1080:r=60:d=0.25' }
)
foreach ($fixture in $fixtures) {
    & ffmpeg -hide_banner -loglevel error -y -f lavfi -i $fixture.Filter -an -c:v libx264 -preset ultrafast (Join-Path $fixtureDir $fixture.Name)
    if ($LASTEXITCODE) { throw 'Fixture generation failed' }
}
$clips = @($fixtures | ForEach-Object { Join-Path $fixtureDir $_.Name })
& (Join-Path $outputDir 'decoder_tests.exe') @clips 2>&1 | Tee-Object -FilePath (Join-Path $projectRoot 'validation\decoder.log')
if ($LASTEXITCODE) { throw 'Decoder tests failed' }
foreach ($mode in @('sequential', 'random')) {
    $arguments = @($ObsBin, (Join-Path $outputDir 'obs-playlist-nexus.dll')) + $clips
    if ($mode -eq 'random') { $arguments += 'random' }
    & (Join-Path $outputDir 'obs_render_tests.exe') @arguments 2>&1 | Tee-Object -FilePath (Join-Path $projectRoot "validation\obs-$mode.log")
    if ($LASTEXITCODE) { throw "OBS render test failed: $mode" }
}
