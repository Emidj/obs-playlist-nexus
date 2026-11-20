param([string]$ObsBin='C:\Program Files\obs-studio\bin\64bit',[string]$Prefix='')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$exe=Join-Path $projectRoot 'build\RelWithDebInfo\obs_render_tests.exe'
$dll=Join-Path $projectRoot 'build\RelWithDebInfo\obs-playlist-nexus.dll'
$originalPath=$env:PATH
$env:PATH="$ObsBin;$originalPath"
function Run-Case([string]$label) {
    $label=$Prefix+$label
    Write-Output "Running $label"
    $env:NEXUS_CAPTURE_CSV=Join-Path $projectRoot "validation\$label.csv"
    & $exe $ObsBin $dll a b c 2>&1 | Tee-Object -FilePath (Join-Path $projectRoot "validation\$label.log")
    if($LASTEXITCODE) {throw "Failed: $label"}
}
try {
    $env:NEXUS_TEST_TIMEOUT='40'
    $env:NEXUS_TEST_TRANSITIONS='100'
    $env:NEXUS_ID_MANIFEST=Join-Path $projectRoot 'tests\fixtures\identity-short\manifest.tsv'
    Run-Case 'single-frame-clips'
    Remove-Item Env:NEXUS_ID_MANIFEST
    $env:NEXUS_REAL_PLAYLIST=Join-Path $projectRoot 'validation\corrupt-middle-playlist.txt'
    $env:NEXUS_TEST_TRANSITIONS='3'
    Run-Case 'corrupt-middle'
    $env:NEXUS_REAL_PLAYLIST=Join-Path $projectRoot 'validation\all-invalid-playlist.txt'
    $env:NEXUS_EXPECT_EXHAUSTED='1'
    Run-Case 'all-invalid'
    Remove-Item Env:NEXUS_REAL_PLAYLIST
    Remove-Item Env:NEXUS_EXPECT_EXHAUSTED
    $env:NEXUS_ID_MANIFEST=Join-Path $projectRoot 'tests\fixtures\identity\manifest.tsv'
    $env:NEXUS_TEST_TRANSITIONS='8'
    $env:NEXUS_RECORD_RGB=Join-Path $projectRoot 'tests\fixtures\capture-demo.rgb'
    Run-Case 'recorded-demo'
    & ffmpeg -hide_banner -loglevel error -y -f rawvideo -pixel_format rgb24 -video_size 640x360 -framerate 60 -i $env:NEXUS_RECORD_RGB -an -c:v libx264 -preset medium -crf 16 -pix_fmt yuv420p -movflags +faststart (Join-Path $projectRoot "validation\$($Prefix)continuity-demo.mp4")
    if($LASTEXITCODE) {throw 'Demo encoding failed'}
    Remove-Item -LiteralPath $env:NEXUS_RECORD_RGB
} finally {
    $env:PATH=$originalPath
    foreach($name in @('NEXUS_ID_MANIFEST','NEXUS_TEST_TRANSITIONS','NEXUS_TEST_TIMEOUT','NEXUS_CAPTURE_CSV','NEXUS_REAL_PLAYLIST','NEXUS_EXPECT_EXHAUSTED','NEXUS_RECORD_RGB')) {
        Remove-Item "Env:$name" -ErrorAction SilentlyContinue
    }
}
