/*==============================================================
 * main.c  —  WiFi / SNTP / FreeRTOS clock task (C)
 *
 *  Core 0 : WiFi / lwIP / SNTP (IDF 기본)
 *  Core 1 : clockTask — 1초 주기 렌더링
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
#include "esp_idf_version.h"
#include "nvs_flash.h"
#include "esp_sntp.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
  #include "esp_netif_sntp.h"
  #define USE_NETIF_SNTP 1
#else
  #define USE_NETIF_SNTP 0
#endif

#include "app_config.h"
#include "hub75_i2s.h"
#include "hub75_display.h"
#include "hub75_framebuffer.h"

static const char *TAG = "MAIN";

#define W_CONN_BIT  BIT0
#define W_FAIL_BIT  BIT1

static EventGroupHandle_t s_evt;
static int                s_retry;

/* ── WiFi / IP 이벤트 핸들러 ── */
static void wifiCb(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;

    if (base == WIFI_EVENT) {
        if (id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
            if (s_retry < WIFI_MAX_RETRY) {
                s_retry++;
                esp_wifi_connect();
                showMsg("Reconnecting...", color565(255, 165, 0));
            } else {
                xEventGroupSetBits(s_evt, W_FAIL_BIT);
            }
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&e->ip_info.ip));
        s_retry = 0;
        xEventGroupSetBits(s_evt, W_CONN_BIT);
    }
}

/* ── WiFi STA 초기화 + 연결 대기 ── */
static bool wifiInit(void)
{
    if (WIFI_SSID[0] == '\0') {
        ESP_LOGE(TAG, "WIFI_SSID is empty — app_config.h 를 채우세요");
        return false;
    }

    s_evt   = xEventGroupCreate();
    s_retry = 0;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifiCb, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifiCb, NULL, NULL));

    wifi_config_t wc;
    memset(&wc, 0, sizeof(wc));                 /* C 에는 = {} 가 없다 */
    strncpy((char *)wc.sta.ssid,     WIFI_SSID, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, WIFI_PASS, sizeof(wc.sta.password) - 1);
    wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t b = xEventGroupWaitBits(
        s_evt, W_CONN_BIT | W_FAIL_BIT,
        pdFALSE, pdFALSE, pdMS_TO_TICKS(WIFI_WAIT_MS));

    return (b & W_CONN_BIT) != 0;
}

/* ── SNTP 시작 + 동기화 대기 ── */
static void sntpSetup(void)
{
#if USE_NETIF_SNTP
    esp_sntp_config_t sc = ESP_NETIF_SNTP_DEFAULT_CONFIG(NTP_SRV);
    sc.sync_cb = NULL;
    ESP_ERROR_CHECK(esp_netif_sntp_init(&sc));
#else
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, NTP_SRV);
    esp_sntp_init();
#endif

    setenv("TZ", TZ_STRING, 1);
    tzset();

    int r = 0;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET &&
           ++r < SNTP_WAIT_STEPS) {
        vTaskDelay(pdMS_TO_TICKS(SNTP_WAIT_STEP_MS));
    }

    /* [노트 14] ESP_LOGI 인자에 삼항연산자를 직접 넣지 말 것 */
    const char *msg = (sntp_get_sync_status() != SNTP_SYNC_STATUS_RESET)
                      ? "NTP OK" : "NTP timeout";
    ESP_LOGI(TAG, "%s", msg);
}

/* ── clockTask  —  Core 1 고정, 1초 주기 갱신 ── */
static void clockTask(void *pv)
{
    (void)pv;

    struct tm  ti;
    TickType_t last = xTaskGetTickCount();

    memset(&ti, 0, sizeof(ti));

    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(1000));

        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
            showMsg("Reconnecting...", color565(255, 165, 0));
            s_retry = 0;
            esp_wifi_connect();
            continue;
        }

        time_t now = time(NULL) + 1;       /* 렌더링 지연 1초 보상 */
        localtime_r(&now, &ti);

        if (ti.tm_year < 100) {            /* 1970 → 아직 미동기화 */
            showMsg("NTP Failed!", color565(255, 0, 0));
            continue;
        }

        clearScreen();
        drawClock(&ti);
        flipDMABuffer();
    }
}

/* ── app_main  —  C 이므로 extern "C" 불필요 ── */
void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    if (!panelBegin()) {
        ESP_LOGE(TAG, "Panel init FAILED");
        return;
    }

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
