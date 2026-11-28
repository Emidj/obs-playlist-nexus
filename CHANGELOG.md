# Changelog

## 1.0.3 — 2026-11-28

Changes since the public 1.0.0 release:

- Added Cut, Dissolve, Slide in from left, Slide in from right and Zoom in transitions in source properties.
- Added transition duration from 50 to 2000 ms. Cut remains the default; the default animated duration is 400 ms.
- Added transitions between local videos and live OBS sources/scenes.
- Matched local-video transitions in the Playlist Manager preview using the same motion calculation as OBS output.
- Clipped sliding video to the source canvas and preserved graphics blending state during live-source transition capture.
- Added translations for the new controls in English, Spanish, French, German and Italian.

Video only. This update does not add audio playback or audio crossfades. The outgoing local clip holds its last frame during an animated transition. Live entries continue to use the live-entry placeholder in the manager preview.

## 1.0.0 — 2026-11-20

Initial public release: continuous video playlists, sequential/shuffle/balanced modes, category quotas, OBS source and scene entries, Playlist Manager, visibility policies, hotkeys and five languages.
