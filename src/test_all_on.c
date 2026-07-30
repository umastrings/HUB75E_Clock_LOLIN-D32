/*==============================================================
 * test_all_on.c  —  전체 화면 점등 테스트 (C)
 *
 * [사용법] 이 파일을 main.c 대신 빌드하려면 src/CMakeLists.txt 의
 *          EXCLUDE 목록을 main.c 로 바꾸거나, 이 파일을 main.c 로
 *          이름 변경해서 업로드한다.
 *          WHITE → RED → GREEN → BLUE 2초씩 순환.
 *
 * 패널이 아예 안 켜질 때 WiFi 계층을 빼고 드라이버만 검증하는 용도.
 *==============================================================*/

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "hub75_i2s.h"
#include "hub75_framebuffer.h"

static const char *TAG = "TEST";

typedef struct { uint8_t r, g, b; const char *name; } TestColor;

static const TestColor kColors[4] = {
    {255, 255, 255, "WHITE"},
    {255,   0,   0, "RED  "},
    {  0, 255,   0, "GREEN"},
    {  0,   0, 255, "BLUE "},
};

static void testTask(void *pv)
{
    (void)pv;
    int ci = 0;
    for (;;) {
        ESP_LOGI(TAG, ">>> %s", kColors[ci].name);
        fillScreen(kColors[ci].r, kColors[ci].g, kColors[ci].b);
        flipDMABuffer();
        vTaskDelay(pdMS_TO_TICKS(2000));
        ci = (ci + 1) % 4;
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "=== HUB75 Test Start ===");
    if (!panelBegin()) {
        ESP_LOGE(TAG, "Panel init FAILED");
        return;
    }

    xTaskCreatePinnedToCore(testTask, "test", 4096, NULL, 5, NULL, 1);
}
