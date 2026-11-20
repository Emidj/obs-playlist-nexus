param(
    [ValidateRange(60,86400)][int]$Seconds=7200,
    [string]$ObsBin='C:\Program Files\obs-studio\bin\64bit',
    [string]$MediaFolder='C:\Media\NightCurrent\Webm'
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$build=Join-Path $root 'build\RelWithDebInfo'
$run=Join-Path $root ('validation\soak-'+(Get-Date -Format yyyyMMdd-HHmmss))
New-Item -ItemType Directory -Path $run | Out-Null
$clips=@(Get-ChildItem -LiteralPath $MediaFolder -File -Filter '*.webm' | Sort-Object Name)
if($clips.Count -lt 2){throw 'At least two WebM files required'}
$variables=@{
    PATH="$ObsBin;$env:PATH"
    NEXUS_REAL_PLAYLIST=(Join-Path $run 'playlist.txt')
    NEXUS_CAPTURE_CSV=(Join-Path $run 'frames.csv')
    NEXUS_MEMORY_CSV=(Join-Path $run 'memory.csv')
    NEXUS_TEST_TIMEOUT="$Seconds"
    NEXUS_TEST_TRANSITIONS='10'
    NEXUS_SOAK='1'
}
$clips.FullName | Set-Content -LiteralPath $variables.NEXUS_REAL_PLAYLIST -Encoding utf8NoBOM
$dll=Join-Path $build 'obs-playlist-nexus.dll'
$old=@{}
foreach($key in $variables.Keys){
    $old[$key]=[Environment]::GetEnvironmentVariable($key,'Process')
    [Environment]::SetEnvironmentVariable($key,$variables[$key],'Process')
}
$result=[ordered]@{
    started=(Get-Date).ToString('o')
    duration_requested_seconds=$Seconds
    source_count=1
    clips=$clips.Count
    plugin_sha256=(Get-FileHash -LiteralPath $dll).Hash
}
$status=Join-Path $run 'status.json'
$result | ConvertTo-Json | Set-Content -LiteralPath $status
try{
    & (Join-Path $build 'obs_render_tests.exe') $ObsBin $dll a b c random 2>&1 | Tee-Object -FilePath (Join-Path $run 'render.log')
    $result.exit_code=$LASTEXITCODE
    $result.finished=(Get-Date).ToString('o')
    $result | ConvertTo-Json | Set-Content -LiteralPath $status
    if($result.exit_code -ne 0){throw "Soak failed. See $run"}
}finally{
    foreach($key in $old.Keys){[Environment]::SetEnvironmentVariable($key,$old[$key],'Process')}
}
