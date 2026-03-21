/*==============================================================
 * main.cpp  —  WiFi / SNTP / FreeRTOS clock_task
 *==============================================================*/

#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_idf_version.h"
#include "esp_sntp.h"
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
  #include "esp_netif_sntp.h"
  #define USE_NETIF_SNTP 1
#else
  #define USE_NETIF_SNTP 0
#endif

#include "hub75_i2s.h"
#include "hub75_display.h"

static const char *TAG = "MAIN";

/* ── WiFi 설정 ── */
#define WIFI_SSID       "2.4GHz WiFi SSID"
#define WIFI_PASS       "WiFi password"
#define NTP_SRV         "pool.ntp.org"
#define WIFI_MAX_RETRY  20
#define W_CONN_BIT      BIT0
#define W_FAIL_BIT      BIT1

static EventGroupHandle_t sEvt;
static int sRetry = 0;

/* ── WiFi 이벤트 핸들러 ── */
static void wifiCb(void *a, esp_event_base_t base, int32_t id, void *d) {
    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            if (sRetry < WIFI_MAX_RETRY) {
                esp_wifi_connect();
                sRetry++;
                showMsg("Reconnecting...", color565(255, 165, 0));
            } else {
                xEventGroupSetBits(sEvt, W_FAIL_BIT);
            }
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)d;
        ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&e->ip_info.ip));
        sRetry = 0;
        xEventGroupSetBits(sEvt, W_CONN_BIT);
    }
}

/* ── WiFi 초기화 ── */
static bool wifiInit(void) {
    sEvt = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifiCb, NULL, NULL);
    esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifiCb, NULL, NULL);

    wifi_config_t wc = {};
    strncpy((char *)wc.sta.ssid,     WIFI_SSID, sizeof(wc.sta.ssid));
    strncpy((char *)wc.sta.password, WIFI_PASS, sizeof(wc.sta.password));
    wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t b = xEventGroupWaitBits(
        sEvt, W_CONN_BIT | W_FAIL_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(10000));
    return (b & W_CONN_BIT) != 0;
}

/* ── SNTP 설정 ── */
static void sntpSetup(void) {
#if USE_NETIF_SNTP
    esp_sntp_config_t sc = ESP_NETIF_SNTP_DEFAULT_CONFIG(NTP_SRV);
    sc.sync_cb = NULL;
    esp_netif_sntp_init(&sc);
#else
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, NTP_SRV);
    esp_sntp_init();
#endif
    setenv("TZ", "KST-9:00:01", 1);  /* 1초 빠르게 보정 */
    tzset();

    int r = 0;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++r < 20)
        vTaskDelay(pdMS_TO_TICKS(500));

    const char *msg = (sntp_get_sync_status() != SNTP_SYNC_STATUS_RESET)
                      ? "NTP OK" : "NTP timeout";
    ESP_LOGI(TAG, "%s", msg);
}

/* ── clock_task  —  Core 1 고정, 1초 주기 갱신 ── */
static void clockTask(void *pv) {
    struct tm   ti   = {};
    TickType_t  last = xTaskGetTickCount();

    while (1) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(1000));

        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
            showMsg("Reconnecting...", color565(255, 165, 0));
            sRetry = 0;
            esp_wifi_connect();
            continue;
        }

        time_t now = time(NULL) + 1;  /* 렌더링 지연 1초 보상 */
        localtime_r(&now, &ti);
        if (ti.tm_year < 100) {
            showMsg("NTP Failed!", color565(255, 0, 0));
            continue;
        }

        clearScreen();
        drawClock(&ti);
        flipDMABuffer();
    }
}

/* ── app_main ── */
extern "C" void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    if (!panelBegin()) { ESP_LOGE(TAG, "Panel init FAILED"); return; }

    showMsg("Connecting Wi-Fi...", color565(0, 255, 0));

    if (wifiInit()) {
        showMsg("Wi-Fi Connected!", color565(0, 255, 255));
        vTaskDelay(pdMS_TO_TICKS(1000));
        sntpSetup();
    } else {
        showMsg("Wi-Fi Failed!", color565(255, 0, 0));
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    xTaskCreatePinnedToCore(clockTask, "clock", 4096, NULL, 5, NULL, 1);
}
