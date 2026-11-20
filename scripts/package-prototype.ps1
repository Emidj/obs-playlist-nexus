param([string]$Configuration='RelWithDebInfo',[string]$Version='0.1.0')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$build=Join-Path $root "build\$Configuration"
$tag=Get-Date -Format 'yyyyMMdd-HHmmss'
$stage=Join-Path $root "dist\OBS-Playlist-NEXUS-$Version-payload-$tag"
$plugins=Join-Path $stage 'obs-plugins\64bit'
New-Item -ItemType Directory -Path $plugins -Force | Out-Null
$names=@('obs-playlist-nexus.dll','avcodec-61.dll','avformat-61.dll','avutil-59.dll','swscale-8.dll','swresample-5.dll','libx264-164.dll','zlib.dll','librist.dll','srt.dll','libcurl.dll')
$manifest=@()
foreach($name in $names) {
    $source=Join-Path $build $name
    if(!(Test-Path -LiteralPath $source -PathType Leaf)){throw "Missing dependency: $name"}
    $destination=Join-Path $plugins $name
    Copy-Item -LiteralPath $source -Destination $destination
    $hash=(Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
    if($hash -ne (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash){throw "Copy mismatch: $name"}
    $manifest+= [ordered]@{file="obs-plugins/64bit/$name";sha256=$hash;bytes=(Get-Item -LiteralPath $destination).Length}
}
[ordered]@{version=$Version;architecture='Windows x64';files=$manifest} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $stage 'manifest.json') -Encoding UTF8
@'
OBS Playlist NEXUS 0.1.0 — paquete interno de pruebas Windows x64

Usar exclusivamente con una COPIA PORTATIL AISLADA de OBS 32/33.
Con OBS cerrado, copiar obs-plugins dentro de esa copia portatil.
Si una DLL ya existe con contenido distinto, detenerse: no sobrescribirla.
No incluye OBS, Qt, clips del usuario, ni configuraciones personales.
Qt y el runtime MSVC deben proceder de la instalacion de OBS de prueba.
Agregar fuente: OBS Playlist NEXUS (Prototype); abrir Playlist Manager.
Los atajos se asignan en Ajustes > Atajos. No hay teclas predeterminadas.

GRUPOS: abrir Playlist Manager > Organizar grupos.
Elegir Sin grupos, Prefijo (guion bajo/guion/espacio), Subcarpeta o Manual.
Revisar la vista previa y Guardar. Para grupos manuales, seleccionar videos
y pulsar Asignar grupo. Los no asignados participan como Sin grupo.
En Orden, elegir Aleatorio equilibrado por grupos para uno de cada grupo
por ronda, con bolsas aleatorias independientes. Para importar carpetas
anidadas, activar Incluir subcarpetas al importar.

Este ZIP no es un instalador ni una version publica.
Pendiente cerrar licencias y redistribucion de dependencias antes de publicar.
Para deshacer la prueba, retirar solo los archivos introducidos por este paquete.
No eliminar dependencias compartidas de otra instalacion de OBS.
'@ | Set-Content -LiteralPath (Join-Path $stage 'LEEME.txt') -Encoding UTF8
$archive="$stage.zip"
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $archive
Write-Output $archive
