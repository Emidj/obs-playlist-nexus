param([string]$Package='',[string]$OutputName='OBS-Playlist-NEXUS-1.0.0-Setup-x64.exe')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(!$Package){$Package=(Get-ChildItem (Join-Path $root 'dist') -Filter '*.zip' | Sort-Object LastWriteTime -Descending | Select-Object -First 1).FullName}
if(!$Package){throw 'Run package-prototype.ps1 first'}
$compiler=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if(!(Test-Path $compiler)){throw '.NET Framework C# compiler required'}
$output=Join-Path (Join-Path $root 'dist') $OutputName
$framework=Split-Path $compiler
$installer=Join-Path $root 'installer'
$icon=Join-Path $installer 'nexus-icon.ico'
$image=Join-Path $installer 'nexus-sidebar.png'
$manifest=Join-Path $installer 'setup.manifest'
& $compiler /nologo /target:winexe /platform:x64 /optimize+ /utf8output /codepage:65001 "/out:$output" "/resource:$Package,payload.zip" "/resource:$image,nexus-sidebar.png" "/resource:$icon,nexus-icon.ico" "/win32icon:$icon" "/win32manifest:$manifest" /reference:System.Windows.Forms.dll /reference:System.Drawing.dll /reference:System.Web.Extensions.dll "/reference:$framework\System.IO.Compression.dll" (Join-Path $installer 'PrototypeSetup.cs')
if($LASTEXITCODE){throw 'Installer build failed'}
Write-Output $output
