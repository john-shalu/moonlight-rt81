# Attribution and modifications

Moonlight RT for Windows RT 8.1, version 0.2.1, 2026-10-27.
Copyright (c) 2026 john-shalu and contributors for the new Windows RT integration.
The combined application is released under GNU GPL version 3, without warranty.

This is an independent, unofficial community port. It uses Moonlight common-c for the streaming protocol. The native Win32 interface, Windows RT loader/toolchain integration, Media Foundation/D3D11 renderer, system audio output, installation scripts and input/session adaptations are provided by this project. It does not claim authorship of upstream Moonlight or its dependencies.

Changes for Windows RT include ARM32/MSVCRT targeting, a Media Foundation H.264 hardware-decoding path with D3D11 presentation, stereo audio through Opus and Windows audio APIs, DPAPI-protected pairing identity and pinned host certificates, a native host/app/settings interface, absolute touch-as-mouse and relative game mouse, and asynchronous input/logging with bounded session cleanup. The 2026-10-27 public build also remaps compiler paths and excludes generated/private data from releases.

## Components

| Component | Attribution / license |
| --- | --- |
| Moonlight common-c | Moonlight contributors; GPL-3.0-or-later. Combined release selects GPLv3. |
| ENet | Lee Salzman and contributors; MIT. |
| NanoRS | Its upstream authors; MIT. |
| Mbed TLS 3.6.7 | Mbed TLS contributors; Apache-2.0 option of its dual license. Framework license retained. |
| Opus 1.5.2 | Xiph.Org and listed contributors; BSD-style copyright and patent notices retained. |
| pugixml 1.15 | Arseny Kapoulkine and contributors; MIT. |
| Earlier decoder probe derivatives and generated clips | Copyright (c) 2026 john-shalu; MIT notice retained. |
| Moonlight icon | moonlight-qt contributors; GPLv3. Exact upstream commit/path/hash in assets/UPSTREAM.json. |
| Compiler runtime | LLVM Apache-2.0 with LLVM exceptions; MinGW runtime notices retained. |

Full license texts are in `LICENSE` and `licenses/`. Source-file copyright and license notices remain authoritative for each third-party component. Dependency repositories and exact revisions/hashes are recorded in `pins.json`.

The project name describes interoperability. This release is not endorsed by Moonlight, Microsoft, NVIDIA or the device vendors. Their names and marks remain their property.
