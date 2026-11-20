param(
    [Parameter(Mandatory)][string]$Obs32Bin,
    [string]$Obs33Bin='C:\Program Files\obs-studio\bin\64bit',
    [string]$ClipFolder='C:\Media\NightCurrent',
    [ValidateSet('windows','minimal')][string]$Platform='windows',
    [string]$Configuration='RelWithDebInfo'
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$build=Join-Path $projectRoot "build\$Configuration"
$originalPath=$env:PATH
$realList=Join-Path $projectRoot 'validation\example-media-mp4-selected.txt'
$real=@(Get-Content $realList)
$reference=Join-Path $projectRoot 'validation\manager-preview-reference-full.png'
& ffmpeg -hide_banner -loglevel error -y -i $real[2] -frames:v 1 $reference
if($LASTEXITCODE){throw 'Preview reference extraction failed'}
try {
    foreach($version in @('32','33')) {
        $bin=if($version -eq '32'){$Obs32Bin}else{$Obs33Bin}
        $stage=Join-Path $projectRoot "build\gui-$version"
        New-Item -ItemType Directory -Path $stage -Force|Out-Null
        # No Qt DLLs here: import the selected host's Qt runtime from PATH.
        Copy-Item -LiteralPath (Join-Path $build 'manager_integration_tests.exe') -Destination $stage
        $env:PATH="$bin;$originalPath";$env:QT_PLUGIN_PATH=$bin;$env:QT_QPA_PLATFORM=$Platform
        $prefix=Join-Path $projectRoot "validation\manager-$version"
        & (Join-Path $stage 'manager_integration_tests.exe') $bin (Join-Path $build 'obs-playlist-nexus.dll') (Join-Path $projectRoot 'tests\fixtures\identity\manifest.tsv') $realList $ClipFolder $prefix $reference 2>&1|Tee-Object -FilePath "$prefix.log"
        if($LASTEXITCODE){throw "Manager integration failed: $version"}
    }
} finally {
    $env:PATH=$originalPath
    Remove-Item Env:QT_PLUGIN_PATH,Env:QT_QPA_PLATFORM -ErrorAction SilentlyContinue
}
