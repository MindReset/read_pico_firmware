# 上游裁剪 fork：只保留可移植的 C/C++ 解码路径。
# Trimmed upstream fork: keep the portable C/C++ decode path only.
#
# jpeg.inl 的 ESP32-S3 SIMD 汇编受 `ARDUINO_ARCH_ESP32` 与 esp-dsp 双重保护
# （jpeg.inl 顶部），纯 ESP-IDF 下自动关闭，所以这里不注册 *.S，也就不需要
# esp-dsp 或 arduino-esp32 —— 上游的 CMakeLists 两个都 REQUIRES。
# The ESP32-S3 SIMD assembly in jpeg.inl is gated on ARDUINO_ARCH_ESP32 plus
# esp-dsp near the top of the file and stays off under plain ESP-IDF, so no *.S
# is registered and neither esp-dsp nor arduino-esp32 is required (upstream
# REQUIRES both).
#
# 改动清单见 FORK.md。/ See FORK.md for the change list.
# ESP-IDF 下只有 C++ 的 JPEGDEC 类可用（jpeg.inl 的 C 接口被 __LINUX__/_WIN64 等保护），
# 所以编译类本体与一个 extern "C" 包装给 book_cover.c 调用。
# Under ESP-IDF only the C++ JPEGDEC class exists (jpeg.inl's C API is behind
# __LINUX__/_WIN64 and friends), so build the class plus an extern "C" wrapper for book_cover.c.
idf_component_register(
    SRCS "src/JPEGDEC.cpp" "src/jpegdec_shim.cpp"
    INCLUDE_DIRS "src"
)

# 上游按 32 位 int 与有符号左移写，而本工程默认 -Werror；这里只对上游源码降级这两类告警，
# 不改动 vendored 代码本身。
# Upstream assumes 32-bit int and shifts signed values, and this project builds with -Werror;
# demote exactly those two warnings for upstream sources without touching the vendored code.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=pointer-to-int-cast
    -Wno-error=shift-negative-value
)
