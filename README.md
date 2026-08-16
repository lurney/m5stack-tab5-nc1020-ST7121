# m5stack-tab5-nc1020-ST7121

NC1020/NC2000 Retro Computer Emulator port for **M5Stack Tab5** (ESP32-P4 RISC-V), with **ST7121 LCD driver** support.

## 项目来源

- **上游项目**: NC1020/NC2000 模拟器（桌面版 C++，基于 SDL2）
- **LCD 驱动**: `espressif/esp-iot-solution` — `esp_lcd_st7121` v1.0.1
- **触摸驱动**: `espressif/esp_lcd_touch_st7123`（ST7121 与 ST7123 共用同一颗触摸 IC，I2C 0x55）
- **ESP-IDF**: v5.5.2（RISC-V toolchain，ESP32-P4 目标）

### 主要改动

| 模块 | 改动 |
|------|------|
| LCD 驱动 | ST7123 → ST7121（独立 DPI 时序 + ESP-IDF 内置默认初始化） |
| DPI 时序 | `vsync_back_porch=24, vsync_pulse_width=20, vsync_front_porch=200` @ 80 MHz |
| DSI lane rate | 730 Mbps |
| 触摸驱动 | 保持 `esp_lcd_touch_st7123`，不变 |
| 构建系统 | 从零搭建 CMakeLists.txt + sdkconfig + 所有组件清单 |
| SDL2 桩 | 完整 stub（audio / events / window），nc2000 桌面版移植到 ESP32 |

---

## 硬件环境

| 项目 | 详情 |
|------|------|
| 开发板 | M5Stack Tab5 |
| SoC | ESP32-P4（RISC-V 双核，最高 400 MHz） |
| LCD | ST7121，720×1280，60 Hz，DSI 4-lane |
| 触摸 | ST7123 touch IC（I2C 0x55） |
| Flash | 16 MB QSPI |
| PSRAM | 8 MB (Octal PSRAM) |

---

## 踩坑记录

### 1. PSRAM DMA 能力缺失 → 黑屏

**现象**：烧录后黑屏，串口报 `ESP_ERR_NO_MEM` → 看门狗复位。

**根因**：ESP-IDF `esp_psram.c` 注册 SPIRAM heap 时漏标 `MALLOC_CAP_DMA` 标志。

- DPI 面板 framebuffer（720×1280×2 = 1.8 MB）需要 `SPIRAM | 8BIT | DMA`
- 但 SPIRAM 区域只有 `SPIRAM | 8BIT | 32BIT | SIMD`，无 `DMA`
- `heap_caps_calloc` 永远失败 → `ESP_ERROR_CHECK` 断言 → WDT 复位 → 黑屏

**修复**：直接修改 `esp_psram.c` 第 450 行：
```c
// Before
byte_aligned_caps[] = {SPIRAM | DEFAULT, 0, 8BIT | 32BIT | SIMD};
// After
byte_aligned_caps[] = {SPIRAM | DEFAULT, 0, 8BIT | 32BIT | SIMD | DMA};
```

### 2. sdkconfig 配置错误（三重错误）

**错误①**：`CONFIG_SPIRAM_USE_MALLOC=y` → 应改为 `CONFIG_SPIRAM_USE_CAPS_ALLOC=y`

**错误②**：`CONFIG_CACHE_L2_CACHE_LINE_64B=y` → 应改为 `CONFIG_CACHE_L2_CACHE_LINE_128B=y`

**错误③**：`CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="../../partitions_ota.csv"` → 应改为 `"partitions_ota.csv"`

### 3. sdkconfig.defaults 优先级问题

ESP-IDF 中 `sdkconfig` 优先级高于 `sdkconfig.defaults`，Kconfig 显式设置的选项不会被 defaults 覆盖。解决方案：直接修正 `sdkconfig`，同时在项目根目录新建 `sdkconfig.defaults`。

### 4. ESP-IDF v5.5 组件名大规模变更

ESP-IDF v5.x 对组件名做了大幅精简：

| v4.x 组件名 | v5.x 组件名 |
|-------------|------------|
| `esp_log` | `esp_system` |
| `esp_ldo_regulator` | `esp_hw_support` |
| `esp_vfs_fat` | `vfs` |
| `usb_host` | `usb` |
| `esp_heap_caps` | `heap` |

### 5. nc2000 SDL2 依赖

nc2000 是桌面 C++ 模拟器，依赖 SDL2（音频、窗口、键盘事件、OpenGL 渲染）。ESP32 上没有 SDL2，需要在 `components/nc2000/stub/SDL2/` 下提供完整桩头（`SDL.h`、`SDL_audio.h`、`SDL_events.h` 等），实际功能由 Tab5 胶水代码接管。

### 6. nc2000 多重定义链接错误

`tab5_stubs.cpp` 与 nc2000 真实实现存在多重定义：
- `ram_io`（NekoDriverIO.cpp vs ram.cpp）
- `dsp`（tab5_stubs.cpp vs sound.cpp）
- `console_on`（tab5_stubs.cpp vs console.cpp）
- `disassemble2` / `disassemble_next`（tab5_stubs.cpp vs disassembler_new.cpp）

**修复**：从 CMakeLists.txt 删除 `tab5_stubs.cpp` 编译行，让 nc2000 用自己的真实实现。

### 7. `ram_io` static 重复定义（NekoDriverIO.cpp）

`NekoDriverIO.cpp` 中 `static uint8_t * ram_io = nc2k_states.ram_io;` 与 `ram.cpp` 中的全局 `uint8_t* ram_io = nc2k_states.ram_io;` 冲突。

**修复**：`static` 改为 `extern`。

### 8. SDL_audio.h / SDL_events.h 字段缺失

桩头头文件严重不完整，缺少大量符号导致编译失败：

- `SDL_QUIT`、`SDL_KEYDOWN`、`SDL_KEYUP`、`SDL_TEXTINPUT`（事件常量）
- `SDL_AudioSpec` 的 `freq`/`format`/`channels`/`samples`/`callback`/`userdata` 字段
- `SDL_LockAudioDevice` / `SDL_UnlockAudioDevice`

**修复**：逐一补全所有缺失的常量、类型和 inline 实现。

### 9. /dev/ttyACM0 权限问题

烧录时 `idf.py` 报 `/dev/ttyACM0 is not readable`，尽管设备存在且用户在 `dialout` 组。

**根因**：当前登录会话未生效 `dialout` 组（`id` 命令显示的组列表无 `dialout`）。

**修复**：用 `sg dialout` 临时切换组：
```bash
. ~/esp/setup_env.sh && sg dialout -c "idf.py -p /dev/ttyACM0 -b 460800 flash"
```

### 10. 分区表空间不足 + Flash 大小未配置

**错误①**：App 分区仅 1 MB，固件 1.8 MB 放不下 → 自定义 `partitions_ota.csv`，App 分区扩至 4 MB。

**错误②**：未配置 Flash 大小 → 添加 `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`。

### 11. git submodule TLS 断连

ESP32 WiFi 库 submodule 克隆时 GnuTLS 断连（recv error -110）。

**修复**：浅克隆 `git submodule update --init --depth 1 components/esp_wifi/lib`。

### 12. GCC 14 编译标志冲突

`-mno-unaligned-access` 与 `-fno-jump-tables` 冲突（ESP-IDF 自动生成的选项）。

**修复**：在 nc2000 组件 CMakeLists.txt 中补全编译标志。

---

## 编译结果

```
固件大小：0x15F8C1 bytes (~1.8 MB)
空闲分区：67%
状态：✅ Build 成功
```

---

## 烧录命令

```bash
cd ~/桌面/m5stack-tab5-nc1020
. ~/esp/setup_env.sh && sg dialout -c "idf.py -p /dev/ttyACM0 -b 460800 flash"
```

> **注意**：需要用 `sg dialout` 绕设备权限检查，或注销重新登录使 `dialout` 组生效。

---

## 关键路径

| 项目 | 路径 |
|------|------|
| 项目根目录 | `~/桌面/m5stack-tab5-nc1020/` |
| ESP-IDF | `~/esp/esp-idf` (v5.5.2) |
| 环境脚本 | `~/esp/setup_env.sh` |
| PSRAM 补丁 | `~/esp/esp-idf/components/esp_psram/system_layer/esp_psram.c` |
| sdkconfig | `~/桌面/m5stack-tab5-nc1020/sdkconfig` |
| sdkconfig.defaults | `~/桌面/m5stack-tab5-nc1020/sdkconfig.defaults` |

---

## License

基于 NC1020/NC2000 模拟器项目，遵循原项目许可证。
