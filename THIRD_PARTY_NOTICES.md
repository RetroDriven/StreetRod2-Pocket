# Third-party licenses and notices

- [Street Rod 2 SDL3](https://github.com/kylofon/street-rod-2-sdl3), Krzysztof Kania: MIT, pinned v0.1.0. The build patches a staged copy and retains the upstream license. Combined releases include that notice.
- [openfpgaSDK](https://github.com/openfpgaOS/openfpgaSDK): Apache-2.0 and its included dependency notices. Compiled releases preserve the SDK/runtime notices in `Licenses/StreetRod2/`.
- [Opal v2.0.3](https://github.com/RealBitdancer/opal), Copyright (c) 2026 Bitdancer: MIT; original core by Shayde/Reality, public domain. Accurate effects use a staged optimized copy. The Opal notice is bundled with compiled releases.
- [Nuked OPL3](https://github.com/nukeykt/Nuked-OPL3): LGPL-2.1; see the fetched checkout's LICENSE. Used by the local offline music renderer, not the normal Pocket runtime. Its source is fetched separately.
- Port-specific integration and the dormant experimental Fast OPL2 source: project MIT license. Fast FM is unavailable in normal release playback.
- Original Street Rod 2 DOS executable, data, music, effects and artwork belong to the original rights holders and are not covered by the port's MIT license. The public repository excludes DOS resources and derived music/effects archives. Supplied platform banner artwork is retained as game artwork, not claimed as port-created artwork.

This project is not affiliated with Analogue or the original game's rights holders.

## Standalone Windows GamePrep utility

The separate GamePrep package bundles a Python runtime, Tcl/Tk interface, the upstream MIT music player, and precompiled native music/effects generators. Nuked OPL3 is linked into the offline music helper under LGPL-2.1; the package includes its license and complete corresponding helper/application source in Source/GamePrep-source.zip, including a rebuild recipe. The Opal effects helper retains its MIT notices. Python and Tcl/Tk notices are included in Licenses/GamePrep. No DOS resources, generated game audio, Git client or compiler is bundled. This companion tool is separate from the Pocket core ZIPs.
