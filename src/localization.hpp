#pragma once
#include <obs.h>
#include <QString>
#include <array>
#include <string_view>

namespace nexus {
enum class Text {
    ClipTransition, TransitionDuration, Cut, Dissolve, SlideLeft, SlideRight, ZoomIn,
    Accept, Cancel, Save,
    ObsSourceUnavailable, // OBS source unavailable
    LiveSourceVisibleInObs, // Live source · visible in OBS
    PreparingVideo, // Preparing video…
    EntriesPerCategory, // Entries per category
    EntriesPerCategory2, // Entries per category…
    ChooseHowManyEntriesFromEach, // Choose how many entries from each category play per round.
    Ungrouped, // Ungrouped
    AddObsSource, // Add OBS source
    EntryType, // Entry type
    Source, // Source
    Group, // Group
    ObsSourceOrScene, // OBS source or scene
    NoSourcesAvailable, // No sources available
    NoGroupsAvailable, // No groups available
    DurationInSeconds, // Duration in seconds
    GroupForBalancedRotation, // Group for balanced rotation
    Camera, // Camera
    ObsGroup, // OBS group
    ThatSourceIsAlreadyInThe, // That source is already in the playlist.
    OrganizeGroups, // Organize groups
    GroupBy, // Group by
    NoGroups, // No groups
    NamePrefix, // Name prefix
    ParentSubfolder, // Parent subfolder
    Manual, // Manual
    UnderscoreEGAerial, // Underscore (_) e.g. Aerial_01
    HyphenEGAerial, // Hyphen (-) e.g. Aerial-01
    SpaceEGAerial, // Space e.g. Aerial 01
    PreviewChangesApplyWhenYouClick, // Preview. Changes apply when you click Save.
    GroupFile, // Group / file
    Videos, // Videos
    AllVideos, // All videos
    GroupsEachGroupPlaysItsConfigured, // %1 groups. Each group plays its configured number of entries per round.\n%2
    UseAssignGroupInThePlaylist, // Use Assign group in the playlist to select its members.
    UsesTheTextBeforeTheFirst, // Uses the text before the first separator, or the full name if absent. Check before saving.
    EachParentFolderIsAGroup, // Each parent folder is a group. Enable Include subfolders when importing if needed.
    AllVideosShareOneShufflePool, // All videos share one shuffle pool.
    VideosAdded, // %1 videos added
    NoNewVideosAdded, // No new videos added
    LookingForVideos, // Looking for videos…
    PlaylistManagerObsPlaylistNexus, // Playlist Manager · OBS Playlist NEXUS
    NexusVideoPlaylist, // NEXUS  /  VIDEO PLAYLIST
    PlaylistManager, // Playlist Manager
    YourVideosPlayingContinuously, // Your videos, playing continuously.
    Playlist, // Playlist
    AddVideos, // Add videos
    AddFolder, // Add folder
    Remove, // Remove
    MoveUp, // Move up
    MoveDown, // Move down
    DragVideosOrAFolderInto, // Drag videos or a folder into the playlist.
    AssignGroup, // Assign group
    IncludeSubfoldersWhenImporting, // Include subfolders when importing
    Playback, // Playback
    NowPlaying, // NOW PLAYING
    Previous, // Previous
    Play, // Play
    Pause, // Pause
    Next, // Next
    Order, // Order
    Sequential, // Sequential
    Shuffle, // Shuffle
    BalancedGroupShuffle, // Balanced group shuffle
    ContinuousLoop, // CONTINUOUS LOOP
    VideosMpMkvMovWebm, // Videos (*.mp4 *.mkv *.mov *.webm)
    SelectTheVideosYouWantTo, // Select the videos you want to group first.
    GroupNameBlankUngrouped, // Group name (blank = Ungrouped)
    SLive, // %1 s  ·  LIVE
    CurrentPaused, //   ·  CURRENT · PAUSED
    Playing, //   ·  PLAYING
    NextReady, //   ·  NEXT READY
    Preparing, //   ·  PREPARING
    NoGroupsAllVideosShareOne, // No groups: all videos share one shuffle pool.
    GroupsVideosUnassignedEntriesAreGrouped, // %1 groups · %2 videos. Unassigned entries are grouped under Ungrouped.
    AddVideosToGetStarted, // Add videos to get started
    Next2, // Next: –
    Next3, // Next: 
    Videos2, // %1 videos
    NoVideos, // NO VIDEOS
    ErrorNoPlayableVideos, // ERROR · No playable videos
    ReadyNextVideoPrepared, // READY · Next video prepared
    PreparingNextVideo, // PREPARING · Next video
    Paused, // PAUSED · 
    SourceHidden, // SOURCE HIDDEN · 
    PreparingSkip, // PREPARING SKIP · 
    ContinuityIssues, // Continuity issues: %1
    TheCurrentVideoWillFinishBefore, // The current video will finish before the playlist continues.
    PlaybackContinuesWhileYouReorderThe, // Playback continues while you reorder the playlist.
    CouldNotPrepareTheNextVideo, // Could not prepare the next video
    CouldNotOpenTheSelectedVideo, // Could not open the selected video
    SkippedFile, // Skipped file: 
    VideosLoaded, //  videos loaded
    OpenPlaylistManager, // Open Playlist Manager
    WhenSourceBecomesVisible, // When source becomes visible
    WhenSourceBecomesHidden, // When source becomes hidden
    Continue, // Continue
    RestartPlaylist, // Restart playlist
    Pause2, // Pause
    ObsPlaylistNexus, // OBS Playlist NEXUS
    PlaylistNexusPlay, // Playlist NEXUS: Play
    PlaylistNexusPause, // Playlist NEXUS: Pause
    PlaylistNexusNext, // Playlist NEXUS: Next
    PlaylistNexusPrevious, // Playlist NEXUS: Previous
};

inline const char *localized(Text key){
    // Spanish, English, French, German and Italian. English is the fallback.
    static constexpr std::array<const char *,5> catalog[]={
        {"Transición entre vídeos","Clip transition","Transition entre vidéos","Clip-Übergang","Transizione tra video"},
        {"Duración de la transición","Transition duration","Durée de la transition","Übergangsdauer","Durata della transizione"},
        {"Corte directo","Cut","Coupe","Harter Schnitt","Taglio"},
        {"Disolución","Dissolve","Fondu enchaîné","Überblenden","Dissolvenza"},
        {"Entrada desde la izquierda","Slide in from left","Entrée par la gauche","Von links einschieben","Entrata da sinistra"},
        {"Entrada desde la derecha","Slide in from right","Entrée par la droite","Von rechts einschieben","Entrata da destra"},
        {"Zoom de entrada","Zoom in","Zoom entrant","Hereinzoomen","Zoom in entrata"},
        {"Aceptar","OK","Valider","OK","OK"},
        {"Cancelar","Cancel","Annuler","Abbrechen","Annulla"},
        {"Guardar","Save","Enregistrer","Speichern","Salva"},
        {"Fuente OBS no disponible","OBS source unavailable","Source OBS indisponible","OBS-Quelle nicht verfügbar","Sorgente OBS non disponibile"},
        {"Fuente en directo · visible en OBS","Live source · visible in OBS","Source en direct · visible dans OBS","Live-Quelle · in OBS sichtbar","Sorgente in diretta · visibile in OBS"},
        {"Preparando vídeo…","Preparing video…","Préparation de la vidéo…","Video wird vorbereitet…","Preparazione video…"},
        {"Entradas por categoría","Entries per category","Éléments par catégorie","Einträge pro Kategorie","Elementi per categoria"},
        {"Entradas por categoría…","Entries per category…","Éléments par catégorie…","Einträge pro Kategorie…","Elementi per categoria…"},
        {"Elige cuántas entradas de cada categoría se reproducen por ronda.","Choose how many entries from each category play per round.","Choisissez le nombre d’éléments de chaque catégorie par cycle.","Wähle die Anzahl der Einträge je Kategorie pro Runde.","Scegli quanti elementi di ogni categoria riprodurre per ciclo."},
        {"Sin grupo","Ungrouped","Sans groupe","Ohne Gruppe","Senza gruppo"},
        {"Añadir fuente de OBS","Add OBS source","Ajouter une source OBS","OBS-Quelle hinzufügen","Aggiungi sorgente OBS"},
        {"Tipo de elemento","Entry type","Type d’élément","Eintragstyp","Tipo di elemento"},
        {"Fuente","Source","Source","Quelle","Sorgente"},
        {"Grupo","Group","Groupe","Gruppe","Gruppo"},
        {"Fuente o escena de OBS","OBS source or scene","Source ou scène OBS","OBS-Quelle oder Szene","Sorgente o scena OBS"},
        {"No hay fuentes disponibles","No sources available","Aucune source disponible","Keine Quellen verfügbar","Nessuna sorgente disponibile"},
        {"No hay grupos disponibles","No groups available","Aucun groupe disponible","Keine Gruppen verfügbar","Nessun gruppo disponibile"},
        {"Duración en segundos","Duration in seconds","Durée en secondes","Dauer in Sekunden","Durata in secondi"},
        {"Grupo para la rotación equilibrada","Group for balanced rotation","Groupe pour la rotation équilibrée","Gruppe für die ausgewogene Wiedergabe","Gruppo per la rotazione bilanciata"},
        {"Cámara","Camera","Caméra","Kamera","Telecamera"},
        {"Grupo de OBS","OBS group","Groupe OBS","OBS-Gruppe","Gruppo OBS"},
        {"Esa fuente ya está en la lista.","That source is already in the playlist.","Cette source figure déjà dans la liste.","Diese Quelle ist bereits in der Wiedergabeliste.","Questa sorgente è già nella playlist."},
        {"Organizar grupos","Organize groups","Organiser les groupes","Gruppen organisieren","Organizza gruppi"},
        {"Agrupar por","Group by","Grouper par","Gruppieren nach","Raggruppa per"},
        {"Sin grupos","No groups","Sans groupes","Keine Gruppen","Nessun gruppo"},
        {"Prefijo del nombre","Name prefix","Préfixe du nom","Namenspräfix","Prefisso del nome"},
        {"Subcarpeta contenedora","Parent subfolder","Sous-dossier parent","Übergeordneter Unterordner","Sottocartella di origine"},
        {"Manual","Manual","Manuel","Manuell","Manuale"},
        {"Guion bajo (_) p. ej. Aerial_01","Underscore (_) e.g. Aerial_01","Tiret bas (_) ex. Aerial_01","Unterstrich (_) z. B. Aerial_01","Trattino basso (_) es. Aerial_01"},
        {"Guion (-) p. ej. Aerial-01","Hyphen (-) e.g. Aerial-01","Tiret (-) ex. Aerial-01","Bindestrich (-) z. B. Aerial-01","Trattino (-) es. Aerial-01"},
        {"Espacio p. ej. Aerial 01","Space e.g. Aerial 01","Espace ex. Aerial 01","Leerzeichen z. B. Aerial 01","Spazio es. Aerial 01"},
        {"Vista previa. Solo se aplica al pulsar Guardar.","Preview. Changes apply when you click Save.","Aperçu. Les modifications s’appliquent après enregistrement.","Vorschau. Änderungen gelten nach dem Speichern.","Anteprima. Le modifiche si applicano dopo il salvataggio."},
        {"Grupo / archivo","Group / file","Groupe / fichier","Gruppe / Datei","Gruppo / file"},
        {"Videos","Videos","Vidéos","Videos","Video"},
        {"Todos los videos","All videos","Toutes les vidéos","Alle Videos","Tutti i video"},
        {"%1 grupos. Cada grupo aporta la cantidad configurada por ronda.\n%2","%1 groups. Each group plays its configured number of entries per round.\n%2","%1 groupes. Chaque groupe joue le nombre d’éléments prévu par cycle.\n%2","%1 Gruppen. Pro Runde spielt jede Gruppe die eingestellte Anzahl.\n%2","%1 gruppi. Ogni gruppo riproduce il numero impostato per ciclo.\n%2"},
        {"Usa Asignar grupo en la playlist para seleccionar sus miembros.","Use Assign group in the playlist to select its members.","Utilisez Attribuer un groupe dans la liste pour choisir ses membres.","Wähle die Einträge über Gruppe zuweisen in der Wiedergabeliste.","Usa Assegna gruppo nella playlist per scegliere gli elementi."},
        {"Se usa el texto anterior al primer separador. Si no aparece, se usa el nombre completo. Revisa antes de guardar.","Uses the text before the first separator, or the full name if absent. Check before saving.","Utilise le texte avant le premier séparateur, ou le nom complet en son absence. Vérifiez avant d’enregistrer.","Verwendet den Text vor dem ersten Trennzeichen, sonst den vollständigen Namen. Vor dem Speichern prüfen.","Usa il testo prima del primo separatore oppure il nome intero. Verifica prima di salvare."},
        {"Cada carpeta contenedora es un grupo. Activa Incluir subcarpetas al importar si lo necesitas.","Each parent folder is a group. Enable Include subfolders when importing if needed.","Chaque dossier parent forme un groupe. Activez l’inclusion des sous-dossiers si nécessaire.","Jeder übergeordnete Ordner bildet eine Gruppe. Bei Bedarf Unterordner beim Import einschließen.","Ogni cartella di origine è un gruppo. Se necessario, includi le sottocartelle durante l’importazione."},
        {"Todos los videos pertenecen a una sola bolsa.","All videos share one shuffle pool.","Toutes les vidéos sont mélangées ensemble.","Alle Videos werden gemeinsam gemischt.","Tutti i video vengono mescolati insieme."},
        {"%1 vídeos añadidos","%1 videos added","%1 vidéos ajoutées","%1 Videos hinzugefügt","%1 video aggiunti"},
        {"No se añadieron vídeos nuevos","No new videos added","Aucune nouvelle vidéo ajoutée","Keine neuen Videos hinzugefügt","Nessun nuovo video aggiunto"},
        {"Buscando vídeos…","Looking for videos…","Recherche de vidéos…","Videos werden gesucht…","Ricerca video…"},
        {"Playlist Manager · OBS Playlist NEXUS","Playlist Manager · OBS Playlist NEXUS","Gestionnaire de listes · OBS Playlist NEXUS","Wiedergabelisten-Verwaltung · OBS Playlist NEXUS","Gestore playlist · OBS Playlist NEXUS"},
        {"NEXUS  /  VIDEO PLAYLIST","NEXUS  /  VIDEO PLAYLIST","NEXUS  /  LISTE VIDÉO","NEXUS  /  VIDEO-WIEDERGABELISTE","NEXUS  /  PLAYLIST VIDEO"},
        {"Playlist Manager","Playlist Manager","Gestionnaire de listes","Wiedergabelisten-Verwaltung","Gestore playlist"},
        {"Tus vídeos, en reproducción continua.","Your videos, playing continuously.","Vos vidéos en lecture continue.","Deine Videos in Endlosschleife.","I tuoi video in riproduzione continua."},
        {"Playlist","Playlist","Liste de lecture","Wiedergabeliste","Playlist"},
        {"Añadir vídeos","Add videos","Ajouter des vidéos","Videos hinzufügen","Aggiungi video"},
        {"Añadir carpeta","Add folder","Ajouter un dossier","Ordner hinzufügen","Aggiungi cartella"},
        {"Quitar","Remove","Retirer","Entfernen","Rimuovi"},
        {"Subir","Move up","Monter","Nach oben","Sposta su"},
        {"Bajar","Move down","Descendre","Nach unten","Sposta giù"},
        {"Arrastra vídeos o una carpeta a la lista.","Drag videos or a folder into the playlist.","Glissez des vidéos ou un dossier dans la liste.","Ziehe Videos oder einen Ordner in die Liste.","Trascina i video o una cartella nella playlist."},
        {"Asignar grupo","Assign group","Attribuer un groupe","Gruppe zuweisen","Assegna gruppo"},
        {"Incluir subcarpetas al importar","Include subfolders when importing","Inclure les sous-dossiers à l’importation","Unterordner beim Import einschließen","Includi sottocartelle durante l’importazione"},
        {"Reproducción","Playback","Lecture","Wiedergabe","Riproduzione"},
        {"ACTUAL","NOW PLAYING","EN COURS","AKTUELL","IN CORSO"},
        {"Anterior","Previous","Précédent","Zurück","Precedente"},
        {"Reproducir","Play","Lire","Wiedergabe","Riproduci"},
        {"Pausa","Pause","Pause","Pause","Pausa"},
        {"Siguiente","Next","Suivant","Weiter","Successivo"},
        {"Orden","Order","Ordre","Reihenfolge","Ordine"},
        {"Secuencial","Sequential","Séquentiel","Der Reihe nach","Sequenziale"},
        {"Aleatorio","Shuffle","Aléatoire","Zufällig","Casuale"},
        {"Aleatorio equilibrado por grupos","Balanced group shuffle","Aléatoire équilibré par groupe","Ausgewogene Gruppenmischung","Casuale bilanciato per gruppi"},
        {"BUCLE CONTINUO","CONTINUOUS LOOP","BOUCLE CONTINUE","ENDLOSSCHLEIFE","CICLO CONTINUO"},
        {"Vídeos (*.mp4 *.mkv *.mov *.webm)","Videos (*.mp4 *.mkv *.mov *.webm)","Vidéos (*.mp4 *.mkv *.mov *.webm)","Videos (*.mp4 *.mkv *.mov *.webm)","Video (*.mp4 *.mkv *.mov *.webm)"},
        {"Selecciona primero los videos que quieres agrupar.","Select the videos you want to group first.","Sélectionnez d’abord les vidéos à regrouper.","Wähle zuerst die Videos aus, die du gruppieren möchtest.","Seleziona prima i video da raggruppare."},
        {"Nombre del grupo (vacio = Sin grupo)","Group name (blank = Ungrouped)","Nom du groupe (vide = Sans groupe)","Gruppenname (leer = Ohne Gruppe)","Nome del gruppo (vuoto = Senza gruppo)"},
        {"%1 s  ·  EN DIRECTO","%1 s  ·  LIVE","%1 s  ·  EN DIRECT","%1 s  ·  LIVE","%1 s  ·  IN DIRETTA"},
        {"  ·  ACTUAL · PAUSA","  ·  CURRENT · PAUSED","  ·  EN COURS · PAUSE","  ·  AKTUELL · PAUSE","  ·  CORRENTE · IN PAUSA"},
        {"  ·  REPRODUCIENDO","  ·  PLAYING","  ·  EN LECTURE","  ·  WIEDERGABE","  ·  IN RIPRODUZIONE"},
        {"  ·  SIGUIENTE LISTO","  ·  NEXT READY","  ·  SUIVANT PRÊT","  ·  NÄCHSTER BEREIT","  ·  SUCCESSIVO PRONTO"},
        {"  ·  PREPARANDO","  ·  PREPARING","  ·  PRÉPARATION","  ·  WIRD VORBEREITET","  ·  IN PREPARAZIONE"},
        {"Sin grupos: todos los videos en una bolsa.","No groups: all videos share one shuffle pool.","Sans groupes : toutes les vidéos sont mélangées ensemble.","Keine Gruppen: Alle Videos werden gemeinsam gemischt.","Nessun gruppo: tutti i video vengono mescolati insieme."},
        {"%1 grupos · %2 videos. Sin asignar participa como Sin grupo.","%1 groups · %2 videos. Unassigned entries are grouped under Ungrouped.","%1 groupes · %2 vidéos. Les éléments non attribués sont classés Sans groupe.","%1 Gruppen · %2 Videos. Nicht zugewiesene Einträge erscheinen unter Ohne Gruppe.","%1 gruppi · %2 video. Gli elementi non assegnati rientrano in Senza gruppo."},
        {"Añade vídeos para empezar","Add videos to get started","Ajoutez des vidéos pour commencer","Füge Videos hinzu, um zu beginnen","Aggiungi video per iniziare"},
        {"Siguiente: -","Next: –","Suivant : –","Weiter: –","Successivo: –"},
        {"Siguiente: ","Next: ","Suivant : ","Weiter: ","Successivo: "},
        {"%1 vídeos","%1 videos","%1 vidéos","%1 Videos","%1 video"},
        {"SIN VÍDEOS","NO VIDEOS","AUCUNE VIDÉO","KEINE VIDEOS","NESSUN VIDEO"},
        {"ERROR · Sin vídeos utilizables","ERROR · No playable videos","ERREUR · Aucune vidéo lisible","FEHLER · Keine abspielbaren Videos","ERRORE · Nessun video riproducibile"},
        {"READY · Siguiente vídeo preparado","READY · Next video prepared","PRÊT · Vidéo suivante prête","BEREIT · Nächstes Video vorbereitet","PRONTO · Video successivo pronto"},
        {"PREPARANDO · Siguiente vídeo","PREPARING · Next video","PRÉPARATION · Vidéo suivante","VORBEREITUNG · Nächstes Video","PREPARAZIONE · Video successivo"},
        {"PAUSA · ","PAUSED · ","PAUSE · ","PAUSE · ","IN PAUSA · "},
        {"FUENTE OCULTA · ","SOURCE HIDDEN · ","SOURCE MASQUÉE · ","QUELLE AUSGEBLENDET · ","SORGENTE NASCOSTA · "},
        {"PREPARANDO SALTO · ","PREPARING SKIP · ","PRÉPARATION DU SAUT · ","SPRUNG WIRD VORBEREITET · ","PREPARAZIONE SALTO · "},
        {"Incidencias de continuidad: %1","Continuity issues: %1","Incidents de continuité : %1","Wiedergabeunterbrechungen: %1","Problemi di continuità: %1"},
        {"El vídeo actual terminará antes de continuar con la lista.","The current video will finish before the playlist continues.","La vidéo en cours se termine avant la reprise de la liste.","Das aktuelle Video läuft zu Ende, bevor die Liste fortgesetzt wird.","Il video corrente terminerà prima di proseguire con la playlist."},
        {"La reproducción continúa al editar el orden de la lista.","Playback continues while you reorder the playlist.","La lecture continue pendant la réorganisation de la liste.","Die Wiedergabe läuft beim Umsortieren weiter.","La riproduzione continua mentre riordini la playlist."},
        {"No se pudo preparar el siguiente vídeo","Could not prepare the next video","Impossible de préparer la vidéo suivante","Das nächste Video konnte nicht vorbereitet werden","Impossibile preparare il video successivo"},
        {"No se pudo abrir el vídeo seleccionado","Could not open the selected video","Impossible d’ouvrir la vidéo sélectionnée","Das ausgewählte Video konnte nicht geöffnet werden","Impossibile aprire il video selezionato"},
        {"Archivo omitido: ","Skipped file: ","Fichier ignoré : ","Datei übersprungen: ","File saltato: "},
        {" vídeos cargados"," videos loaded"," vidéos chargées"," Videos geladen"," video caricati"},
        {"Abrir Playlist Manager","Open Playlist Manager","Ouvrir le gestionnaire de listes","Wiedergabelisten-Verwaltung öffnen","Apri gestore playlist"},
        {"Al mostrar la fuente","When source becomes visible","À l’affichage de la source","Beim Einblenden der Quelle","Quando la sorgente diventa visibile"},
        {"Al ocultar la fuente","When source becomes hidden","Au masquage de la source","Beim Ausblenden der Quelle","Quando la sorgente viene nascosta"},
        {"Continuar","Continue","Continuer","Fortsetzen","Continua"},
        {"Reiniciar playlist","Restart playlist","Recommencer la liste","Wiedergabeliste neu starten","Riavvia playlist"},
        {"Pausar","Pause","Mettre en pause","Pausieren","Metti in pausa"},
        {"OBS Playlist NEXUS","OBS Playlist NEXUS","OBS Playlist NEXUS","OBS Playlist NEXUS","OBS Playlist NEXUS"},
        {"Playlist NEXUS: Reproducir","Playlist NEXUS: Play","Playlist NEXUS : Lire","Playlist NEXUS: Wiedergabe","Playlist NEXUS: Riproduci"},
        {"Playlist NEXUS: Pausa","Playlist NEXUS: Pause","Playlist NEXUS : Pause","Playlist NEXUS: Pause","Playlist NEXUS: Pausa"},
        {"Playlist NEXUS: Siguiente","Playlist NEXUS: Next","Playlist NEXUS : Suivant","Playlist NEXUS: Weiter","Playlist NEXUS: Successivo"},
        {"Playlist NEXUS: Anterior","Playlist NEXUS: Previous","Playlist NEXUS : Précédent","Playlist NEXUS: Zurück","Playlist NEXUS: Precedente"},
    };
    const char *locale=obs_get_locale();
    const std::string_view language=locale?locale:"";
    const auto column=language.starts_with("es")?0:language.starts_with("fr")?2:
        language.starts_with("de")?3:language.starts_with("it")?4:1;
    return catalog[static_cast<size_t>(key)][column];
}
inline QString ui(Text key){return QString::fromUtf8(localized(key));}
}
