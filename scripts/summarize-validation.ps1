$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$validation=Join-Path $projectRoot 'validation'
$labels=@('identity-33-sequential','identity-33-random','identity-32-sequential','identity-32-random','single-frame-clips','corrupt-middle','all-invalid','fisher3-mp4','fisher3-webm','recorded-demo')
$cases=foreach($label in $labels) {
    $values=[ordered]@{name=$label}
    foreach($line in Get-Content (Join-Path $validation "$label.log")) {
        foreach($match in [regex]::Matches($line,'([a-z_]+)=([^\s]+)')) { $values[$match.Groups[1].Value]=$match.Groups[2].Value }
    }
    [pscustomobject]$values
}
$manifest=[ordered]@{
    project='OBS Playlist NEXUS'; stage='temporal-validation'; date='2026-11-17';
    dll_sha256=(Get-FileHash (Join-Path $projectRoot 'build\RelWithDebInfo\obs-playlist-nexus.dll')).Hash;
    tests=$cases
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $validation 'phase2-summary.json') -Encoding utf8
$temporalRows=foreach($case in $cases | Where-Object name -like 'identity-*') {
    if([int]$case.timing_errors -or [int]$case.invalid -or [int]$case.skipped -or [int]$case.excess_repeats -or [int]$case.endpoint_errors -or [int]$case.timestamp_gaps -or [int]$case.backwards -or [int]$case.transitions -lt 100) {throw "Temporal result failed: $($case.name)"}
    "| $($case.libobs) | $($case.mode) | $($case.transitions) | $($case.identity_valid) | 0 | 0 | $([math]::Round([double]$case.obs_average_frame_ms,3)) |"
}
$resourceRows=foreach($case in $cases | Where-Object name -in @('fisher3-mp4','fisher3-webm')) {
    if([int]$case.transitions -lt 20 -or [int]$case.failures) {throw "Real-media result failed: $($case.name)"}
    "| $($case.name) | $($case.transitions) | $($case.frames) | $($case.black) | $([math]::Round([double]$case.cpu_percent_one_core,1)) % | $([math]::Round([double]$case.peak_working_set_mib,1)) MiB | $([math]::Round([double]$case.obs_average_frame_ms,3)) |"
}
$pairs=@(Get-Content (Join-Path $validation 'fisher3-pairs.json') -Raw | ConvertFrom-Json)
$mp4Bytes=($pairs | Measure-Object mp4_bytes -Sum).Sum
$webmBytes=($pairs | Measure-Object webm_size -Sum).Sum
$reduction=[math]::Round(100*(1-$webmBytes/$mp4Bytes),1)
$mp4=$cases | Where-Object name -eq 'fisher3-mp4'
$webm=$cases | Where-Object name -eq 'fisher3-webm'
$extraCpu=[math]::Round(100*([double]$webm.cpu_percent_one_core/[double]$mp4.cpu_percent_one_core-1),1)
$short=$cases | Where-Object name -eq 'single-frame-clips'
$report=@"
# Validación temporal ampliada — 17 de noviembre de 2026

**Resultado:** pruebas temporales ampliadas y recuperación superadas en los escenarios documentados. Prototipo experimental; no certificación universal de gapless.

## Cambios del motor

- Precarga: los candidatos ilegibles se sustituyen mientras el clip actual sigue reproduciéndose. La recuperación recorre candidatos sin repetir aleatoriamente el mismo error y conserva el índice del clip activo.
- Reloj: margen de presentación de 1 microsegundo para que el redondeo entre reloj entero OBS y duración racional no retenga un frame completo. El test de un solo frame detectó una repetición antes de esta corrección; el log anterior se conserva en single-frame-before-fix.log. Los contadores internos eran cero, lo que confirma la necesidad del detector externo.
- Se han añadido comprobaciones de asignación FFmpeg y limpieza de creación de fuente fallida.

## Verificación de cada frame

La salida real RGBA de libobs/D3D11 se captura a 1920×1080 y 60 FPS. Los clips incluyen un ID binario de 24 bits, su complemento en otra franja y números legibles. El detector comprueba firma, ID, cadencia, primer/último frame de cada clip y una única fase temporal que debe explicar toda la captura. Permite repetición normal de 24/25/30 FPS sobre 60 FPS; rechaza repeticiones excesivas, IDs saltados, retrocesos y huecos de captura. Margen numérico externo: 2 microsegundos.

Clips: 24 FPS 1920×1080 MP4, 25 FPS 1280×720 MP4, 30 FPS 640×480 MOV y 60 FPS 1080×1920 MP4. Incluyen cambios de tamaño, relación de aspecto y bucles último→primero. Los tests del propio detector rechazan deliberadamente un frame repetido/saltado, una captura ausente y una firma inválida.

| libobs real | Orden | Transiciones | Frames con ID válido | Anomalías temporales | Huecos de captura | Render medio libobs (ms) |
|---|---|---:|---:|---:|---:|---:|
$($temporalRows -join "`n")

Es la misma DLL en libobs 32.0.4 y 33.0.1; no se ha sustituido la instalación ni el perfil de OBS del usuario. Cada log y CSV está en esta carpeta. El render medio comunicado por libobs no es una medición aislada del tiempo GPU.

## Casos límite

- Dos clips de un solo frame a 60 FPS: $($short.transitions) cambios internos, $($short.switches) cambios observados en captura; cero repeticiones excesivas, saltos, firmas inválidas o huecos. La diferencia de un cambio se debe al corte del final de la captura, no a una ventana intermedia ignorada.
- Archivo deliberadamente ilegible entre dos vídeos reales: tres transiciones, cero fallos de continuidad registrados y cero muestras negras en la cuadrícula. El candidato se saltó durante la precarga.
- Todos los archivos ilegibles: error explícito, suspensión de intentos y cierre correcto; no se presenta como reproducción correcta.
- Los tests independientes del reloj y del detector han pasado. Los registros se conservan en phase2-unit-tests.log.

## Carpeta real del usuario

Se han inventariado sin modificar originales 265 MP4 H.264 1920×1080 a 24000/1001 FPS y 285 WebM VP9 1920×1080 a 60 FPS. Se han emparejado por nombre los 265 originales con una versión WebM cada uno. Tamaño conjunto de los pares: $mp4Bytes bytes MP4 frente a $webmBytes bytes WebM (**$reduction % menos**). Los 20 WebM adicionales no se suman a esa comparación.

La reproducción real utiliza seis pares seleccionados, con duraciones aproximadas entre 2,84 y 7,38 segundos, tres ciclos completos y parte de un cuarto. No se afirma haber reproducido íntegramente los 265/285 archivos. Listas e inventarios JSON conservados junto a este informe. Se han leído directamente desde C:\ExampleMedia y su subcarpeta Webm.

| Material | Transiciones | Frames examinados | Muestras negras | CPU media, % de un núcleo | Working set máximo, proceso completo | Render medio libobs (ms) |
|---|---:|---:|---:|---:|---:|---:|
$($resourceRows -join "`n")

Los WebM han usado aproximadamente **$extraCpu % más CPU** en esta ejecución de esos seis pares. Los originales no tienen la misma tasa: 23,976 FPS frente a 60 FPS; no se atribuye la diferencia exclusivamente al códec. CPU y memoria corresponden al proceso completo del banco, incluido libobs, captura, comprobaciones y decodificadores, sin descontar un baseline. No son el coste aislado del plugin ni el porcentaje de toda la CPU del PC.

En material real se registra un hash de una cuadrícula de píxeles por frame. Sirve para observar imagen negra y rachas idénticas, pero **no sustituye la comparación con los fotogramas originales**: puede haber imágenes repetidas o estáticas en el propio archivo. La demostración temporal precisa corresponde a los clips identificados.

## Grabación y trazabilidad

continuity-demo.mp4 es una captura de la salida de la DLL, reducida de 1080p60 a 640×360/60 FPS para revisión visual. No es un render independiente que imite al plugin. Los CSV temporales conservan la evidencia por frame en la resolución de ensayo original.

phase2-summary.json incluye métricas y SHA-256 de la DLL final: $($manifest.dll_sha256). Los logs baseline-fisher3-* conservan la medición anterior a las correcciones; las tablas usan los logs finales fisher3-*.

## Pendientes

Reproducción prolongada, VFR, carga de varias fuentes, discos lentos, interfaz interactiva de OBS y otras versiones 32.x. Sigue pendiente atribuir y medir el coste GPU con un profiler adecuado: el acceso a contadores de proceso mediante CIM no estuvo disponible. El uso global de GPU no se ha atribuido al plugin. También faltan el gestor Qt, carpetas, Next/Previous e instalador. Se mantienen los límites de color/HDR/rotación documentados en README.md.
"@
$report | Set-Content (Join-Path $validation 'VALIDACION_TEMPORAL.md') -Encoding utf8
