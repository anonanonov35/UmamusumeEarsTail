# Building and checking the reviewed source

Requirements: Windows x64, PowerShell, and Zig **0.13.0**. Zig includes its C/C++ compiler and standard libraries. It is a separate build tool, not bundled here. Obtain it from the [official Zig downloads](https://ziglang.org/download/).

Run `./build.ps1 -ZigPath '<path-to-zig.exe>' -RunTests`. The script compiles four MinHook C units and `src/uma_runtime.cpp` plus `src/ear_motion.cpp` using `-O2 -std=c++17 -target x86_64-windows-gnu`. It writes only the repository's `out/` folder, restores previous cache environment variables, and neither starts nor attaches to a game. The seven native tests require no game/assets. The optional real-game CSV input supported by the map-origin test is not bundled and is not needed for its standalone test.

Headers in `compat/eldenring_1_17/` are the exact numeric compatibility metadata used by the submitted runtime. The historical local build generated these tables from the author's installation before compilation; publishing the captured tables avoids any dependency on that installation. They are not a portable address database. Regenerating guards for a newer game build alone does not make the hooks compatible.

Target tested during mod development: Elden Ring App/Calibrations 1.17, executable SHA-256 `d1a84083c6c7c7902162ff098f7d86812839aa6b3575959398857e539c488134`, PE timestamp `0x69e9c9b9`, image size `98604544`. The DLL checks PE identity and function hash guards; it does not compute a full executable SHA at runtime. It refuses initialization if either named Easy Anti-Cheat module is present. This is the normal offline mod-loader route, with no anti-cheat bypass implementation.

For file correspondence, `release-manifest.json` records the exact distributed DLL hash and raw PE section hashes. Run `python tools/verify_release.py` to verify source contents (Python 3 standard library only). Use `--dll` for a copy of the distributed DLL and `--rebuilt` for a local compile. Linker timestamps, PDB/build identifiers, debug paths and different compiler versions can change binary bytes. Exact-source checks and rebuilt section checks are reported separately. The shipped archive remains unchanged by this source-review task.

A local rebuild is for review. It does not rebuild or redistribute the mod's mesh, texture, regulation or message bundles; those files are unnecessary for the DLL compiler and outside this source review. Complete preservation/save-reload of every appearance setting and two-client Seamless Co-op behavior remain separate testing tasks.

## Recorded local result

The source snapshot successfully built on Windows with Zig 0.13.0. All seven standalone executables completed with exit code 0: settings encoding, ear transforms, idle stances, tail simulation, map-origin/duplicate-update handling, tail idle swish and fast-travel/riding behavior. Compiler warnings came from Zig's bundled libunwind declarations; they did not prevent the build.

The rebuilt DLL entry point and six functional PE sections (`.text`, `.rdata`, `.data`, `.pdata`, `.tls`, `.reloc`) are byte-exact to the submitted DLL. `.buildid`, one discardable debug section (`/33`) and the PE linker timestamp differ. The rebuilt full-file SHA therefore differs. See `local_build_verification` in `release-manifest.json` for both hashes and the comparison.

Resource inventory comparison is optional: `python tools/verify_release.py --mod-root '<extracted-archive-root>'`. It reads the separate resource files without loading any executable. See [RESOURCES.md](RESOURCES.md).
