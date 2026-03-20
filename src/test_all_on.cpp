/*==============================================================
 * test_all_on.cpp  —  전체 화면 점등 테스트
 * [사용법] 이 파일을 src/main.cpp 로 이름 바꿔서 업로드
 * WHITE → RED → GREEN → BLUE 2초씩 순환
 *==============================================================*/

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "hub75_i2s.h"
#include "hub75_framebuffer.h"

static const char *TAG = "TEST";

static void testTask(void *pv) {
    static const struct { uint8_t r, g, b; const char *name; } C[] = {
        {255, 255, 255, "WHITE"},
        {255,   0,   0, "RED  "},
        {  0, 255,   0, "GREEN"},
        {  0,   0, 255, "BLUE "},
    };
    int ci = 0;
    while (1) {
        ESP_LOGI(TAG, ">>> %s", C[ci].name);
        fillScreen(C[ci].r, C[ci].g, C[ci].b);
        flipDMABuffer();
        vTaskDelay(pdMS_TO_TICKS(2000));
        ci = (ci + 1) % 4;
    }
}

extern "C" void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "=== HUB75 Test Start ===");
    if (!panelBegin()) { ESP_LOGE(TAG, "Panel init FAILED"); return; }

    xTaskCreatePinnedToCore(testTask, "test", 4096, NULL, 5, NULL, 1);
}
