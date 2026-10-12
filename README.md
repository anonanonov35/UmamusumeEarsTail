# Uma Ears and Tail — runtime source, mod 0.2.5

Complete project-specific C++ source for the `uma_runtime.dll` included in Uma Ears and Tail 0.2.5 (and the same DLL in 0.2.4). This repository is prepared for Nexus Mods source review. It contains the runtime, the exact target-build compatibility metadata, the pinned MinHook sources, configuration, build/test scripts and a hash inventory of the separate resource payload.

The DLL adds appearance-editor settings, independent ear RGB or hair-colour synchronization, native Hair material updates, ear motion and a five-bone tail simulation. The tail includes the accepted 8-second idle swish and map-origin/duplicate-update fixes. The mesh and message bundles are distributed separately; changing the menu titles in 0.2.5 did not change the DLL.

## Build

On Windows x64, install **Zig 0.13.0**, then run:

```powershell
.\build.ps1 -ZigPath 'C:\path\to\zig.exe' -RunTests
```

Output: `out/uma_runtime.dll`. No installed game, Python packages, downloaded assets or private build files are needed to compile. MinHook is included at the commit recorded in `release-manifest.json`. The build script does not download or install anything. See [BUILDING.md](BUILDING.md) for scope and comparison details.

## Review and release correspondence

- [src/uma_runtime.cpp](src/uma_runtime.cpp): DLL entry point, guarded hooks, appearance settings, Hair shader updates and runtime sampling.
- [src/ear_settings.h](src/ear_settings.h): selector encoding and independent tail/eyepatch settings.
- `src/ear_motion.*`, `ear_idle.h`, `ear_transform.h`: ear animation and movement.
- `src/tail_*.h`: tail simulation, riding classification, map-origin tracking and transforms.
- `compat/eldenring_1_17/`: numeric RVAs, hash guards and singleton-site metadata for the tested executable. These headers contain no game executable instructions or decompiled functions.
- `third_party/minhook/`: unmodified upstream hook library and its original license.
- `tests/`: standalone native regression tests, with historical project-authored solver fixtures under `tests/fixtures/`.
- [release-manifest.json](release-manifest.json): source file hashes, dependency revision and the exact distributed DLL/ZIP/7z hashes.
- [RESOURCES.md](RESOURCES.md) and [resources-manifest.json](resources-manifest.json): roles, provenance, sizes and SHA-256 of all 165 mod resources; no asset contents.
- [REVIEW.md](REVIEW.md): process/file access summary and limitations.

The distributed DLL SHA-256 is **`ac04594cd98d140320e7917d89b7502b0806911fc5466eb366866f703fa141c9`**. The original private-build DLL hash and the packaging change are recorded separately: packaging redacted one personal path in a discardable debug section, leaving the executable/core sections unchanged.

`python tools/verify_release.py` checks this source inventory. `python tools/verify_release.py --dll <downloaded-DLL>` checks the exact original distributed file. `--rebuilt <DLL>` compares a rebuild's PE sections without loading it. Normal linker timestamp/debug identity differences are distinguished from code/data differences; a different full-file hash is not represented as an exact reproduction.

The exported sources were locally rebuilt with Zig 0.13.0 and all **seven native tests passed**. The rebuilt entry point and raw `.text`, `.rdata`, `.data`, `.pdata`, `.tls` and `.reloc` sections match the distributed DLL exactly. The full DLL hash differs because the linker timestamp, `.buildid` and one discardable debug section differ. The successful build and section comparison are recorded in `release-manifest.json`; this is not a claim of full-file byte identity.

## Scope and licenses

This source archive contains no models, game resource containers, shader bytecode, save files, original game executables, binary DLLs or private project history. It is not an installable mod archive. ModEngine2 and the released game resource payload remain separate.

MinHook retains its upstream license; compiler/runtime notices are in [THIRD_PARTY.md](THIRD_PARTY.md). No new license grant for the project-specific code has been selected in this review snapshot. Publication for inspection does not license third-party models or Elden Ring content. Project design and game testing: the repository owner. Implementation and locally authored motion code: Codex (GPT-6 family), reviewed in game by the user. No fal.ai service generated the source or assets.
