param([switch]$Short)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$fixtureRoot=Join-Path $projectRoot $(if($Short){'tests\fixtures\identity-short'}else{'tests\fixtures\identity'})
New-Item -ItemType Directory -Path $fixtureRoot -Force | Out-Null
$fixtures=@(
    @{id=1;width=1920;height=1080;fps=24;frames=6;name='id1-24.mp4'},
    @{id=2;width=1280;height=720;fps=25;frames=6;name='id2-25.mp4'},
    @{id=3;width=640;height=480;fps=30;frames=9;name='id3-30.mov'},
    @{id=4;width=1080;height=1920;fps=60;frames=15;name='id4-60.mp4'}
)
if($Short) {
    $fixtures=@(
        @{id=5;width=640;height=360;fps=60;frames=1;name='id5-single-frame.mp4'},
        @{id=6;width=640;height=360;fps=60;frames=1;name='id6-single-frame.mp4'}
    )
}
$manifest=foreach($fixture in $fixtures) {
    $raw=Join-Path $fixtureRoot 'temporary.rgb'
    & (Join-Path $projectRoot 'build\RelWithDebInfo\fixture_generator.exe') $raw $fixture.id $fixture.frames $fixture.width $fixture.height $fixture.fps
    if($LASTEXITCODE) {throw 'Raw fixture generation failed'}
    $output=Join-Path $fixtureRoot $fixture.name
    & ffmpeg -hide_banner -loglevel error -y -f rawvideo -pixel_format rgb24 -video_size "$($fixture.width)x$($fixture.height)" -framerate $fixture.fps -i $raw -an -c:v libx264 -preset ultrafast -crf 16 -pix_fmt yuv420p -video_track_timescale 60000 $output
    if($LASTEXITCODE) {throw 'Fixture encoding failed'}
    Remove-Item -LiteralPath $raw
    @($fixture.id,$fixture.width,$fixture.height,$fixture.fps,$fixture.frames,$output) -join "`t"
}
$manifest | Set-Content -LiteralPath (Join-Path $fixtureRoot 'manifest.tsv') -Encoding utf8
