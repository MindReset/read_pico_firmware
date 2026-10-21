# Upstream PR Description (English, for submission to MindReset/read_pico_firmware)

> Ready-to-paste English PR description. Drafted for submission to the upstream author
> (MindReset). The two ⚠️ blocks below are intentional and must stay prominent.

---

## Suggested PR title

**Pico-Plus: landscape dual-column UI, offline dictionary & vocabulary book, and BLE text push (deep fork of r95)**

## Description

### Summary

Hi MindReset team — thank you for open-sourcing the RDP-G01-W sample firmware. This PR shares
**Pico-Plus**, a community fork built on your **r95 (0.3.3-rc95)** baseline, in the spirit of
giving back to the project and the hardware community.

We understand this fork is **not intended to be merged as-is** — it is a deep modification
with an independent partition layout and a custom BLE protocol (see the warnings below). We
are submitting it for visibility, attribution and discussion, and we are happy to adjust the
form if you prefer a different sharing channel.

### Motivation

The stock firmware is a solid reading experience, but the hardware (a 1216×684 e-paper panel)
sits mostly unused in portrait text mode, and two capabilities we wanted were missing: a
denser reading layout, and a way to get long text from a phone onto the device without the
Wi-Fi transfer dance. Pico-Plus explores exactly those two axes, plus offline dictionary
lookup for language learners.

### Core features

| Area | What we added |
|---|---|
| **Landscape + dual-column layout** | Rotated UI with two columns per page, halving page turns; toggleable in reading settings |
| **Faster page turns** | Partial-refresh pipeline without black flash / white clear; optional water-ripple animation |
| **Offline dictionary + vocabulary book** | In-reading word lookup from an ECDICT-derived `dict.bin` on the TF card; looked-up words auto-collected into a vocabulary book (user must supply the dictionary file — it is not bundled, for licensing/size reasons) |
| **BLE Text Push (geek secondary display)** | Android app pushes long text / web snippets / word cards over BLE; the device opens them as a "virtual book"; unread items surface as a dot on the home status bar |
| **Anti-cross-talk (MAC binding)** | Broadcast name derived from the factory MAC (e.g. `PicoPush-1A2B`); multi-device environments get a pick-list in the app instead of "connect to first found" |
| **Bluetooth clock sync** | The device has no RTC crystal and drifts; the phone calibrates the clock on push, and the firmware learns the drift rate offline |
| **Refresh / power polish** | Trimming unnecessary partial refreshes during the push-arming window; reworked sleep/lock paths |
| **Browser-based flashing** | Zero-install web flasher (esp-web-tools) publishing the exact binaries |

### ⚠️ Important: independent partition table (please read before testing)

This fork uses its **own 16 MB partition layout** — `partitions_16M.csv`, with **5.5 MB × 2
app partitions** and `otadata` at `0xb10000` (the upstream layout differs, e.g. the `0xD10000`
otadata placement). Consequences:

- **Devices cannot be mixed-flashed** between official firmware and this fork using the
  official tools — the first flash of Pico-Plus must be a full write from a computer, and
  going back to official firmware likewise requires a full erase + reflash.
- **The device's built-in "online upgrade" must not be used** with this fork (it checks the
  official firmware address and would restore the stock image).

We flag this prominently so nobody bricks a device by mixing flows.

### ⚠️ Important: custom low-level BLE protocol (please read before testing)

BLE Text Push uses a **project-specific BLE service UUID and framing protocol** (see
`components/push_sink/include/push_proto.h` in our repo), including a MAC-derived device token
for anti-cross-talk and a phone→device clock-sync opcode. It is **not wire-compatible with
any official app or protocol**, and the companion Android app is required. This is by design
(the stock protocol had no multi-device story), but it means the two stacks must not be mixed.

### Testing

- 125 hardware-free host tests (C/Python/JS) in `tools/`, run in CI on every commit;
- On-device regression across multiple generations (page turns, waveforms, dictionary lookup,
  push sessions, sleep/lock paths) on real RDP-G01-W units.

### Licensing & attribution

- Upstream code and third-party components retain their original licenses (Apache-2.0 etc.);
  a full notice list is maintained in our repo (`THIRD_PARTY_NOTICES.md`).
- Dictionary data is derived from [ECDICT](https://github.com/skywind3000/ECDICT) (MIT) and is
  **not bundled** in the firmware; users supply their own `dict.bin`.
- Pico-Plus is a community project, not affiliated with MindReset; hardware names belong to
  their respective rights holders.

Happy to answer any questions, and thank you again for the open hardware + open firmware.

---

*Internal note (do not paste into the PR): our branch has stopped syncing upstream; this
description is for the sharing/awareness submission only.*
