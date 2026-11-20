param([Parameter(Mandatory)][string]$Obs32Bin,[string]$Obs33Bin='C:\Program Files\obs-studio\bin\64bit')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$exe=Join-Path $projectRoot 'build\RelWithDebInfo\obs_render_tests.exe'
$dll=Join-Path $projectRoot 'build\RelWithDebInfo\obs-playlist-nexus.dll'
$originalPath=$env:PATH
try {
    $env:NEXUS_TEST_TRANSITIONS='100';$env:NEXUS_TEST_TIMEOUT='90'
    foreach($case in @(@{name='manager-regression-cfr-33';bin=$Obs33Bin;manifest='identity';random=$false},@{name='manager-regression-cfr-32';bin=$Obs32Bin;manifest='identity';random=$true},@{name='manager-regression-vfr-33';bin=$Obs33Bin;manifest='vfr';random=$true})) {
        $env:PATH="$($case.bin);$originalPath"
        $env:NEXUS_ID_MANIFEST=Join-Path $projectRoot "tests\fixtures\$($case.manifest)\manifest.tsv"
        $env:NEXUS_CAPTURE_CSV=Join-Path $projectRoot "validation\$($case.name).csv"
        $arguments=@($case.bin,$dll,'a','b','c');if($case.random){$arguments+='random'}
        & $exe @arguments 2>&1|Tee-Object -FilePath (Join-Path $projectRoot "validation\$($case.name).log")
        if($LASTEXITCODE){throw "Regression failed: $($case.name)"}
    }
} finally {
    $env:PATH=$originalPath
    foreach($name in @('NEXUS_TEST_TRANSITIONS','NEXUS_TEST_TIMEOUT','NEXUS_ID_MANIFEST','NEXUS_CAPTURE_CSV')) {Remove-Item "Env:$name" -ErrorAction SilentlyContinue}
}
