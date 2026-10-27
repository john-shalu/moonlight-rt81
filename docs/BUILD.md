# Building and release contents

Use a modern Windows x64 development machine, PowerShell 7, Git and Ninja on PATH. A short checkout path is recommended. Windows RT itself is the deployment target, not the build machine.

```powershell
./Get-Sources.ps1
./Get-BuildTools.ps1
./build.ps1
```

Output: `work/build-armv7/MoonlightRT81.exe`. Build tools are downloaded from the vendors' official releases and checked against SHA-256 pins. `pins.json` records all dependency revisions. Existing dependency folders in the complete source ZIP are reused, so a matching source release does not need to fetch dependency source again. Start with a clean `work/` when building directly from a Git checkout; the source-fetch script trusts existing directories.

The toolchain is LLVM-MinGW 20261020 **MSVCRT**, targeting ARM32/Thumb2 and Windows 6.3. CMake 4.4.4 configures static Moonlight common-c, Mbed TLS 3.6.7, fixed-point Opus 1.5.2 and pugixml 1.15. System Win32/Media Foundation/D3D11 DLLs are used on the tablet and are not redistributed. The toolchain and CMake are build tools, not runtime requirements. `-Architecture x86_64` is for developer tests only.

CMake remaps project/toolchain prefixes in assertions and compiler metadata to avoid embedding local usernames. `build.ps1` disables Python bytecode generation. Release packaging excludes `.git`, Python caches, build outputs and private profiles by allowlist. Do not package the development `work/` folder wholesale.

## Create a release

After building, use Python 3 to package the runtime and complete corresponding source:

```powershell
python tests/package_release.py
```

This creates `dist/Moonlight-WindowsRT81-0.2.1.zip`, its matching `-source.zip`, and `SHA256SUMS.txt`. The source archive contains the exact dependency source used for the build (unused Mbed TLS tests/programs/docs and Git metadata are omitted). The public repository itself omits fetched dependencies; the source archive is supplied alongside each binary for GPL compliance.

Before publishing, compile that staged corresponding source with the same toolchain, inspect ARMNT/subsystem 6.3/imports/security-cookie metadata, audit executable strings and archive entries, and compare the source/asset hashes in the manifest. This is not an automatic claim of byte-for-byte reproducible builds. No client GUI or real input needs to be launched for these checks.

## Tests

`tests/input_dispatch_test.cpp` links the real input dispatcher with stub protocol transport. It exercises ordering/coalescing, blocked transport, overflow reset, held-state release and repeated sessions without creating windows, reading input hardware or using a network.

`tests/mock_pairing.py` uses Python's `cryptography` package for a local protocol test with newly generated disposable fixtures under ignored `work/test-private/`. Those fixtures must never be published. The client also has opt-in local decoder/identity/codec self-tests using the included generated clips. Test scope and actual-device reports are recorded separately in `validation/`.
