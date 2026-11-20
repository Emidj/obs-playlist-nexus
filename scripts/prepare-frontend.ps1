param([string]$ObsRoot='C:\Program Files\obs-studio',[switch]$Launch,[string]$TargetRoot='')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$target=if($TargetRoot){[IO.Path]::GetFullPath($TargetRoot)}else{Join-Path $projectRoot 'work\frontend-33'}
$allowed=[IO.Path]::GetFullPath((Join-Path $projectRoot 'work'))+[IO.Path]::DirectorySeparatorChar
if(!$target.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)){throw 'Frontend staging must stay inside project/work'}
New-Item -ItemType Directory -Path $target -Force|Out-Null
foreach($directory in @('bin','data')) {Copy-Item -LiteralPath (Join-Path $ObsRoot $directory) -Destination $target -Recurse -Force}
# OBS 33 moved built-in modules out of obs-plugins into core.
if(Test-Path -LiteralPath (Join-Path $ObsRoot 'core')) {
    Copy-Item -LiteralPath (Join-Path $ObsRoot 'core') -Destination $target -Recurse -Force
}
$plugins=Join-Path $target 'obs-plugins\64bit'
New-Item -ItemType Directory -Path $plugins -Force|Out-Null
$build=Join-Path $projectRoot 'build\RelWithDebInfo'
foreach($name in @('obs-playlist-nexus.dll','avcodec-61.dll','avformat-61.dll','avutil-59.dll','swscale-8.dll','swresample-5.dll','libx264-164.dll','zlib.dll','librist.dll','srt.dll','libcurl.dll')) {Copy-Item -LiteralPath (Join-Path $build $name) -Destination $plugins -Force}
New-Item -ItemType File -Path (Join-Path $target 'portable_mode.txt') -Force|Out-Null
if($Launch) {Start-Process -FilePath (Join-Path $target 'bin\64bit\obs64.exe') -WorkingDirectory (Join-Path $target 'bin\64bit') -ArgumentList @('--portable','--multi','--disable-updater','--disable-shutdown-check') -WindowStyle Hidden}
Write-Output "Prepared: $target"
