/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
/* Includes */
#include "common.h"
#include "gap.h"
#include "gatt_svc.h"
#include "heart_rate.h"
#include "led.h"

/* Library function declarations */
void ble_store_config_init(void);

/* Private function declarations */
static void nimble_host_config_init(void);
static void nimble_host_task(void *param);

/* Private functions */
/*
 *  Stack event callback functions
 *      - on_stack_reset is called when host resets BLE stack due to errors
 *      - on_stack_sync is called when host has synced with controller
 */
static void on_stack_reset(int reason) {
    /* On reset, print reset reason to console */
    ESP_LOGI(TAG, "nimble stack reset, reset reason: %d", reason);
}

static void on_stack_sync(void) {
    /* On stack sync, do advertising initialization */
    adv_init();
}

static void nimble_host_config_init(void) {
    /* Set host callbacks */
    ble_hs_cfg.reset_cb = on_stack_reset;
    ble_hs_cfg.sync_cb = on_stack_sync;
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    /* Security manager configuration */


    /*
    Security Manager Local Input Output Capabilities
    BLE_HS_IO_DISPLAY_ONLY      表示のみ
    BLE_HS_IO_DISPLAY_YESNO     表示とYes/No入力    Numeric Comparison 向き
    BLE_HS_IO_KEYBOARD_ONLY     数字入力のみ        相手のパスキーを入力できる
    BLE_HS_IO_NO_INPUT_OUTPUT   入出力なし          センサー等の画面・ボタンなし機器
    BLE_HS_IO_KEYBOARD_DISPLAY  表示 + 数字入力     最も高機能なUIあり
    */
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;

    /*
    ボンディングする(1)か、しない(0)か
    LTK/IRK/CSRKなどの鍵情報を保存し次回接続時に再利用する
    */
    ble_hs_cfg.sm_bonding = 1;

    /*
    BLEペアリング時に MITM(Man-In-The-Middle) protection を要求する(1)か、しない(0)か
    */
    ble_hs_cfg.sm_mitm = 1;

    /*
    Key Distribution段階で、自分側が相手に配布する鍵の種類を指定する
    */
    ble_hs_cfg.sm_our_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    /*
    Key Distribution段階で、相手から受け取る鍵の種類を指定する
    */
    ble_hs_cfg.sm_their_key_dist |= BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    /*
    BLE_SM_PAIR_KEY_DIST_ENC    Encryption Key / LTK            次回接続時に暗号化を再開するため
    BLE_SM_PAIR_KEY_DIST_ID     Identity Resolving Key / IRK    ランダムアドレス使用時に同一デバイスだと識別するため
    BLE_SM_PAIR_KEY_DIST_SIGN   CSRK                            署名付きデータ用
    BLE_SM_PAIR_KEY_DIST_LINK   Link Key                        BR/EDR 関連
    */

    /* Store host configuration */
    ble_store_config_init();
}

static void nimble_host_task(void *param) {
    /* Task entry log */
    ESP_LOGI(TAG, "nimble host task has been started!");

    /* This function won't return until nimble_port_stop() is executed */
    nimble_port_run();

    /* Clean up at exit */
    vTaskDelete(NULL);
}

static void heart_rate_task(void *param) {
    /* Task entry log */
    ESP_LOGI(TAG, "heart rate task has been started!");

    /* Loop forever */
    while (1) {
        /* Update heart rate value every 1 second */
        update_heart_rate();
        ESP_LOGI(TAG, "heart rate updated to %d", get_heart_rate());

        /* Send heart rate indication if enabled */
        send_heart_rate_indication();

        /* Sleep */
        vTaskDelay(HEART_RATE_TASK_PERIOD);
    }

    /* Clean up at exit */
    vTaskDelete(NULL);
}

void app_main(void) {
    /* Local variables */
    int rc = 0;
    uint32_t seed = esp_random();
    esp_err_t ret;

    /* LED initialization */
    led_init();

    /* Random generator initialization */
    srand(seed);

    /*
     * NVS flash initialization
     * Dependency of BLE stack to store configurations
     */
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize nvs flash, error code: %d ", ret);
        return;
    }

    /* NimBLE stack initialization */
    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize nimble stack, error code: %d ",
                 ret);
        return;
    }

#if CONFIG_BT_NIMBLE_GAP_SERVICE
    /* GAP service initialization */
    rc = gap_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "failed to initialize GAP service, error code: %d", rc);
        return;
    }
#endif

    /* GATT server initialization */
    rc = gatt_svc_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "failed to initialize GATT server, error code: %d", rc);
        return;
    }

    /* NimBLE host configuration initialization */
    nimble_host_config_init();

    /* Start NimBLE host task thread and return */
    xTaskCreate(nimble_host_task, "NimBLE Host", 4*1024, NULL, 5, NULL);
    xTaskCreate(heart_rate_task, "Heart Rate", 4*1024, NULL, 5, NULL);
    return;
}
