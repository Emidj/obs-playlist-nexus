param(
    [string]$Obs33Bin='C:\Program Files\obs-studio\bin\64bit',
    [Parameter(Mandatory)][string]$Obs32Bin
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$exe=Join-Path $projectRoot 'build\RelWithDebInfo\obs_render_tests.exe'
$dll=Join-Path $projectRoot 'build\RelWithDebInfo\obs-playlist-nexus.dll'
$originalPath=$env:PATH
try {
    $env:NEXUS_ID_MANIFEST=Join-Path $projectRoot 'tests\fixtures\identity\manifest.tsv'
    $env:NEXUS_TEST_TRANSITIONS='100'
    $env:NEXUS_TEST_TIMEOUT='90'
    foreach($version in @('33','32')) {
        $bin=if($version -eq '33') {$Obs33Bin} else {$Obs32Bin}
        $env:PATH="$bin;$originalPath"
        foreach($mode in @('sequential','random')) {
            $label="identity-$version-$mode"
            Write-Output "Running $label"
            $env:NEXUS_CAPTURE_CSV=Join-Path $projectRoot "validation\$label.csv"
            $arguments=@($bin,$dll,'a','b','c')
            if($mode -eq 'random') {$arguments+='random'}
            & $exe @arguments 2>&1 | Tee-Object -FilePath (Join-Path $projectRoot "validation\$label.log")
            if($LASTEXITCODE) {throw "Failed: $label"}
        }
    }
    Remove-Item Env:NEXUS_ID_MANIFEST
    $env:NEXUS_TEST_TRANSITIONS='20'
    $env:NEXUS_TEST_TIMEOUT='180'
    $env:PATH="$Obs33Bin;$originalPath"
    foreach($format in @('mp4','webm')) {
        $label="fisher3-$format"
        Write-Output "Running $label"
        $env:NEXUS_REAL_PLAYLIST=Join-Path $projectRoot "validation\fisher3-$format-selected.txt"
        $env:NEXUS_CAPTURE_CSV=Join-Path $projectRoot "validation\$label.csv"
        & $exe $Obs33Bin $dll a b c 2>&1 | Tee-Object -FilePath (Join-Path $projectRoot "validation\$label.log")
        if($LASTEXITCODE) {throw "Failed: $label"}
    }
} finally {
    $env:PATH=$originalPath
    foreach($name in @('NEXUS_ID_MANIFEST','NEXUS_TEST_TRANSITIONS','NEXUS_TEST_TIMEOUT','NEXUS_CAPTURE_CSV','NEXUS_REAL_PLAYLIST')) {
        Remove-Item "Env:$name" -ErrorAction SilentlyContinue
    }
}
