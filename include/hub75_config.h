#ifndef HUB75_CONFIG_H
#define HUB75_CONFIG_H
/*==============================================================
 * hub75_config.h  —  HUB75 드라이버 전역 상수 / 타입 / CIE LUT
 *
 * [C 버전 메모]
 *  - 원본(C++)의 constexpr 템플릿 LUT은 C에 없으므로
 *    부팅 시 1회 계산하는 런타임 LUT(cie_init)으로 대체했다.
 *    값·공식은 동일하고, 하드코딩된 테이블은 여전히 없다.
 *==============================================================*/

#include <stdint.h>
#include <stddef.h>

/* ── 패널 해상도 ── */
#define MATRIX_W     128
#define MATRIX_H     64
#define ROWS         32          /* MATRIX_H / 2 */
#define COLS         128         /* MATRIX_W */

/* ── BAM 색상 깊이 ── */
#define DEPTH        4

/* ── I2S 클럭
 *   소스: 80MHz PLL_D2 (clka_en=0)
 *   최종: 80MHz / I2S_DIV_NUM / I2S_BCK_DIV = 10MHz
 *   TRM: "I2S_TX_BCK_DIV_NUM must not be 1" ── */
#define I2S_DIV_NUM  4
#define I2S_BCK_DIV  2

/* ── 밝기 / 블랭킹 ── */
#define LAT_BLANK    4
#define BRIGHTNESS   150
#define DMA_MAX      (4096 - 4)

/* ── 타입 별칭 ── */
typedef uint16_t u16;

/* ── DMA 워드 비트 필드
 *  [0] R1  [1] G1  [2] B1
 *  [3] R2  [4] G2  [5] B2
 *  [6] LAT [7] OE
 *  [8..12] A B C D E (행 주소) ── */
#define BIT_R1     (1u << 0)
#define BIT_G1     (1u << 1)
#define BIT_B1     (1u << 2)
#define BIT_R2     (1u << 3)
#define BIT_G2     (1u << 4)
#define BIT_B2     (1u << 5)
#define BIT_LAT    (1u << 6)
#define BIT_OE     (1u << 7)
#define ADDR_SH    8
#define BITS_RGB2  3

#define CLR_RGB1   ((u16)0xFFF8u)   /* 0b1111111111111000 */
#define CLR_RGB2   ((u16)0xFFC7u)   /* 0b1111111111000111 */
#define CLR_RGB12  ((u16)0xFFC0u)   /* 0b1111111111000000 */
#define CLR_OE     ((u16)0xFF7Fu)   /* 0b1111111101111111 */

/* ── BAM 비트 마스크 ── */
#define MASK_OFF       (16 - DEPTH)
#define PIX_MASK(d)    (1u << ((d) + MASK_OFF))

/* ── ESP32 WROOM TX FIFO 바이트 스왑 보정
 *   (원본 ESP32_TX_FIFO_POSITION_ADJUST)
 *   주의: 인자를 두 번 평가하므로 부수효과 있는 식 금지 ── */
#define FX(x)  (((x) & 1u) ? (x) - 1 : (x) + 1)

/* ── GPIO 핀 배치 ──
 *  C에서는 헤더의 static const 구조체가 TU마다 복제되고
 *  -Wunused 경고를 유발하므로 매크로로 정의한다.            ── */
#define PIN_R1   25
#define PIN_G1   26
#define PIN_B1   27
#define PIN_R2   14
#define PIN_G2   12
#define PIN_B2   13
#define PIN_LAT  18
#define PIN_OE   19
#define PIN_A     2
#define PIN_B     4
#define PIN_C     5
#define PIN_D    17
#define PIN_E    15
#define PIN_CLK  16

/* 데이터 핀 13개 (I2S DATA_OUT8..OUT20 순서와 1:1 대응) */
#define HUB75_DATA_PINS { PIN_R1, PIN_G1, PIN_B1, \
                          PIN_R2, PIN_G2, PIN_B2, \
                          PIN_LAT, PIN_OE,        \
                          PIN_A, PIN_B, PIN_C, PIN_D, PIN_E }
#define HUB75_DATA_PIN_CNT 13

/* ── CIE 1931 감마 보정 LUT ──
 *   공식: L = x*100/255
 *         Y = (L<=8) ? L/903.3 : ((L+16)/116)^3
 *         출력 = Y * 65535
 *   cie_init() 을 panelBegin() 안에서 1회 호출한다.        ── */
extern u16 CIE[256];
void cie_init(void);

/* ── color565 유틸리티 ── */
static inline u16 color565(uint8_t r, uint8_t g, uint8_t b)
{
    return (u16)(((u16)(r & 0xF8u) << 8) |
                 ((u16)(g & 0xFCu) << 3) |
                 (u16)(b >> 3));
}

#endif /* HUB75_CONFIG_H */
