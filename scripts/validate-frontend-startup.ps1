param([string]$PortableRoot='')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(!$PortableRoot){$PortableRoot=Join-Path $root 'work\frontend-33'}
$resolved=[IO.Path]::GetFullPath($PortableRoot)
$allowed=[IO.Path]::GetFullPath((Join-Path $root 'work'))+[IO.Path]::DirectorySeparatorChar
if(!$resolved.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)){throw 'Smoke test requires an isolated copy inside project/work'}
if(!(Test-Path -LiteralPath (Join-Path $resolved 'portable_mode.txt'))){throw 'Portable marker missing'}
$started=Get-Date
$process=Start-Process -FilePath (Join-Path $resolved 'bin\64bit\obs64.exe') -WorkingDirectory (Join-Path $resolved 'bin\64bit') -ArgumentList @('--portable','--multi','--disable-updater','--disable-shutdown-check') -WindowStyle Hidden -PassThru
$loaded=$false
$log=$null
try {
    $deadline=(Get-Date).AddSeconds(25)
    do {
        Start-Sleep -Milliseconds 250
        $log=Get-ChildItem -LiteralPath (Join-Path $resolved 'config\obs-studio\logs') -File -ErrorAction SilentlyContinue | Where-Object {$_.LastWriteTime -ge $started.AddSeconds(-1)} | Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if($log){$content=Get-Content -LiteralPath $log.FullName -Raw; $loaded=$content -match 'Loaded Modules:[\s\S]*?obs-playlist-nexus\.dll'}
        $process.Refresh()
    } while(!$loaded -and !$process.HasExited -and (Get-Date) -lt $deadline)
    if(!$loaded){throw 'Frontend did not confirm plugin load; inspect portable log'}
    Copy-Item -LiteralPath $log.FullName -Destination (Join-Path $root 'validation\frontend-33-startup.log') -Force
    [ordered]@{plugin_loaded=$true;test='frontend startup only';interactive_workflow_tested=$false;portable_root=$resolved;plugin_sha256=(Get-FileHash (Join-Path $resolved 'obs-plugins\64bit\obs-playlist-nexus.dll')).Hash;timestamp=(Get-Date).ToString('o')} | ConvertTo-Json | Set-Content (Join-Path $root 'validation\frontend-startup.json') -Encoding UTF8
    Write-Output 'PASS: real OBS frontend loaded plugin (startup smoke test only)'
} finally {
    $process.Refresh()
    if(!$process.HasExited){$null=$process.CloseMainWindow(); if(!$process.WaitForExit(3000)){$process.Kill();$process.WaitForExit()}}
    $process.Dispose()
}
