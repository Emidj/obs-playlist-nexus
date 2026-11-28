# Third-party dependencies in release 1.0.3

The installer bundles the production Playlist NEXUS DLL and unmodified runtime DLLs from the official OBS dependency bundle **2025-07-11, Windows x64**. All ten third-party DLLs were matched byte-for-byte against that bundle. The release includes a binary SHA-256 manifest, complete dependency notices and a dependency source archive with upstream sources, pinned revisions, build recipes and Windows patches.

## Binary licensing

FFmpeg reports **GPL version 3 or later** and was built with `--enable-gpl --enable-version3`. The combined binary distribution follows **GPL-3.0-or-later**. Playlist NEXUS's own source remains **GPL-2.0-or-later**, which permits this combination. See LICENSE and COPYING-GPLv3.txt.

| Dependency | Version | Role / license |
| --- | --- | --- |
| FFmpeg | 7.1.1, db69d06eeeab4f46da15030a80d539efb4503ca8 plus OBS patches | Video decoding and scaling; GPL-3.0-or-later in this build |
| x264 | r3106, eaa68fad9e5d201d42fde51665f2d137ae96baf0 | FFmpeg dependency; GPL-2.0-or-later |
| zlib | 1.3.1 | Compression; zlib license |
| librist | 0.2.7, 809390b3b75a259a704079d0fb4d8f1b5f7fa956 | RIST transport dependency; BSD-2-Clause and bundled component notices |
| SRT | 1.5.2 | Transport dependency; MPL-2.0 |
| curl | 8.12.1 | Runtime dependency; curl license |

Additional statically linked codec/crypto dependencies are supplied in the dependency source archive: AOM 3.12.0, SVT-AV1 2.3.0, Opus 1.5.2, Ogg 1.3.5, Vorbis 1.3.7, VPX 1.14.1, Theora 1.1.1, LAME 3.100 and mbedTLS 3.6.4. Header sources and auxiliary sources required by the build recipes are also included. Their original licenses remain in the source trees and the installer archive's licenses directory.

## Source downloads

The [same release page](https://github.com/Emidj/obs-playlist-nexus/releases/tag/v1.0.3) provides:

- **Sources.zip**: Playlist NEXUS source, installer source and build instructions.
- **Dependency-Sources.zip**: upstream dependency source archives and the complete OBS build recipes and patches. See its README and source-manifest.json for extraction and revision details.

Qt Widgets/Core/Gui and libobs are not redistributed by this installer. A compatible OBS installation supplies them. OBS is GPL-2.0-or-later; Qt licensing depends on its distribution. Project references: https://github.com/obsproject/obs-studio, https://www.qt.io/licensing/, https://ffmpeg.org/legal.html.

The fictional Night Current example artwork was created for this project. No personal media library files are included. OBS and Qt names identify dependencies and do not imply endorsement.
