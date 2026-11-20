param([string]$PortableRoot='')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$portable=if($PortableRoot){[IO.Path]::GetFullPath($PortableRoot)}else{Join-Path $root 'work\frontend-33'}
$allowed=[IO.Path]::GetFullPath((Join-Path $root 'work'))+[IO.Path]::DirectorySeparatorChar
if(!$portable.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)){throw 'Persistence test requires a project/work portable copy'}
$helper=Join-Path $portable 'obs-plugins\64bit\nexus-frontend-test.dll'
if(!(Test-Path (Join-Path $portable 'core\obs-transitions'))){throw 'Run prepare-frontend.ps1 with OBS 33 first (core modules required)'}
$config=Join-Path $portable 'config\obs-studio'
New-Item -ItemType Directory -Path $config -Force | Out-Null
$collection='NEXUSTest'+(Get-Date -Format 'yyyyMMddHHmmss')
@'
[General]
FirstRun=true
ConfirmOnExit=false
[Basic]
Profile=NEXUS-Test
ProfileDir=NEXUS-Test
SceneCollection=NEXUS-Test
SceneCollectionFile=NEXUS-Test
ConfigOnNewProfile=false
[BasicWindow]
SysTrayEnabled=false
'@ | ForEach-Object {$_ -replace 'SceneCollection=NEXUS-Test',"SceneCollection=$collection" -replace 'SceneCollectionFile=NEXUS-Test',"SceneCollectionFile=$collection"} | Set-Content (Join-Path $config 'user.ini') -Encoding UTF8
$profile=Join-Path $config 'basic\profiles\NEXUS-Test'
New-Item -ItemType Directory -Path $profile -Force | Out-Null
@'
[General]
Name=NEXUS-Test
[Video]
BaseCX=1920
BaseCY=1080
OutputCX=1920
OutputCY=1080
FPSType=0
FPSCommon=60
'@ | Set-Content (Join-Path $profile 'basic.ini') -Encoding UTF8
$old=@{}
foreach($key in @('NEXUS_FRONTEND_PHASE','NEXUS_FRONTEND_REPORT','NEXUS_FRONTEND_CLIPS')){$old[$key]=[Environment]::GetEnvironmentVariable($key,'Process')}
$clips=@(Get-Content (Join-Path $root 'validation\example-media-mp4-selected.txt') | Select-Object -First 2)
if($clips.Count -ne 2){throw 'Two real clips required'}
$env:NEXUS_FRONTEND_CLIPS=$clips -join '|'
Copy-Item (Join-Path $root 'build\RelWithDebInfo\nexus-frontend-test.dll') -Destination $helper -Force
try {
    foreach($phase in @('create','reload')) {
        $env:NEXUS_FRONTEND_PHASE=$phase
        $env:NEXUS_FRONTEND_REPORT=Join-Path $root "validation\frontend-$phase.json"
        if(Test-Path $env:NEXUS_FRONTEND_REPORT){Remove-Item -LiteralPath $env:NEXUS_FRONTEND_REPORT}
        $p=Start-Process -FilePath (Join-Path $portable 'bin\64bit\obs64.exe') -WorkingDirectory (Join-Path $portable 'bin\64bit') -ArgumentList @('--portable','--multi','--disable-updater','--disable-shutdown-check') -WindowStyle Hidden -PassThru
        try {
            if(!$p.WaitForExit(45000)){$p.Kill();$p.WaitForExit();throw "Frontend $phase did not close normally"}
            if($p.ExitCode -ne 0){throw "Frontend $phase exit code: $($p.ExitCode)"}
            if(!(Test-Path $env:NEXUS_FRONTEND_REPORT)){throw "Frontend $phase produced no report"}
            $result=Get-Content $env:NEXUS_FRONTEND_REPORT -Raw | ConvertFrom-Json
            if(!$result.passed){throw $result.message}
            $log=Get-ChildItem (Join-Path $config 'logs') -File | Sort-Object LastWriteTime -Descending | Select-Object -First 1
            Copy-Item -LiteralPath $log.FullName -Destination (Join-Path $root "validation\frontend-$phase.log") -Force
            Write-Output "PASS $phase : $($result.message)"
        } finally {$p.Refresh();if(!$p.HasExited){$p.Kill();$p.WaitForExit()};$p.Dispose()}
    }
} finally {
    Remove-Item -LiteralPath $helper -ErrorAction SilentlyContinue
    foreach($key in $old.Keys){[Environment]::SetEnvironmentVariable($key,$old[$key],'Process')}
}
