#ifndef APP_CONFIG_H
#define APP_CONFIG_H
/*==============================================================
 * app_config.h  —  WiFi / NTP / 시간대 설정
 *
 *  ★ 아래 두 값을 본인 공유기 정보로 채운 뒤 빌드하세요.
 *    소스에 자격증명을 남기고 싶지 않다면 platformio.ini 에
 *      build_flags = -DWIFI_SSID='"myap"' -DWIFI_PASS='"mypw"'
 *    로 넘겨도 됩니다 (아래 #ifndef 가 그대로 존중합니다).
 *==============================================================*/

#ifndef WIFI_SSID
#define WIFI_SSID       ""      /* ← 채우세요 */
#endif

#ifndef WIFI_PASS
#define WIFI_PASS       ""      /* ← 채우세요 */
#endif

/* NTP 서버 */
#ifndef NTP_SRV
#define NTP_SRV         "pool.ntp.org"
#endif

/* POSIX TZ 문자열. KST(UTC+9). 끝의 :01 은 원본의 1초 선행 보정. */
#ifndef TZ_STRING
#define TZ_STRING       "KST-9:00:01"
#endif

/* WiFi 재시도 / 대기 */
#define WIFI_MAX_RETRY      20
#define WIFI_WAIT_MS        10000
#define SNTP_WAIT_STEPS     20
#define SNTP_WAIT_STEP_MS   500

#endif /* APP_CONFIG_H */
