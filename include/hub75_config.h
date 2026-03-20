#pragma once
/*==============================================================
 * hub75_config.h  —  HUB75 드라이버 전역 상수 / 타입 / CIE LUT
 *==============================================================*/

#include <stdint.h>
#include <array>

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
#define DMA_MAX      (4096-4)

/* ── 타입 별칭 ── */
typedef uint16_t u16;

/* ── DMA 워드 비트 필드
 *  [0] R1  [1] G1  [2] B1
 *  [3] R2  [4] G2  [5] B2
 *  [6] LAT [7] OE
 *  [8..12] A B C D E (행 주소) ── */
#define BIT_R1     (1u<<0)
#define BIT_G1     (1u<<1)
#define BIT_B1     (1u<<2)
#define BIT_R2     (1u<<3)
#define BIT_G2     (1u<<4)
#define BIT_B2     (1u<<5)
#define BIT_LAT    (1u<<6)
#define BIT_OE     (1u<<7)
#define ADDR_SH    8
#define BITS_RGB2  3

#define CLR_RGB1   ((u16)0b1111111111111000)
#define CLR_RGB2   ((u16)0b1111111111000111)
#define CLR_RGB12  ((u16)0b1111111111000000)
#define CLR_OE     ((u16)0b1111111101111111)

/* ── BAM 비트 마스크 ── */
#define MASK_OFF       (16 - DEPTH)
#define PIX_MASK(d)    (1u << ((d) + MASK_OFF))

/* ── ESP32 WROOM TX FIFO 바이트 스왑 보정
 *   (원본 ESP32_TX_FIFO_POSITION_ADJUST) ── */
#define FX(x)  (((x) & 1u) ? (x) - 1 : (x) + 1)

/* ── GPIO 핀 배열 ── */
static const struct {
    int8_t r1, g1, b1;
    int8_t r2, g2, b2;
    int8_t lat, oe;
    int8_t a, b, c, d, e;
    int8_t clk;
} HUB75_PINS = {
    25, 26, 27,
    14, 12, 13,
    18, 19,
     2,  4,  5, 17, 15,
    16
};

/* ── CIE 1931 감마 보정 LUT (컴파일 타임 생성)
 *   공식: L = x*100/255
 *         Y = (L<=8) ? L/903.3 : ((L+16)/116)^3
 *   출력 = Y * 65535
 *   하드코딩 없이 컴파일러가 동일한 256개 값을 자동 계산 ── */
static constexpr u16 _cie_val(int x) {
    float L = x * (100.0f / 255.0f);
    float t = (L + 16.0f) / 116.0f;
    float Y = (L <= 8.0f) ? (L / 903.3f) : (t * t * t);
    float v = Y * 65535.0f;
    return (u16)(v < 0.0f ? 0.0f : (v > 65535.0f ? 65535.0f : v));
}
template<int... Is> struct _CieSeq {};
template<int N, int... Is> struct _CieMake : _CieMake<N-1, N-1, Is...> {};
template<int... Is> struct _CieMake<0, Is...> { using type = _CieSeq<Is...>; };
template<int... Is>
static constexpr auto _make_cie(_CieSeq<Is...>) {
    return std::array<u16, 256>{{ _cie_val(Is)... }};
}
static constexpr auto CIE = _make_cie(typename _CieMake<256>::type{});

/* ── color565 유틸리티 ── */
static inline u16 color565(uint8_t r, uint8_t g, uint8_t b) {
    return ((u16)(r & 0xF8u) << 8) | ((u16)(g & 0xFCu) << 3) | (b >> 3);
}
