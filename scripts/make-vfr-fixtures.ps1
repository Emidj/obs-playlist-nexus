$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$fixtureRoot=Join-Path $projectRoot 'tests\fixtures\vfr'
New-Item -ItemType Directory -Path $fixtureRoot -Force | Out-Null
$fixtures=@(
    @{id=7;width=1920;height=1080;fps=30;frames=12;name='id7-vfr.mp4';codec='libx264'},
    @{id=8;width=1280;height=720;fps=30;frames=10;name='id8-vfr.mov';codec='libx264'},
    @{id=9;width=640;height=480;fps=30;frames=12;name='id9-vfr.webm';codec='libvpx-vp9'}
)
$manifest=foreach($fixture in $fixtures) {
    $raw=Join-Path $fixtureRoot 'temporary.rgb'
    & (Join-Path $projectRoot 'build\RelWithDebInfo\fixture_generator.exe') $raw $fixture.id $fixture.frames $fixture.width $fixture.height $fixture.fps
    if($LASTEXITCODE) {throw 'Raw VFR fixture failed'}
    $output=Join-Path $fixtureRoot $fixture.name
    $arguments=@('-hide_banner','-loglevel','error','-y','-f','rawvideo','-pixel_format','rgb24','-video_size',"$($fixture.width)x$($fixture.height)",'-framerate','30','-i',$raw,'-vf','setpts=(N+floor(N/3)+2*floor(N/5))/(30*TB)','-fps_mode','vfr','-an','-c:v',$fixture.codec,'-pix_fmt','yuv420p')
    if($fixture.codec -eq 'libx264') {$arguments+=@('-preset','ultrafast','-crf','16','-video_track_timescale','60000')} else {$arguments+=@('-deadline','realtime','-cpu-used','8','-crf','20','-b:v','0')}
    $arguments+=$output
    & ffmpeg @arguments
    if($LASTEXITCODE) {throw 'VFR encoding failed'}
    Remove-Item -LiteralPath $raw
    $probe=& ffprobe -v error -select_streams v:0 -show_frames -show_streams -show_entries 'frame=best_effort_timestamp,duration,pkt_duration:stream=time_base,duration_ts' -of json $output | ConvertFrom-Json
    $probe | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath ($output+'.ffprobe.json') -Encoding utf8
    $clock=$probe.streams[0].time_base.Split('/')
    $tickNs=1e9*[double]$clock[0]/[double]$clock[1]
    $frames=@($probe.frames)
    $first=[long]$frames[0].best_effort_timestamp
    $timing=for($i=0;$i -lt $frames.Count;$i++) {
        $begin=[long][Math]::Round(([long]$frames[$i].best_effort_timestamp-$first)*$tickNs)
        if($i+1 -lt $frames.Count) {
            $end=[long][Math]::Round(([long]$frames[$i+1].best_effort_timestamp-$first)*$tickNs)
        } else {
            $duration=if($frames[$i].duration){[long]$frames[$i].duration}else{[long]$frames[$i].pkt_duration}
            if($duration -le 0){throw 'Last-frame duration absent; no independent oracle'}
            $end=$begin+[long][Math]::Round($duration*$tickNs)
        }
        "$begin`t$end"
    }
    $timing | Set-Content -LiteralPath ($output+'.timing.tsv') -Encoding utf8
    @($fixture.id,$fixture.width,$fixture.height,$fixture.fps,$frames.Count,$output) -join "`t"
}
$manifest | Set-Content -LiteralPath (Join-Path $fixtureRoot 'manifest.tsv') -Encoding utf8
