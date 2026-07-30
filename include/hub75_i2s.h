#ifndef HUB75_I2S_H
#define HUB75_I2S_H
/*==============================================================
 * hub75_i2s.h  —  ESP32 I2S1 병렬 DMA 드라이버 선언
 *==============================================================*/

#include <stdbool.h>
#include "hub75_config.h"
#include "hub75_framebuffer.h"

/* GPIO 라우팅 + I2S 레지스터 설정 */
void routeGPIO(void);
void configI2S(void);

/* DMA 디스크립터 할당 + 전체 패널 초기화 */
bool panelBegin(void);

#endif /* HUB75_I2S_H */
