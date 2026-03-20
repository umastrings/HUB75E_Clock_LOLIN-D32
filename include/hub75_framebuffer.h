#pragma once
/*==============================================================
 * hub75_framebuffer.h  —  BAM 비트플레인 프레임버퍼
 *==============================================================*/

#include "hub75_config.h"
#include <rom/lldesc.h>

/* ── 한 논리 행의 DEPTH개 비트플레인 ── */
struct Row {
    u16 *data;
    u16 *pl(int d) { return data + (size_t)d * COLS; }
};

/* ── 전체 프레임 (ROWS개 행) ── */
struct Frame {
    Row rows[ROWS];
    int n = 0;
};

/* ── 더블버퍼 전역 (hub75_framebuffer.cpp 에서 정의) ── */
extern Frame          FB[2];
extern Frame         *fb;          /* 현재 쓰기 대상 (back buffer) */
extern volatile int   back_id;
extern lldesc_t      *dmaA;
extern lldesc_t      *dmaB;
extern int            dma_last;

/* ── 함수 선언 ── */
bool  fb_alloc(void);               /* DMA SRAM 할당 */
void  clearFrame(int fid);          /* 제어 비트 초기화 */
void  setBrightOE(int fid, uint8_t brt);  /* 밝기 OE 적용 */

void  setPixel(u16 x, u16 y, uint8_t r, uint8_t g, uint8_t b);
void  fillScreen(uint8_t r, uint8_t g, uint8_t b);
void  clearScreen(void);

void  buildChain(lldesc_t *ds, Frame *f);
void  flipDMABuffer(void);
