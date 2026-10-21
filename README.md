# Pico-Plus · E-Ink Geek Secondary Display Firmware (Modified)

**English | [简体中文](README.zh-CN.md)**

> **Third-party modified firmware** for the MindReset RDP-G01-W e-paper reader: built on the
> official open-source firmware, it adds a **landscape UI, dual-column layout, offline
> dictionary and Vocabulary Book**, plus **BLE Text Push** (phone-to-device secondary display).
>
> This project is not affiliated with, endorsed by, or released by MindReset. The firmware is
> provided "as is"; flashing carries risk — please read the [Disclaimer](#8-disclaimer).

Based on the original work by [MindReset/read_pico_firmware](https://github.com/MindReset/read_pico_firmware).

**Current version: v1.0 (internal codename gen22, firmware version `0.3.3-pico22`)**

> **Language note:** the firmware UI is currently **primarily in Chinese**; English UI support
> is **planned for a future release**. This README is available in both English and Chinese,
> but on-device menus, prompts and the Vocabulary Book are Chinese-only for now.
>
> **Dictionary note:** this firmware **does not include dictionary data** — you must supply
> your own `dict.bin` on the TF card (see [Section 5](#5-after-flashing-put-books-on-the-device)).

---

## ⚠️ Read this first: branch independence

1. **Upstream syncing has stopped.** This firmware is a **deeply modified fork** of upstream
   `rc95`. It no longer tracks upstream changes, and its changes are not pushed back upstream.
2. **The partition table is independent.** It uses `partitions_16M.csv` (5.5 MB × 2 app
   partitions, `otadata` at `0xb10000`), which is **different** from the upstream/official
   layout. Therefore:
   - **The first flash must be done from a computer** (web flasher or command line) — the
     official tools and the device's built-in online upgrade cannot be used;
   - After the first flash, **later upgrades can go through the TF card**
     (see [Section 4](#4-tf-card-upgrade-no-computer-needed)).
3. **The low-level protocol is independent.** BLE Text Push (phone secondary display) uses this
   project's own BLE protocol and service UUIDs, which are **not compatible** with the official
   app/protocol.
4. **Android only for now.** **BLE Text Push and the phone secondary display support Android
   only; iOS is not supported due to system limitations.** Download the companion APK from the
   [`apk/`](apk/) directory — **the firmware and the APK must be updated together**.

> If you only want the "official stock experience", please go back to
> [MindReset/read_pico_firmware](https://github.com/MindReset/read_pico_firmware).

---

## 1. What this firmware does

On top of the original features (bookshelf, EPUB/TXT reading, reading progress, font import,
lock screen, USB storage, Wi-Fi book transfer), this modified version mainly adds:

| Category | Feature | Notes |
|---|---|---|
| 📖 Core reading | Landscape display | UI supports landscape orientation, closer to a regular book in hand |
| 📖 Core reading | Dual-column layout | Two columns per page, halving page turns; toggleable in reading settings |
| 📖 Core reading | Faster page turns | Text-only turns: no black flash, no white clear, partial refresh |
| 📖 Core reading | Water-ripple page turn | Page-turn water-ripple animation (optional) |
| 📚 Dictionary & vocabulary | Offline dictionary | Look up words while reading, no internet needed (**you must supply your own dictionary file on the TF card**) |
| 📚 Dictionary & vocabulary | Vocabulary book | Looked-up words are recorded automatically for later review |
| 📱 Geek secondary display | **BLE Text Push** | Android phones push long text / web snippets / word cards to the device, which opens them for reading automatically (**Android only**) |
| 📱 Geek secondary display | **Anti-cross-talk (MAC binding)** | The broadcast name derives from the factory MAC (e.g. `PicoPush-1A2B`), fixed and unique; with multiple devices present, pick from the APK list — no more pushing to the wrong device |
| 📱 Geek secondary display | **Unread reminder** | When a push arrives unread, an inverted dot + "new content" appears on the home status bar; opening it clears the reminder |
| 📱 Geek secondary display | **Bluetooth clock sync** | The phone calibrates the device clock while pushing (the device has no RTC crystal, so it drifts over time) |
| ⚙️ Low-level polish | Refresh / power tuning | Trimming unnecessary partial refreshes during the arming window, reworked sleep & lock paths, etc. |

> ### ⚠️ Important: bring your own dictionary file
>
> **This firmware does not include dictionary data.** The lookup feature depends on a
> dictionary file on the TF card: download `dict.bin` yourself and place it in the `dict`
> folder at the TF card root (recorded in the device as `/sdcard/dict/dict.bin`, where
> `/sdcard` is the mount-point name of the TF card inside the firmware — do **not** create an
> `sdcard` folder on the card itself), **otherwise word lookup will not work**.
> Download links and detailed steps: [Section 5](#5-after-flashing-put-books-on-the-device).

All original capabilities (book transfer, Wi-Fi, USB storage, TF card read/write, etc.) are
preserved (see [Section 5](#5-after-flashing-put-books-on-the-device)).

## 2. How to flash (three ways)

> **Before flashing**: make sure the device is sufficiently charged (50%+ recommended); use a
> **USB data cable** (some charge-only cables cannot transfer data).
>
> **The first flash must be done from a computer** (web flasher or command line), because this
> firmware's partition layout differs from the official one and requires a full write; after
> the first flash, later upgrades can go through the TF card.

### Method 1: Web flasher (recommended, simplest)

No software installation needed — flash from a desktop browser.

1. Connect the device to your computer with a USB data cable;
2. Open the web flasher page on your computer (**Chrome or Edge** required; mobile browsers
   and Safari are not supported):

   ```
   https://<your-username>.github.io/<repo-name>/
   ```

   (Once this repo has GitHub Pages enabled, the address above is the flasher page — the
   `web-flasher/` directory is published as the site root. If you haven't put the repo on
   GitHub, you can also enter the `web-flasher` directory locally and run
   `python -m http.server 8000`, then open `http://localhost:8000` to flash.)
3. Click the **"Install firmware"** button on the page;
4. Select the device's serial port in the dialog (e.g. `COM3`, `USB JTAG/serial debug unit`)
   and click connect;
5. Wait for the progress bar to finish (about 1–2 minutes); the device reboots automatically
   and flashing is complete.

> For devices previously flashed with official firmware, the tool will prompt **erase all
> data** — this is normal (the official firmware and this firmware have different partition
> layouts and a full erase is required before writing); just confirm.

**Troubleshooting**

- No response on click / serial port not found → try a different data cable, a different USB
  port, and close other software that may occupy the serial port;
- Page keeps spinning or the button doesn't appear → the flashing script failed to load; retry
  on a different network;
- Browser unsupported → desktop Chrome / Edge required;
- Flash failed halfway → re-plug the USB and retry.

### Method 2: One-click flash scripts (`flash.bat` on Windows, `flash.sh` on Linux/macOS)

1. Download the firmware package from this repo's Release assets, or take the 4 `.bin` files
   and flash scripts from the [`firmware/`](firmware/) directory (**the dictionary file is not
   included** — download it separately, see [Section 5](#5-after-flashing-put-books-on-the-device));
2. Install Python (download from [python.org](https://www.python.org/downloads/) and
   **make sure to check "Add Python to PATH"** during installation);
3. Press `Win + R`, type `cmd`, and run:

   ```
   pip install esptool
   ```

4. Connect the device via USB and **double-click `flash.bat`** (Windows) or run
   `sh flash.sh /dev/ttyUSB0`;
5. Wait for the prompts; done when you see `Flash done`.

The script first verifies that the SHA-256 of `Read_Pico.bin` matches the pinned value, and
**refuses to flash** if they differ.

### Method 3: Command-line flashing (Linux / macOS / Windows)

For users comfortable with the command line:

```sh
pip install esptool   # install esptool first

python -m esptool --chip esp32s3 -b 460800 \
  --before default-reset --after hard-reset write-flash \
  --flash-mode dio --flash-size 16MB --flash-freq 80m \
  0x0      bootloader.bin \
  0x8000   partition-table.bin \
  0x10000  Read_Pico.bin \
  0xb10000 ota_data_initial.bin
```

The four offsets correspond to this project's 16 MB partition layout (the app partition
`ota_0` is at `0x10000`, so `otadata` lands at `0xb10000`; **do not write the
upstream/other-project `0xD10000`**). If flashing errors out midway, retry with
`-b 460800` changed to `-b 230400`.

### Companion APK (for BLE Text Push)

After flashing, to use **BLE Text Push (phone secondary display)**, download the Android APK
from [`apk/`](apk/) and install it:

- **Android only**; **iOS is not supported due to system limitations**.
- **The firmware and the APK must be updated together**: an older APK in a multi-device
  environment may connect to the wrong device (it has no device-selection capability).
- **Android 10/11 phones note**: those systems require the location-services master switch to
  be on plus the location permission granted, otherwise BLE scanning returns nothing at all
  (it just keeps saying "no device found"). Since APK v1.1 a red/green banner at the top of
  the app detects this automatically and tells you exactly what is missing.

## 3. How to use BLE Text Push (secondary display)

1. On the device, enter the "Phone Push" entry to enable receiving (an arming window opens and
   the screen shows the broadcast name, e.g. `PicoPush-1A2B`);
2. Open the companion APK on your Android phone; after scanning finds the device:
   - **With only one device, it connects automatically**;
   - **With multiple devices, a list pops up** — pick the one matching the code shown on the
     e-paper screen (anti-cross-talk);
3. Type/paste the text to push in the APK (word cards are also supported) and send;
4. After the device finishes receiving, it opens the content as a "virtual book" for reading;
   if you don't open it, an **inverted dot + "new content"** reminder appears on the home
   status bar, cleared once opened.

> The broadcast name derives from the factory MAC, is fixed, and can be used to identify the
> device directly.

## 4. TF card upgrade (no computer needed)

1. **Rename** the `Read_Pico.bin` from the firmware package to `Pico-update.bin`;
2. Copy it to the **root** of the TF card (FAT32) with a card reader;
3. Insert the TF card back into the device and enter **Settings → Upgrade & Recovery**;
4. Tap "Install TF card upgrade package" and wait for automatic completion and reboot.

> If the device already runs the **same version**, it will show "upgrade package matches the
> current version" — to reflash in that case, use the web flasher or command-line flashing
> above.
>
> **Important: do not use the device's "Online Upgrade".** Online upgrade checks the official
> firmware address and would flash back the official version, losing this fork's landscape,
> dual-column and other features. Use only the three methods on this page for upgrades.

## 5. After flashing: put books on the device

Pick any of the three ways:

- **Wi-Fi book transfer (most convenient)**: open the "Transfer" app on the device → connect
  your phone/computer to the device hotspot as prompted on screen → open the address shown on
  screen in a browser to upload EPUB / TXT;
- **USB**: enable USB storage mode on the device, then drag books in like a USB drive;
- **TF card**: copy books to the TF card and open them in the device file manager;
- **BLE Text Push**: push directly from the phone (see [Section 3](#3-how-to-use-ble-text-push-secondary-display),
  **Android only**).

### Offline dictionary setup (required for word lookup)

> **This firmware does not include dictionary data.** The lookup feature depends on a
> dictionary file on the TF card; you must set it up as follows, otherwise word lookup will
> not work.

1. Download a dictionary file (either one, the first is recommended):
   - [`dict.bin`](https://github.com/cere-hy/pico-plus/releases/download/v1.0-gen10/dict.bin)
     — recommended, about 8.9 MB, 124k entries, SHA-256 `2bb228e4…9ee7`;
   - [`dict-full.bin`](https://github.com/cere-hy/pico-plus/releases/download/v1.0-gen10/dict-full.bin)
     — larger dictionary, about 23 MB, 400k entries, SHA-256 `7c17cca7…f61b`;
2. If you downloaded `dict-full.bin`, **rename it to `dict.bin`** first;
3. Copy `dict.bin` into the **`dict` folder at the TF card root**: insert the TF card into
   your computer (or a card reader), open the card drive, create a folder named `dict` at the
   top level and put `dict.bin` in it — the card should then contain `dict/dict.bin` at that
   level; **do not create an `sdcard` folder on the card** (create the `dict` folder if it
   doesn't exist; the device also creates it automatically on boot with a card inserted);
4. Insert the TF card back into the device and reboot; afterwards, select a word while reading
   to look it up, and looked-up words go into the Vocabulary Book automatically.

> **The name and location must match exactly**: the file must be named `dict.bin` and placed
> in the `dict` folder at the TF card **root**. The internal device path `/sdcard/dict/dict.bin`
> mentioned in the docs: `/sdcard` is just the **mount-point name** of the TF card inside the
> firmware (when the card is in your computer, you see the card drive root itself) — do not
> create another `sdcard` folder on the card; other names or locations will not work. Put only
> one dictionary file in the `dict` folder — with multiple files, which one opens is
> undefined.
>
> The dictionary data is converted from [ECDICT](https://github.com/skywind3000/ECDICT) (MIT);
> you can also build the dictionary yourself with `tools/build_dict.py` in this repo.

## 6. Developer notes

- Development environment: **ESP-IDF v6.1**;
- Build:

  ```sh
  idf.py set-target esp32s3
  idf.py build
  ```

- Partition table: `partitions_16M.csv` (5.5 MB × 2 app partitions, `otadata` at `0xb10000`);
- Host tests: `tools/` contains **125 tests that run without hardware** (C / Python / JS) and
  12 one-click runner scripts; CI runs the main regressions on every commit;
- Directory tour:
  - `main/` firmware core — `book/` text parsing & layout engine, `dict/` the offline
    dictionary & Vocabulary Book, `apps/` the various apps;
  - `components/` hardware & feature components — `read_pico_transfer/` for Wi-Fi book
    transfer, `push_sink/` for BLE push receiving (including the `push_proto` protocol);
  - `firmware/` final firmware four-pack + flash scripts; `apk/` the Android companion
    package;
  - `web-flasher/` web flashing page (manifest + firmware bins, updated in sync with the
    firmware);
  - `tools/build_dict.py` dictionary build tool (ECDICT CSV → device RPD1 `dict.bin`);
  - `tools/android/` Android APK sources & build script (`build-apk.sh`), plus the
    `push_pico.py` command-line push tool;
- Internal mechanism conventions (virtual books, transfer policy, waveforms, etc.) are
  documented in `docs/ONBOARDING.md`, `docs/HANDOFF.md` and `AGENTS.md`.

## 7. Changelog

Organized in four categories; **currently v1.0 (firmware version `0.3.3-pico22`)**:
see [RELEASE_NOTES.md](RELEASE_NOTES.md).

## 8. Disclaimer

1. This firmware is a third-party modified version, **provided "as is" without warranty of
   any kind, express or implied**; this project assumes no liability for device damage, data
   loss or other losses caused by flashing or using this firmware;
2. Flashing carries inherent risk (including a very low probability of bricking, potentially
   requiring teardown recovery); make sure you understand and accept this risk before
   proceeding;
3. This project is not affiliated with MindReset; hardware names such as "RDP-G01-W" belong
   to their respective rights holders;
4. This branch **has stopped syncing upstream** — please use the flashing methods provided on
   this page; do not use the official tools or the device's built-in online upgrade;
5. The new original content in the firmware is licensed under the
   [Personal Study License](licenses/kiikoread-Personal-Study-1.0.txt), which permits personal
   study use; commercial use requires separate written authorization. Upstream code licenses
   (Apache-2.0 etc.) are unaffected; for the full scope see [LICENSE](LICENSE) and
   [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## 9. Acknowledgements

- Built on the [MindReset official open-source example firmware](https://github.com/MindReset/read_pico_firmware)
  (based on the original work by [MindReset/read_pico_firmware](https://github.com/MindReset/read_pico_firmware));
- Dictionary data from [ECDICT](https://github.com/skywind3000/ECDICT) (MIT);
- Web flashing based on [esp-web-tools](https://github.com/espressif/esp-web-tools) (Apache-2.0);
- Thanks to the upstream project and all third-party component authors; the full license
  list is in `licenses/` and `THIRD_PARTY_NOTICES.md`.
