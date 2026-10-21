#!/bin/bash
# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
#
# 中文：ReadPico 推送 APK 的手动构建脚本（不需要 Gradle/Android Studio）。
# 依赖：JDK（>=8）+ Android SDK（platforms;android-34 + build-tools;34.0.0）。
# 产物：ReadPicoPush.apk（debug 签名，可直接侧载安装）。
#
# 流程：javac(Java8) -> PushProto 宿主机对表测试 -> d8 -> aapt2 package ->
#       aapt2 add dex -> zipalign -> apksigner sign -> badging/verify。
set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
SDK="${ANDROID_HOME:-/opt/android-sdk}"
BT="$SDK/build-tools/34.0.0"
AJ="$SDK/platforms/android-34/android.jar"
APK="$HERE/ReadPicoPush.apk"
KEYSTORE="$HERE/debug.keystore"

[ -x "$BT/apksigner" ] || { echo "缺少 $BT（build-tools 34.0.0）"; exit 1; }
[ -f "$AJ" ] || { echo "缺少 $AJ（platforms;android-34）"; exit 1; }

echo "== 1/7 编译（Java 8，对表 android.jar）"
rm -rf "$HERE/build"
mkdir -p "$HERE/build/obj" "$HERE/build/apk" "$HERE/build/test"
javac --release 8 -encoding UTF-8 -nowarn \
    -classpath "$AJ" -d "$HERE/build/obj" \
    "$HERE/src/dev/readpico/push/PushProto.java" \
    "$HERE/src/dev/readpico/push/MainActivity.java"

echo "== 2/7 PushProto 宿主机对表测试（对表 push_pico.py）"
python3 "$HERE/test/gen-vectors.py"
javac --release 8 -encoding UTF-8 -nowarn \
    -cp "$HERE/build/obj" -d "$HERE/build/test" "$HERE/test/PushProtoTest.java"
cp "$HERE/test/vectors.txt" "$HERE/build/test/"
java -cp "$HERE/build/test:$HERE/build/obj" PushProtoTest

echo "== 3/7 d8 -> classes.dex"
"$BT/d8" --release --min-api 26 --lib "$AJ" \
    --output "$HERE/build/apk" \
    $(find "$HERE/build/obj" -name '*.class')

echo "== 4/7 aapt 打包资源与清单（aapt 1.x：package + add 一步链路）"
"$BT/aapt" package -f \
    -M "$HERE/AndroidManifest.xml" \
    -S "$HERE/res" \
    -I "$AJ" \
    -F "$HERE/build/apk/base.apk"

echo "== 5/7 装入 dex 并对齐"
(cd "$HERE/build/apk" && "$BT/aapt" add base.apk classes.dex)
"$BT/zipalign" -p -f 4 "$HERE/build/apk/base.apk" "$HERE/build/apk/aligned.apk"

echo "== 6/7 签名（debug keystore，首次自动生成）"
if [ ! -f "$KEYSTORE" ]; then
    keytool -genkeypair -keystore "$KEYSTORE" -alias androiddebugkey \
        -keyalg RSA -keysize 2048 -validity 10000 \
        -storepass android -keypass android \
        -dname "CN=Android Debug,O=Android,C=US" >/dev/null 2>&1
fi
"$BT/apksigner" sign --ks "$KEYSTORE" --ks-key-alias androiddebugkey \
    --ks-pass pass:android --key-pass pass:android \
    --out "$APK" "$HERE/build/apk/aligned.apk"

echo "== 7/7 校验产物"
"$BT/apksigner" verify --print-certs "$APK" | head -2
"$BT/aapt" dump badging "$APK" | grep -E 'package|application-label|sdkVersion|targetSdk'
ls -la "$APK"
sha256sum "$APK"
echo "BUILD_OK: $APK"
