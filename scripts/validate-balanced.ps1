param([Parameter(Mandatory)][string]$Obs32Bin,[string]$Obs33Bin='C:\Program Files\obs-studio\bin\64bit')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$names=@('PATH','NEXUS_BALANCED_TEST','NEXUS_TEST_TRANSITIONS','NEXUS_TEST_TIMEOUT','NEXUS_ID_MANIFEST','NEXUS_CAPTURE_CSV')
$old=@{};foreach($name in $names){$old[$name]=[Environment]::GetEnvironmentVariable($name,'Process')}
try {
    $env:NEXUS_BALANCED_TEST='1';$env:NEXUS_TEST_TRANSITIONS='100';$env:NEXUS_TEST_TIMEOUT='90'
    foreach($version in @('32','33')) {foreach($format in @('identity','vfr')) {
        $bin=if($version -eq '32'){$Obs32Bin}else{$Obs33Bin}
        $env:PATH="$bin;$($old['PATH'])"
        $env:NEXUS_ID_MANIFEST=Join-Path $root "tests\fixtures\$format\manifest.tsv"
        $prefix=Join-Path $root "validation\balanced-$format-$version"
        $env:NEXUS_CAPTURE_CSV="$prefix.csv"
        & (Join-Path $root 'build\RelWithDebInfo\obs_render_tests.exe') $bin (Join-Path $root 'build\RelWithDebInfo\obs-playlist-nexus.dll') a b c 2>&1 | Tee-Object -FilePath "$prefix.log"
        if($LASTEXITCODE){throw "Balanced render test failed: $format / $version"}
    }}
}finally {foreach($name in $names){[Environment]::SetEnvironmentVariable($name,$old[$name],'Process')}}
