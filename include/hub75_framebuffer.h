#ifndef HUB75_FRAMEBUFFER_H
#define HUB75_FRAMEBUFFER_H
/*==============================================================
 * hub75_framebuffer.h  —  BAM 비트플레인 프레임버퍼
 *==============================================================*/

#include <stdbool.h>
#include <rom/lldesc.h>
#include "hub75_config.h"

/* ── 한 논리 행: DEPTH개 비트플레인이 data[] 에 연속 배치 ── */
typedef struct {
    u16 *data;
} Row;

/* ── 전체 프레임 (ROWS개 행) ── */
typedef struct {
    Row rows[ROWS];
    int n;
} Frame;

/* 원본 C++ 의 Row::pl(d) 대체 매크로
 *   ROW_PL(frame_ptr, row_index, depth) → u16*                */
#define ROW_PL(f, r, d)  ((f)->rows[(r)].data + (size_t)(d) * COLS)

/* ── 더블버퍼 전역 (hub75_framebuffer.c 에서 정의) ── */
extern Frame          FB[2];
extern Frame         *fb;          /* 현재 쓰기 대상 (back buffer) */
extern volatile int   back_id;
extern lldesc_t      *dmaA;
extern lldesc_t      *dmaB;
extern int            dma_last;

/* ── 함수 선언 ── */
bool fb_alloc(void);                      /* DMA SRAM 할당 */
void clearFrame(int fid);                 /* 제어 비트 초기화 */
void setBrightOE(int fid, uint8_t brt);   /* 밝기 OE 적용 */

void setPixel(u16 x, u16 y, uint8_t r, uint8_t g, uint8_t b);
void fillScreen(uint8_t r, uint8_t g, uint8_t b);
void clearScreen(void);

void buildChain(lldesc_t *ds, Frame *f);
void flipDMABuffer(void);

#endif /* HUB75_FRAMEBUFFER_H */
