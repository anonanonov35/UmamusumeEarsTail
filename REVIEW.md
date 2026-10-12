# Runtime behavior for source review

The entry point is `DllMain` in `src/uma_runtime.cpp`. Process attach creates one worker thread, which obtains the directory of its own loaded module, opens `uma-runtime.log` there, reads the neighboring `uma-ears.ini`, checks the current process for `eldenring.exe` and the two Easy Anti-Cheat module names, and validates the exact build before installing MinHook detours.

MinHook installs in-process detours for appearance-editor getters/setters, palette refresh, matrix copy and color application. The mod maps its private menu settings to existing appearance data, updates its material IDs 64/65, samples each player actor independently, and poses the private ear/tail bones. Matrix/render writes and appearance-data reads use `GetCurrentProcess()`; the runtime does not call `OpenProcess` to attach to unrelated processes. The external DLL is loaded by ModEngine2.

The project runtime has no HTTP/network client, telemetry upload, downloader, shell-command launcher, startup registration or service installer. Its file access consists of the adjacent INI and diagnostic log. MinHook uses the normal executable-memory/protection APIs required for detours; this is visible in its vendored source. No code was obfuscated for this source export.

The support request is about the DLL. This repository supplies all project translation units and headers needed to build it, plus the licensed source dependency. Game binaries, disassembly logs, original textures, extracted content and models are excluded. Numeric compatibility metadata and hashes are retained so the reviewer can build the actual guarded implementation instead of a stub.

The source export is not a guarantee of malware-scanner clearance or a Nexus approval decision. It supplies inspection material and file correspondence; the moderation team makes that decision.
