set(main_requires
    read_pico
    read_pico_transfer
    read_pico_search
    espressif__qrcode
    espressif__esp_jpeg
    # 渐进式 JPEG；基线仍走 esp_jpeg。/ Progressive JPEG; baseline stays on esp_jpeg.
    jpegdec
    espressif__libpng
    espressif__zlib
    espressif__esp_tinyusb
    # BLE HID 主机（蓝牙翻页器）。本仓库开了 MINIMAL_BUILD，组件不进依赖图的话连它的
    # Kconfig 都不会加载，sdkconfig.defaults 里的 CONFIG_BT_* 会被判为未知符号而丢弃。
    # BLE HID host (Bluetooth page-turner). This tree sets MINIMAL_BUILD, so a component
    # outside the dependency graph does not even load its Kconfig and the CONFIG_BT_*
    # entries in sdkconfig.defaults are dropped as unknown symbols.
    bt
    # 蓝牙翻页器本体。/ The page-turner itself.
    ble_page_turner
    # 手机 BLE 推流接收端。与 ble_page_turner 共用一套 NimBLE 栈生命周期（外设
    # 角色 + 射频互斥），同样必须进依赖图否则 Kconfig 不加载。
    # The phone BLE push sink. It shares the NimBLE stack lifecycle with
    # ble_page_turner (peripheral role + radio mutex) and must stay in the
    # dependency graph for the same Kconfig reason.
    push_sink
    continuous_du
    e0470_epaper_waveform
    e0470_page_turn
    read_pico_pmu
    fatfs
    wear_levelling
    bootloader_support
    spi_flash
    heap
    nvs_flash
    esp_hw_support
    esp_rom
    esp_driver_gpio
    esp_driver_usb_serial_jtag
    esp_app_format
    app_update
    esp_http_client
    mbedtls
    espressif__cjson
)
idf_component_register(
    SRCS
        "app_main.c"
        "boot_state.c"
        "apps/app_boot_recovery.c"
        "ota_update.c"
        "ota_online.c"
        "ota_release.c"
        "display.c"
        "display_main_waveform.c"
        "display_reader_waveform.c"
        "sleep.c"
        "time_service.c"
        "lock_pin.c"
        "lock_pin_flow.c"
        "lock_screen.c"
        "settings.c"
        "app/app_loop.c"
        "app/crash_note.c"
        "app/app_registry.c"
        "ui/ui_kit.c"
        "ui/ui_text_edit.c"
        "ui/ui_text_input.c"
        "ui/ui_ime.c"
        "ui/ui_keyboard.c"
        "ui/ui_gesture.c"
        "ui/ui_pinpad.c"
        "ui/ui_power_dialog.c"
        "ui/ui_quick_menu.c"
        "ui/ui_lock_feedback.c"
        "ui/ui_quick_menu_input.c"
        "ui/ui_click_feedback.c"
        "ui/ui_wifi_qr.c"
        "ui/datamatrix.c"
        "ui/ui_menu.c"
        "ui/ui_nav.c"
        "ui/ui_home_widget.c"
        "ui/ui_home_clock.c"
        "ui/ui_image_dither.c"
        "ui/ui_wallpaper.c"
        "ui/ui_avatar.c"
        "ui/ui_reader_background.c"
        "apps/app_book.c"
        "apps/dict_popup.c"
        "apps/book_toc.c"
        "apps/app_dashboard.c"
        "apps/app_files.c"
        "apps/file_tree.c"
        "apps/app_device_settings.c"
        "apps/app_image.c"
        "apps/app_transfer.c"
        "apps/usb_storage.c"
        "book/book_store.c"
        "book/book_progress.c"
        "book/book_history_backup.c"
        "book/book_title.c"
        "book/book_ticket.c"
        "book/book_excerpt.c"
        "book/book_lock_collage.c"
        "book/book_source.c"
        "book/book_index_cache.c"
        "book/book_txt.c"
        "book/book_epub.c"
        "book/book_cover.c"
        "book/book_cover_jobs.c"
        "book/book_image_header.c"
        "book/book_cover_auto.c"
        "book/zip_reader.c"
        "book/html_text.c"
        "book/book_layout.c"
        "book/gbk.c"
        "dict/dict_lookup.c"
        "dict/vocab_log.c"
        "font/ttf_font.c"
        "font/app_font_context.c"
        "font/ui_font.c"
        "font/ui_hanzi.c"
        "factory/pmu_selftest.c"
        "factory/vcom_setup.c"
    REQUIRES ${main_requires}
    INCLUDE_DIRS "." "app" "apps" "font" "ui" "factory" "book" "dict"
    EMBED_FILES
        "assets/lock_4bpp.bin"
        "assets/loading_4bpp.bin"
        "assets/builtin.ttf"
        "assets/ui-hanzi.bin"
        "assets/reading.md"
        "assets/display-test.png"
)

# 包住 IDF 的 panic 处理器：崩溃现场在重启前落进 RTC，没有串口时那是唯一的现场。
# 链接选项挂在 main 上，INTERFACE 会带到最终链接；写在 project() 之后的全局属性则太晚。
# Wrap the IDF panic handler so the crash scene reaches RTC before a restart; without a console
# that is the only place it survives. The option hangs off main, whose INTERFACE carries it into
# the final link; a global property set after project() is too late for the linker to see it.
target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=esp_panic_handler")

# 本地 USB 测时入口，普通构建不包含。/ Local USB timing probe, omitted from ordinary product builds.
if("$ENV{KIIKO_LOCAL_LATENCY}" STREQUAL "1")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE KIIKO_LOCAL_LATENCY=1)
endif()
