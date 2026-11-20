param([Parameter(Mandatory)][string]$Obs32Bin,[string]$Obs33Bin='C:\Program Files\obs-studio\bin\64bit')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$originalPath=$env:PATH
$exe=Join-Path $projectRoot 'build\RelWithDebInfo\obs_render_tests.exe'
$dll=Join-Path $projectRoot 'build\RelWithDebInfo\obs-playlist-nexus.dll'
function Run-Case([string]$label,[string]$bin,[switch]$Random) {
    Write-Output "Running $label"
    $env:PATH="$bin;$originalPath"
    $env:NEXUS_CAPTURE_CSV=Join-Path $projectRoot "validation\$label.csv"
    $env:NEXUS_MEMORY_CSV=Join-Path $projectRoot "validation\$label-memory.csv"
    $arguments=@($bin,$dll,'a','b','c')
    if($Random){$arguments+='random'}
    & $exe @arguments 2>&1 | Tee-Object -FilePath (Join-Path $projectRoot "validation\$label.log")
    if($LASTEXITCODE){throw "Failed: $label"}
}
try {
    $env:NEXUS_TEST_TIMEOUT='240'
    $env:NEXUS_TEST_TRANSITIONS='600'
    $env:NEXUS_ID_MANIFEST=Join-Path $projectRoot 'tests\fixtures\identity\manifest.tsv'
    Run-Case 'stress-600' $Obs33Bin -Random
    $env:NEXUS_TEST_TRANSITIONS='100'
    $env:NEXUS_ID_MANIFEST=Join-Path $projectRoot 'tests\fixtures\vfr\manifest.tsv'
    foreach($version in @('33','32')) {
        $bin=if($version -eq '33'){$Obs33Bin}else{$Obs32Bin}
        Run-Case "vfr-$version-sequential" $bin
        Run-Case "vfr-$version-random" $bin -Random
    }
} finally {
    $env:PATH=$originalPath
    foreach($name in @('NEXUS_ID_MANIFEST','NEXUS_TEST_TRANSITIONS','NEXUS_TEST_TIMEOUT','NEXUS_CAPTURE_CSV','NEXUS_MEMORY_CSV')) {
        Remove-Item "Env:$name" -ErrorAction SilentlyContinue
    }
}
