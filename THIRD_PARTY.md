# Dependencies and notices

- [MinHook](https://github.com/TsudaKageyu/minhook), commit `8af6b4acae5a9388fd742b56fa79ece89d96f823`. `include/` and `src/` are included unmodified with the upstream README, AUTHORS and [complete license](third_party/minhook/LICENSE.txt). The license also covers the included Hacker Disassembler Engine portions by Vyacheslav Patkov.
- Zig 0.13.0 supplies the compiler and standard C/C++ runtime sources. Zig itself is not included in this repository. Notices matching the runtime components linked in the original DLL are retained under `licenses/`: LLVM libc++, libc++abi, libunwind and mingw-w64.
- ModEngine2 is a separate loader requirement and is not bundled: https://github.com/soulsmods/ModEngine2 .

No license for project-specific code or third-party character models is newly assigned by this source-review package. The separate mod credits describe the third-party asset provenance; none of those models are included here.
