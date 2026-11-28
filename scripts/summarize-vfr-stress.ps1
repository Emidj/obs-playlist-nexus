$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$root=Join-Path $projectRoot 'validation'
function Read-Result([string]$label) {
    $values=[ordered]@{name=$label}
    foreach($line in Get-Content (Join-Path $root "$label.log")) {
        foreach($match in [regex]::Matches($line,'([a-z_]+)=([^\s]+)')) {$values[$match.Groups[1].Value]=$match.Groups[2].Value}
    }
    [pscustomobject]$values
}
function Memory-Summary([string]$label) {
    $samples=@(Import-Csv (Join-Path $root "$label-memory.csv"))
    $steady=@($samples|Where-Object {[double]$_.seconds -ge 30})
    $windows=foreach($start in @(30,60,90,120)) {
        $window=@($steady|Where-Object {[double]$_.seconds -ge $start -and [double]$_.seconds -lt $start+30})
        if($window.Count -eq 30) {
            [pscustomobject]@{start_seconds=$start;samples=$window.Count;mean_private_mib=($window|ForEach-Object {[double]$_.private_commit_mib}|Measure-Object -Average).Average;mean_handles=($window|ForEach-Object {[int]$_.handles}|Measure-Object -Average).Average}
        }
    }
    [pscustomobject]@{name=$label;samples=$samples.Count;warmup_seconds=30;windows=$windows}
}
$labels=@('stress-600','vfr-33-sequential','vfr-33-random','vfr-32-sequential','vfr-32-random','real-memory-soak')
$results=@($labels|ForEach-Object {Read-Result $_})
foreach($case in $results | Where-Object name -ne 'real-memory-soak') {
    $target=if($case.name -eq 'stress-600'){600}else{100}
    foreach($field in @('failures','invalid','timing_errors','timestamp_gaps','backwards','skipped','excess_repeats','endpoint_errors')) {
        if($null -eq $case.$field -or [int]$case.$field -ne 0){throw "Failure in $($case.name): $field"}
    }
    if([int]$case.transitions -lt $target){throw "Incomplete: $($case.name)"}
}
$memory=@((Memory-Summary 'stress-600'),(Memory-Summary 'real-memory-soak'))
$manifest=[ordered]@{project='OBS Playlist NEXUS';stage='VFR and sustained tests';date='2026-11-17';dll_sha256=(Get-FileHash (Join-Path $projectRoot 'build\RelWithDebInfo\obs-playlist-nexus.dll')).Hash;results=$results;memory=$memory}
$manifest|ConvertTo-Json -Depth 12|Set-Content (Join-Path $root 'phase3-summary.json') -Encoding utf8
$rows=foreach($case in $results | Where-Object name -ne 'real-memory-soak') {
    "| $($case.name) | $($case.libobs) | $($case.transitions) | $($case.identity_valid) | 0 | $([math]::Round([double]$case.wall_seconds,1)) |"
}
$memoryRows=foreach($case in $memory) {foreach($window in $case.windows) {
    "| $($case.name) | $($window.start_seconds)–$($window.start_seconds+30) s | $([math]::Round([double]$window.mean_private_mib,2)) | $([math]::Round([double]$window.mean_handles,1)) |"
}}
$real=$results|Where-Object name -eq 'real-memory-soak'
if([int]$real.transitions -lt 40 -or [int]$real.failures -ne 0){throw 'Incomplete real-material soak'}
$text=@"
# VFR y pruebas sostenidas — 17 de noviembre de 2026

**Resultado:** superadas 1.000 transiciones nuevas con identificadores: 600 con FPS constante y 400 VFR. Se añade una ejecución sostenida con seis vídeos MP4 reales del usuario y registro de memoria por segundo. El alcance es de minutos, no de horas.

## Implementación

El decodificador retiene un frame de anticipación: el siguiente PTS determina el final de validez del anterior. Así no se sustituye una pausa VFR legítima por una duración nominal de FPS. La última imagen conserva la duración del frame/contenedor; si falta, permanece el fallback por tasa estimada. El soporte de timestamps ausentes o incorrectos no queda certificado.

La cola sigue limitada a ocho frames y aproximadamente 64 MiB de RGBA por decodificador. Fuera de la cola están el frame actual, el pendiente y el que se convierte, además de memoria interna FFmpeg/GPU: no es un techo de RAM total. El consumidor solo despierta al productor cuando libera una posición; inspeccionar READY o volver a mostrar el mismo frame no lo despierta.

## Oráculo independiente

Se generan MP4 y MOV H.264, y WebM VP9, con intervalos entre frames de aproximadamente 33 a 100 ms. Los PTS y la duración final se extraen con ffprobe en ticks del contenedor y se guardan en archivos .timing.tsv, junto al JSON original. Las ventanas intermedias se calculan a partir del siguiente PTS; no se usa el resultado del motor como referencia.

El detector contrasta cada frame de salida contra esas ventanas reales, acepta permanencias VFR legítimas y mantiene las comprobaciones de firma, extremos, repeticiones excesivas, saltos, fase temporal y huecos. Un test independiente verifica además los 34 frames decodificados: PTS y duraciones coinciden con el oráculo dentro de 2 ns. Evidencia en vfr-decoder-oracle.csv y su log.

## Salida real de libobs/D3D11 a 1080p60

| Ensayo | libobs | Transiciones | Frames identificados | Anomalías temporales y huecos | Duración (s) |
|---|---|---:|---:|---:|---:|
$($rows -join "`n")

Los cuatro ensayos VFR incluyen orden secuencial/aleatorio y bucles, con resoluciones 1920×1080, 1280×720 y 640×480. La tanda de 600 usa los cuatro clips CFR de 24/25/30/60 FPS, incluidas relaciones 4:3 y vertical. Los CSV no excluyen ventanas intermedias ni reestablecen la fase al cambiar de clip.

## Memoria y manejadores

Se muestrea el proceso completo cada segundo y se descartan los primeros 30 segundos como calentamiento. Se muestran ventanas completas de 30 muestras. Son memoria privada comprometida y número de manejadores del banco, no uso aislado del plugin ni VRAM.

| Ejecución | Ventana tras inicio | Memoria privada media (MiB) | Manejadores medios |
|---|---|---:|---:|
$($memoryRows -join "`n")

En la mezcla aleatoria cambia mucho la cantidad de RGBA viva por resolución; por eso se añaden ventanas completas y un control con vídeos reales de tamaño constante. La comparación inicial de medianas de la mezcla daba +15,4 MiB, pero las medias por ventanas y los máximos posteriores no muestran una subida progresiva: esa mediana aislada no demuestra una fuga ni permite descartarla.

El control real completa $($real.transitions) transiciones en $([math]::Round([double]$real.wall_seconds,1)) s, con $($real.frames) frames examinados, $($real.failures) fallos registrados y $($real.black) muestras negras de cuadrícula. Reproduce directamente los seis MP4 seleccionados de Fisher 3; no altera los originales. Su huella se mide en un proceso independiente. Este control coincide parcialmente con la prueba VFR de otro proceso, de modo que no se usan sus cifras de CPU/GPU como comparación aislada de rendimiento.

Estos ensayos acotan el comportamiento observado durante unos minutos. No prueban ausencia de fugas a largo plazo: siguen pendientes una sesión de varias horas y límites bajo múltiples fuentes en un mismo OBS.

## Regresión y trazabilidad

La regresión FFmpeg sin OBS pasa 100 cambios secuenciales y 100 aleatorios, archivo ausente, recuperación y 30 cancelaciones rápidas (phase3-decoder-regression.log). Los tests del reloj y detector, incluida una permanencia VFR deliberadamente larga, están en phase3-unit-tests.log.

Los casos de un frame, corrupción y agotamiento de candidatos se repiten con el prefijo phase3-. La nueva captura se conserva como phase3-continuity-demo.mp4. Se mantienen los registros de las tandas anteriores como históricos.

SHA-256 de la DLL ensayada: $($manifest.dll_sha256). Todas las métricas y ventanas están en phase3-summary.json; los CSV *-memory.csv conservan las muestras individuales.

## Pendientes

Pruebas de varias horas, VFR con duraciones finales/timestamps ausentes o corruptos, discos lentos, varias fuentes en un mismo OBS, atribución GPU y frontend interactivo. El Playlist Manager Qt, carpetas, Next/Previous, aceleración de decodificación e instalador siguen pendientes. No se ha instalado el prototipo en el OBS habitual del usuario.
"@
$text|Set-Content (Join-Path $root 'VFR_Y_ESTABILIDAD.md') -Encoding utf8
