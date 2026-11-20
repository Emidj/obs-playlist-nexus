param([string]$PreviousInstaller='',[string]$InstallerName='OBS-Playlist-NEXUS-1.0.0-Setup-x64.exe',
    [string]$Configuration='RelWithDebInfo',[switch]$Standard)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$exe=Join-Path (Join-Path $root 'dist') $InstallerName
$sandbox=Join-Path $root ('work\installer-check-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $sandbox -Force | Out-Null
function New-TestRoot([string]$name) {
    $target=Join-Path $sandbox $name
    New-Item -ItemType Directory -Path (Join-Path $target 'bin\64bit'),(Join-Path $target 'obs-plugins\64bit') -Force | Out-Null
    Copy-Item -LiteralPath 'C:\Program Files\obs-studio\bin\64bit\obs64.exe' -Destination (Join-Path $target 'bin\64bit')
    if(!$Standard){New-Item -ItemType File -Path (Join-Path $target 'portable_mode.txt') | Out-Null}
    return $target
}
function Invoke-Setup([string]$action,[string]$target,[int]$expected) {
    $p=Start-Process -FilePath $exe -ArgumentList @($action,"`"$target`"") -WindowStyle Hidden -PassThru
    try {if(!$p.WaitForExit(20000)){$p.Kill();throw 'Installer timed out'};if($p.ExitCode -ne $expected){throw "Unexpected setup result: $($p.ExitCode); expected $expected. $(Get-Content (Join-Path $env:TEMP 'nexus-playlist-setup-error.txt') -Raw -ErrorAction SilentlyContinue)"}}finally{$p.Dispose()}
}
function Snapshot([string]$target) {
    $values=@{}
    Get-ChildItem (Join-Path $target 'obs-plugins\64bit') -File | ForEach-Object {$values[$_.Name]=(Get-FileHash -LiteralPath $_.FullName).Hash}
    return $values
}
function Assert-Same($before,$after) {
    if($before.Count -ne $after.Count){throw 'File count changed after rejected operation'}
    foreach($key in $before.Keys){if($before[$key] -ne $after[$key]){throw "Unexpected mutation: $key"}}
}
$target=New-TestRoot 'portable with spaces'
$plugins=Join-Path $target 'obs-plugins\64bit'
Copy-Item (Join-Path $root 'build\RelWithDebInfo\avutil-59.dll') -Destination $plugins
Set-Content (Join-Path $plugins 'unrelated.dll') 'must-survive'
if($PreviousInstaller){
    $p=Start-Process -FilePath $PreviousInstaller -ArgumentList @('--install',"`"$target`"") -WindowStyle Hidden -PassThru
    try {if(!$p.WaitForExit(20000)){$p.Kill();throw 'Previous installer timed out'};if($p.ExitCode){throw 'Previous installer failed'}}finally{$p.Dispose()}
}
Invoke-Setup '--install' $target 0
if((Get-FileHash (Join-Path $plugins 'obs-playlist-nexus.dll')).Hash -ne (Get-FileHash (Join-Path $root "build\$Configuration\obs-playlist-nexus.dll")).Hash){throw 'Update did not deploy current plugin'}
$first=Snapshot $target
$state=Get-Content (Join-Path $plugins 'nexus-playlist-install.json') -Raw | ConvertFrom-Json
if($state.files.Count -ne 10){throw 'Shared dependency incorrectly claimed'}
Invoke-Setup '--install' $target 0
Assert-Same $first (Snapshot $target)
# Trigger failure after earlier replacements, then check rollback byte for byte.
$lock=[IO.File]::Open((Join-Path $plugins 'swscale-8.dll'),[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
try {Invoke-Setup '--install' $target 1}finally{$lock.Dispose()}
Assert-Same $first (Snapshot $target)
$plugin=Join-Path $plugins 'obs-playlist-nexus.dll'
$bytes=[IO.File]::ReadAllBytes($plugin)
[IO.File]::AppendAllText($plugin,'modified')
$modified=Snapshot $target
Invoke-Setup '--uninstall' $target 1
Assert-Same $modified (Snapshot $target)
[IO.File]::WriteAllBytes($plugin,$bytes)
Invoke-Setup '--uninstall' $target 0
$remaining=Snapshot $target
if($remaining.Count -ne 2 -or !$remaining.ContainsKey('avutil-59.dll') -or !$remaining.ContainsKey('unrelated.dll')){throw 'Uninstall did not preserve foreign files'}
$conflict=New-TestRoot 'conflict'
Set-Content (Join-Path $conflict 'obs-plugins\64bit\avcodec-61.dll') 'foreign-version'
$before=Snapshot $conflict
Invoke-Setup '--install' $conflict 1
Assert-Same $before (Snapshot $conflict)
[ordered]@{passed=$true;tests=@('fresh install','same-version update','rollback after locked DLL','modified managed DLL refused','shared identical DLL preserved','foreign different DLL refused','uninstall preserves foreign files','target path with spaces');previous_version_upgrade_tested=[bool]$PreviousInstaller;setup_sha256=(Get-FileHash $exe).Hash;sandbox=$sandbox;timestamp=(Get-Date).ToString('o')} | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $root 'validation\installer-check.json') -Encoding UTF8
Write-Output 'PASS: installer, update, rollback, conflicts and uninstall'
