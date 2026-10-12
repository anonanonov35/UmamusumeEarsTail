# Mod resource inventory

[resources-manifest.json](resources-manifest.json) lists the **165 resource files** in the separate Uma Ears and Tail 0.2.5 archive, with their archive-relative paths, byte sizes and SHA-256 hashes. It also records the runtime DLL, INI, launch script and loader configuration. The complete ZIP/7z hashes are in [release-manifest.json](release-manifest.json). No resource bytes are included here.

| Resource group | Files | Role |
| --- | ---: | --- |
| `mod/UmaEars/parts/*.partsbnd.dcx` | 160 | Appearance-part containers with the selected ear meshes and unified tail, including the game's part variants |
| `mod/UmaEars/parts/common_body.tpf.dcx` | 1 | Shared DDS texture storage, preserving 97 stock texture names and 66 mod texture names |
| `mod/UmaEars/material/allmaterial_dlc02.matbinbnd.dcx` | 1 | Material definitions used by the mod; ears and tail use the native Hair shader |
| `mod/UmaEars/regulation.bin` | 1 | Appearance parameters and menu definitions |
| `mod/UmaEars/msg/{engus,rusru}/menu_dlc02.msgbnd.dcx` | 2 | English/Russian appearance-editor strings, including Uma Ears and Uma Tail |

The runtime is built independently of these containers. They supply the models, private bones, materials and menu data that the compiled hooks operate on. The menu exposes off, Symboli Rudolf, Agnes Tachyon and Oguri Cap; the tail is separately controlled under Alter Body. This inventory documents the released payload, not a pipeline for rebuilding those assets.

To compare an already downloaded and extracted mod archive without executing it:

```powershell
python tools/verify_release.py --mod-root '<extracted-archive-root>'
```

This checks all 165 resources and the four runtime/loader files against the recorded sizes and hashes. `SHA256SUMS.txt` in this source repository covers the source-review package, not the separate mod payload. Hash matches establish file correspondence; they do not establish redistribution rights or guarantee moderation approval.

## Asset provenance

- Symboli Rudolf ears: [Aeicos on Cults](https://cults3d.com/en/3d-model/fashion/umamusume-symboli-rudolf-ears). The owner supplied the [CULTS PU license](https://cults3d.com/en/licenses#cults_pu), whose terms do not permit public redistribution of the digital model or adapted digital copies. A separate permission would be required for that use. The prior provenance audit also recorded the creator's statement of an Umamusume origin.
- Oguri Cap: [Printables model](https://www.printables.com/model/1619273-oguri-cap-umamusume/files). Agnes Tachyon: [Printables model](https://www.printables.com/model/1641016-agnes-tachyon-umamusume). The owner reported [CC0](https://creativecommons.org/publicdomain/zero/1.0/) for both. The model pages returned HTTP 403 during review, so their specific license association and original provenance were not independently confirmed. The unified tail was shaped using these two supplied models.
- Texture patterns and mod-specific motion/settings code were authored locally. Some resource containers retain or modify Elden Ring content; the shared bank explicitly retains stock textures. Publishing the DLL source does not grant rights to these game resources or to third-party models.

The user's requested GitHub material includes this inventory and hashes only. Any asset permissions concern remains separate from Nexus's request to inspect the DLL source.
