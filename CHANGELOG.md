## 2026-08-17 编译修复

### 修复内容
1. **`nc2000/CMakeLists.txt`** — 添加 `target_compile_definitions(COMPONENT_LIB PRIVATE HANDYPSP TAB5_PORT)`，替换 `idf_component.yml` 中的 `cmake_compile_options`（ESP-IDF v5.5 不处理 YML 中的 cmake_compile_options）
2. **`nc2000/tab5_stubs.cpp`** — 所有冲突 stub（sound/console/cmd/disassembler/dsp）用 `#ifndef HANDYPSP` 包裹，避免与 nc2000 真实源文件多重定义
3. **`components/espressif__esp_codec_dev/CMakeLists.txt`** — 精简为只编译 `device/es8388`，并修复 INCLUDE_DIRS
4. **`CMakeLists.txt`（根）** — 添加 `add_compile_options(-fcommon)` 处理多重定义（最终未使用，因为 tab5_stubs 修复解决了问题）

### 构建结果
- ✅ `idf.py build` 通过（Step [9/9] Generating binary image）
- ✅ `build/nc2000_tab5.bin` 0x1bc8e0 bytes（约 1.8MB）
- ✅ 分区剩余空间 57%
