#ifndef HUB75_PANEL_H
#define HUB75_PANEL_H
/*==============================================================
 * hub75_panel.h  —  GFX 텍스트/픽셀 레이어 선언
 *==============================================================*/

#include "hub75_config.h"

/* ── GFX 커서 / 색상 / 크기 설정 ── */
void setCursor(int16_t x, int16_t y);
void setColor(u16 c);
void setSize(uint8_t s);

/* ── 텍스트 출력 ── */
void drawChar(int16_t x, int16_t y, char c, u16 col, uint8_t sz);
void printStr(const char *s);

/* 문자열 픽셀 폭 (5x7 폰트, 문자당 6*sz) — 정렬 계산용 */
int  textWidth(const char *s, uint8_t sz);

#endif /* HUB75_PANEL_H */
