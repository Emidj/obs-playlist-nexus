# Third-party dependencies

OBS Playlist NEXUS uses the public APIs of these independently licensed projects. Their source or runtime binaries are not included in this repository.

| Project | Purpose | Licensing information |
| --- | --- | --- |
| OBS Studio / libobs | Source lifecycle, rendering and scenes | GPL-2.0-or-later; https://github.com/obsproject/obs-studio |
| Qt 6 Widgets / Gui / Core | Playlist Manager interface | LGPL-3.0 / GPL / commercial options, depending on component and distribution; https://www.qt.io/licensing/ |
| FFmpeg 7 | Local video demuxing, decoding and scaling | LGPL-2.1-or-later by default; GPL-enabled builds have additional obligations; https://ffmpeg.org/legal.html |

Some FFmpeg binary builds pull in x264, zlib, librist, SRT, curl and other libraries. Their licensing and source obligations depend on the exact build configuration. Do not assume that this project's license covers dependency DLLs.

Before redistributing binaries, retain the complete dependency license notices and provide corresponding source as required by each applicable license. This source publication deliberately does not redistribute the local development SDK or its runtime DLLs.

The fictional Night Current example graphics were created specifically for this repository. They contain no third-party event artwork or personal media library content. OBS and Qt names identify dependencies and do not imply endorsement.
