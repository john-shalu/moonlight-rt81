# Moonlight RT for Windows RT 8.1

An **unofficial community Moonlight client for unlocked Windows RT 8.1 ARM32**, tested on Microsoft Surface 2 (Tegra 4). It uses the tablet's original Windows drivers for H.264 hardware decoding, stereo audio, keyboard, mouse and touch-as-mouse input.

[中文说明](README.zh-CN.md) · [Download](https://github.com/john-shalu/moonlight-rt81/releases/latest) · [Build from source](docs/BUILD.md) · [Licenses and attribution](NOTICE.md)

This is a native Win32 port built on [Moonlight common-c](https://github.com/moonlight-stream/moonlight-common-c), not the official Moonlight Qt application. It is not affiliated with or endorsed by the Moonlight project or Microsoft.

## Download and run

1. Download **Moonlight-WindowsRT81-0.2.1.zip** from Releases and extract the entire folder. The executable is ARM32, not x86/x64/ARM64. Your Windows RT installation must already permit third-party ARM desktop applications.
2. Run `Install-Moonlight.cmd` for a per-user installation and desktop/Start-menu shortcuts, or run `Start-Moonlight.cmd` from the extracted folder. No administrator rights are needed for installation.
3. Add your Sunshine computer's address. Enter the displayed PIN in Sunshine's web interface, select the computer, then select Desktop or an application.

Start at **720p, 30 FPS, 5 Mbps, H.264**. The settings also offer 1080p and 60 FPS, but these higher settings are not performance guarantees. This Windows client has decoded both CAVLC and CABAC on the tested Surface 2; it does not require the CAVLC workaround used by some Linux Tegra decoders.

## Input and controls

- Touch clicks and drags use absolute mouse coordinates. Physical keyboard and mouse are forwarded while the stream has focus.
- Local cursor is hidden over the video. For games, enable **game mouse** in the stream menu or press **Ctrl+Alt+Shift+M** for relative motion without hitting the tablet's screen edges. Use the same shortcut to release it. Touching local controls or switching away also releases capture.
- **Ctrl+Alt+Shift+Q** disconnects. The top-right menu also switches fullscreen and disconnects; the host application stays running.
- Version 0.2.1 moves protocol input and disk logging off the window thread and improves session cleanup for repeated connections.

Supports saved computers, PIN pairing and host-certificate pinning, app cards/covers, H.264 video and stereo audio. LAN discovery, gamepads, clipboard, native multitouch, local IME composition, HEVC, AV1, HDR and YUV444 are not implemented. Touch is mouse emulation. There is no Qt/FFmpeg/UCRT dependency.

## Tested scope

The device owner tested v0.2.1 on **Surface 2 / Windows RT 8.1** and reports normal video, audio and input after the cursor/reconnect fixes. Earlier on-device decoder tests displayed 120 frames each of baseline/CAVLC, main/CABAC and high/CABAC+8x8 video with hardware decode calls. A headless dispatcher test also covered blocked transport, 10,000 queued motion events, state release and 50 session cycles.

The public binary is rebuilt with build-path remapping, a license notice/viewer in diagnostics, and clean packaging; its streaming and input source is unchanged from the owner-tested version. This public rebuild has compilation/static checks, rather than a separate on-device retest. Other Windows RT models, sustained 1080p/60 FPS and suspend/resume remain unverified. See [validation](validation/) for the specific scope; this is a community release, not a broad compatibility certification.

## Profiles and troubleshooting

Pairing identity, pinned certificates, settings and local logs are stored in `%LOCALAPPDATA%\MoonlightRT81`. Installation and uninstallation preserve this directory. The pairing private key is protected with the current Windows user's DPAPI. The application does not upload logs automatically. Logs may contain computer names/addresses: review and redact them before posting an issue. Do not upload your profile or pairing files.

Settings → Help/diagnostics provides local self-tests and the log folder. The three included H.264 clips are project-generated test animations. Local audio codec tests do not by themselves verify audible speaker output.

## Source and license

The combined application is distributed under **GNU GPL version 3**. You may use, modify and redistribute it under that license, without warranty. Preserve the notices and provide matching corresponding source when distributing binaries. Each release includes a **complete corresponding-source archive**, containing the pinned dependency sources and build/install scripts; GitHub's automatic repository ZIP alone does not include those dependencies.

Original probe-derived code retains its MIT notice. Moonlight common-c, ENet, NanoRS, Mbed TLS, Opus, pugixml and toolchain components retain their own licenses and copyrights. The upstream Moonlight icon and its exact source revision are documented in [assets/UPSTREAM.json](assets/UPSTREAM.json). GPL does not grant trademark rights; Moonlight and other marks belong to their respective owners. See [LICENSE](LICENSE), [NOTICE.md](NOTICE.md) and [licenses/](licenses/).

No Windows system binaries, vendor drivers, firmware, bootloaders, unlock tools, personal profiles, pairing credentials or runtime logs are included.
