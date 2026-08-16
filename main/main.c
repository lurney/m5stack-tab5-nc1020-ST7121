/*
 * NC2000 / NC1020 (wangyu wqx) — standalone app for M5Stack Tab5 (ESP32-P4).
 *
 * Everything lives on the SD card. A scrolling file browser (the same component
 * the GB build uses) lets the user pick a data file; the emulator is chosen by
 * the file's suffix:
 *     <name>.nand  -> NC2000  (needs <name>.nand0 + <name>.nor too)
 *     <name>.rom   -> NC1020  (needs <name>.nor)
 * The matching .nor / saved-state files are written next to the chosen base.
 */
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include "app_common.h"
#include "file_browser.h"
#include "nc2000_run.h"
#include "odroid_sdcard.h"
#include "esp_log.h"

static const char *TAG = "nc2000_tab5";

/* Default-test shortcut: if 1 and /sd/nc1020.rom exists, boot straight into
 * NC1020 (skips the browser). Set to 0 for the normal file-browser flow. */
#define DEBUG_AUTOLAUNCH_NC1020 0

void app_main(void)
{
    ESP_LOGI(TAG, "=== NC2000/NC1020 Tab5 (standalone) Starting ===");

    app_init();

    if (odroid_sdcard_open("/sd") != ESP_OK) {
        ESP_LOGE(TAG, "SD card mount failed — insert a card with .rom/.nand files");
        app_return_to_launcher();
        return;
    }

#if DEBUG_AUTOLAUNCH_NC1020
    if (access("/sd/nc1020.rom", F_OK) == 0) {
        ESP_LOGW(TAG, "[TEST] auto-launching /sd/nc1020 (NC1020)");
        nc2000_run("/sd/nc1020", NC2K_MODE_NC1020);
        odroid_sdcard_close();
        app_return_to_launcher();
        return;
    }
    ESP_LOGW(TAG, "[TEST] /sd/nc1020.rom not found — falling back to browser");
#endif

    file_browser_init("/sd");

    char sel[512];   /* match file_browser MAX_PATH so deep paths aren't truncated */
    if (file_browser_run(sel, sizeof(sel)) != 0) {
        ESP_LOGW(TAG, "No file selected, returning to launcher");
        file_browser_deinit();
        odroid_sdcard_close();
        app_return_to_launcher();
        return;
    }
    file_browser_deinit();
    ESP_LOGI(TAG, "Selected: %s", sel);

    /* Derive machine + base path (no suffix) from the chosen file's extension. */
    nc2k_mode_t mode;
    size_t len = strlen(sel);
    if (len > 5 && strcasecmp(sel + len - 5, ".nand") == 0) {
        mode = NC2K_MODE_NC2000;
        sel[len - 5] = '\0';
    } else if (len > 4 && strcasecmp(sel + len - 4, ".rom") == 0) {
        mode = NC2K_MODE_NC1020;
        sel[len - 4] = '\0';
    } else {
        ESP_LOGE(TAG, "Unrecognized file (need .rom or .nand): %s", sel);
        odroid_sdcard_close();
        app_return_to_launcher();
        return;
    }

    ESP_LOGI(TAG, "Launching base=%s mode=%d", sel, (int)mode);
    nc2000_run(sel, mode);

    odroid_sdcard_close();
    app_return_to_launcher();
}
