# Read Pico 公式デモファームウェア

**言語:** [English](./README.md) | [简体中文](./README.zh-CN.md) | [日本語](./README.ja-JP.md)

[貢献ガイド](CONTRIBUTING.md) · [サポート](SUPPORT.md) · [セキュリティ](SECURITY.md) · [行動規範](CODE_OF_CONDUCT.md)

[![License](https://img.shields.io/github/license/MindReset/read_pico_firmware?style=for-the-badge&logo=apache&logoColor=white)](LICENSE)
[![Build](https://img.shields.io/github/actions/workflow/status/MindReset/read_pico_firmware/build.yml?branch=main&style=for-the-badge&logo=githubactions&logoColor=white)](https://github.com/MindReset/read_pico_firmware/actions)
![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.1-E7352C?style=for-the-badge&logo=espressif&logoColor=white)
![Target](https://img.shields.io/badge/target-ESP32--S3-E7352C?style=for-the-badge&logo=espressif&logoColor=white)

Read Pico は、Read シリーズの開発ボードです。ESP32-S3 と 4.7 インチのモノクロ電子ペーパーディスプレイを搭載しています。
Shenzhen MindReset Technology Co., Ltd. が、電子ペーパー向けのオープンソースファームウェアを
開発する方に向けて提供しています。このリポジトリには、ボードに出荷時から搭載されるファームウェアが含まれています。

ディスプレイ、タッチ、加速度センサー、電源、キー、TF カード、フォント、スリープと復帰の
デモ画面と診断画面を個別に用意しています。ハードウェアの動作確認や、独自ファームウェアを
開発する際のリフレッシュ制御、電力管理、操作方法の参考として利用できます。

ボードサポート、PMU プロトコルのホスト実装、各チップのドライバーは独立したコンポーネントとして
再利用できます。本ファームウェアはハードウェアのデモであり、完成した電子書籍リーダーではありません。

AI エージェント向けのディレクトリ構成、`app_desc_t` の仕様、用語集、コメント規約は
[AGENTS.md](AGENTS.md) を参照してください。

## 公式ドキュメントとその他のデバイス

- [Read Pico 公式ドキュメント](https://dot.mindreset.tech/docs/read_0)
- [Dot Open Platform](https://github.com/MindReset/dot_open_platform)：Quote/0 のハードウェア資料や Rand/0 のローカル表示連携など、ほかの Dot デバイスやプロジェクトも試してみてください。ファームウェアのサンプル、ピン配置、ケースの設計ファイルを公開しています。

## ハードウェア

| 項目 | 仕様 |
| --- | --- |
| MCU | ESP32-S3、16 MB flash、8 MB Octal PSRAM。flash と PSRAM はいずれも 120 MHz で動作 |
| ディスプレイ | 4.7 インチのモノクロ電子ペーパー、1216 × 684、16 階調、LCD ペリフェラルで駆動する 16 bit パラレルインターフェース |
| 電子ペーパー電源 | SY7636A。PGOOD は IO エキスパンダー経由で読み取り |
| 電源管理 | CW32L010。独自の I2C プロトコルでバッテリー、充放電、インジケーター LED、RTC、アラーム、電源を制御 |
| タッチ | CST836U、2 点タッチ、割り込み、ディープスリープからの復帰 |
| 加速度センサー | SC7A20H、タップ、向き検出、自由落下、FIFO |
| IO エキスパンダー | FCA9555、電子ペーパー制御ピンとカード検出 |
| ストレージ | TF カード（1 bit SDMMC）。フォントはカードから読み込み |
| その他 | ブザー、3 つの静電容量式キー領域 |

## ビルドと書き込み

ESP-IDF v6.1 が必要です。`components/read_pico/read_pico_flash_hpm.c` は、
v6 でのみ提供される `esp_flash_chips/spi_flash_override.h` に依存しています。

```sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem* flash monitor
```

`sdkconfig.defaults` の 120 MHz flash / PSRAM タイミングは、ボードに搭載された flash の型番に依存します。
CI は `sdkconfig.ci` の標準タイミングを使い、コンパイルできることのみを確認します。
[.github/workflows/build.yml](.github/workflows/build.yml) を参照してください。

開発中や書き込み時に、スリープから復帰したデバイスが認識されない場合は、次の順に試してください。

1. USB Type-A のデータケーブルに交換する。
2. デバイスを再度スリープさせてから復帰させる。
3. ボードを再起動してやり直す。

パネルの共通電圧（VCOM）は工場で校正され、PMU に保存されます。ファームウェアは起動時に
一度だけ読み取り、ドライバーを設定します。ローカルには保存せず、ユーザーによる変更機能もありません。

## 画面

機能メニューには、[main/app/app_registry.c](main/app/app_registry.c) で定義された順序で画面が並びます。

| 画面 | 内容 |
| --- | --- |
| 概要 | 起動時の I2C スキャン、識別情報、バッテリーと充電状態、ビルド日時 |
| 電子ペーパー更新 | 全画面 GC16、部分 DU、16 階調と高速 8 階調のグラデーション。各更新の実測時間を表示 |
| 読書テスト | 内蔵テキスト、DU / GL16 / GC16 によるページ送り、ヘッダーでの文字サイズ調整 |
| タッチ | 連続 DU による 2 点追従、指を離した後の全画面更新、ディープスリープと自動復帰 |
| 加速度センサー | リアルタイムの 3 軸値と傾き、タップ回数、向き検出 |
| 加速度センサー診断 | サンプリング設定とセルフテスト |
| 電源とバッテリー | バッテリー電圧、残量、充電状態。電子ペーパーの電源レール、温度、異常、読み取り専用の SY7636A 設定 |
| PMU プロトコル | プロトコルの状態、イベント、設定、コマンド |
| 電源キー | `key_raw_events`、押下・解放時のレベル、DOWN/UP/SHORT/LONG イベント |
| スリープと復帰 | ライトスリープ（キー操作または持ち上げで復帰）、ディープスリープ、電源オフ |
| TF カードとブザー | カード容量とマウント状態、再マウント、フォーマット、ブザー |
| フォント | カード内の TTF の一覧と切り替え、組版プレビュー、ウェイトの順次切り替え |
| IO 拡張 | Port-0 のレベルと割り込み、下部バーからのタッチリセットパルス |
| デバイス機能テスト | デバイスの応答確認とコマンド ACK。電源断を伴う項目はバックエンドにのみ結果を記録 |

3 つのキー領域があり、KEY1 / KEY3 は複数ページの画面でページを切り替えます。
KEY2 は常に全画面を GC16 で再描画します。

## ディレクトリ構成

```text
main/
  app_main.c        起動処理を組み立て、app_loop に引き渡す
  app/              アプリのインターフェース（app.h）、登録テーブル、イベントループ
  apps/             デモごとに 1 ファイル。app_desc_t のみを公開
  ui/               ui_kit の描画プリミティブとレイアウト定数、ui_menu の 2 階層メニュー
  font/             stb_truetype のグリフキャッシュ
  factory/          デバイス機能テストと出荷時の VCOM 校正
components/
  read_pico/        ボード BSP：I2C、EPD 定義と走査タイミング、TF カード、ブザー、flash HPM
  read_pico_pmu/    CW32L010 プロトコルのホスト実装
  epdiy/            LCD ペリフェラル経路に限定した電子ペーパーレンダラー
  continuous_du/    複数回の走査にわたり位相を蓄積し、指の動きに追従する連続 DU
  cst836u/ sc7a20h/ fca9555/ sy7636a/    チップドライバー
  e0470_epaper_waveform/                 パネル波形テーブルとトリミング関数
  pwm_audio/        LEDC PWM オーディオ。ブザーのバックエンドのひとつ
assets/             main/assets/*.bin の元画像
tools/              フォントと画像の変換スクリプト
```

デモ画面を追加するには、`main/apps/` にファイルを作成し、必要な `app_desc_t` のコールバックを
実装して、`main/app/app_registry.c` のメニューテーブルに登録します。メインループの変更は不要です。

## ピン配置

| 機能 | GPIO |
| --- | --- |
| I2C SCL / SDA | 40 / 39（400 kHz） |
| EPD データ D0–D15 | 4–18, 45 |
| EPD XLE / XSTL / XCL / SPV / CKV | 3 / 46 / 21 / 47 / 48 |
| FCA9555 INT# | 41（ライトスリープからの復帰にも使用） |
| CST836U INT# | 43 |
| SC7A20H INT1 | 1 |
| TF カード CLK / CMD / D0 | 38 / 42 / 44 |
| ブザー | 2 |

電子ペーパーの電源イネーブル、XOE、MODE、VCOM_EN、タッチリセット、カード検出は
FCA9555 の Port-0 に接続されています。[main/apps/app_ioe.c](main/apps/app_ioe.c) のピン定義表を参照してください。

## 謝辞とライセンス

- ファームウェア本体：Apache-2.0。[LICENSE](LICENSE) を参照してください。
- [epdiy](https://github.com/vroland/epdiy)：電子ペーパーのタイミング制御と描画。
  本ボードの LCD 経路に合わせて機能を絞ったフォークです。LGPL-3.0-or-later。
  変更内容は [components/epdiy/LICENSE](components/epdiy/LICENSE) に記載しています。
- [stb_truetype](https://github.com/nothings/stb)：グリフのラスタライズ。パブリックドメイン。
- [pwm_audio](https://github.com/espressif/esp-iot-solution/tree/master/components/audio/pwm_audio)：
  Espressif の LEDC PWM オーディオ。機能を絞ったコピーを Apache-2.0 で使用しています。
  [components/pwm_audio/LICENSE](components/pwm_audio/LICENSE) を参照してください。
- パネル波形テーブルはボードに付属し、現状のまま Apache-2.0 で提供されます。
  [components/e0470_epaper_waveform/LICENSE](components/e0470_epaper_waveform/LICENSE) を参照してください。
- 内蔵フォント `main/assets/builtin.ttf` は、Warren2060 氏の可変フォント
  [ChillDuanSans](https://github.com/Warren2060/ChillDuanSans) を
  `tools/gen_builtin_font.py` でサブセット化したものです。フォント全体はリポジトリに含めていません。
  サブセットにも SIL OFL-1.1 が適用されます。

開発者の皆さまのご理解とご支援に感謝します。

Shenzhen MindReset Technology Co., Ltd.
