param([switch]$Plugin, [string]$ObsInclude = '', [string]$ObsConfigInclude = '', [string]$ObsLibrary = '', [string]$FFmpegRoot = '',[string]$QtRoot='')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$cmake = 'C:\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
if (!(Test-Path $cmake)) { $cmake = (Get-Command cmake -ErrorAction Stop).Source }
$arguments = @('-S', $projectRoot, '-B', "$projectRoot\build", '-G', 'Visual Studio 17 2022', '-A', 'x64')
if ($Plugin) {
    if (!$ObsInclude -or !$ObsConfigInclude -or !$ObsLibrary -or !$FFmpegRoot) { throw 'Specify ObsInclude, ObsConfigInclude, ObsLibrary and FFmpegRoot' }
    $arguments += @('-DNEXUS_BUILD_PLUGIN=ON', "-DOBS_INCLUDE_DIR=$ObsInclude", "-DOBS_CONFIG_INCLUDE_DIR=$ObsConfigInclude", "-DOBS_LIBRARY=$ObsLibrary", "-DCMAKE_PREFIX_PATH=$FFmpegRoot")
    if($QtRoot){$arguments+="-DQt6_DIR=$QtRoot/lib/cmake/Qt6"}
} else {
    $arguments += '-DNEXUS_BUILD_PLUGIN=OFF'
}
& $cmake @arguments
if ($LASTEXITCODE) { throw 'Configure failed' }
& $cmake --build "$projectRoot\build" --config RelWithDebInfo
if ($LASTEXITCODE) { throw 'Build failed' }
& (Join-Path (Split-Path $cmake) 'ctest.exe') --test-dir "$projectRoot\build" -C RelWithDebInfo --output-on-failure
if ($LASTEXITCODE) { throw 'Tests failed' }
