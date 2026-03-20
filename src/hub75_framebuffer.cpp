/*==============================================================
 * hub75_framebuffer.cpp  —  BAM 비트플레인 프레임버퍼 구현
 *==============================================================*/

#include "hub75_framebuffer.h"
#include <string.h>
#include <algorithm>
#include <esp_heap_caps.h>
#include <esp_log.h>

static const char *TAG = "FB";

/* ── 전역 변수 정의 ── */
Frame         FB[2];
Frame        *fb      = &FB[0];
volatile int  back_id = 0;
lldesc_t     *dmaA    = nullptr;
lldesc_t     *dmaB    = nullptr;
int           dma_last = 0;

/* ── DMA SRAM 할당 ── */
bool fb_alloc(void) {
    for (int f = 0; f < 2; f++) {
        FB[f].n = 0;
        for (int r = 0; r < ROWS; r++) {
            FB[f].rows[r].data = (u16 *)heap_caps_calloc(
                DEPTH * COLS, sizeof(u16),
                MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
            if (!FB[f].rows[r].data) {
                ESP_LOGE(TAG, "alloc fail fb=%d row=%d", f, r);
                return false;
            }
            FB[f].n++;
        }
    }
    return true;
}

/* ── clearFrame  —  원본 clearFrameBuffer() 그대로
 *
 *  ABCDE 행 주소를 data[] 전체에 기입:
 *   plane1~3 구간 → 현재 행 주소
 *   plane0   구간 → 이전 행 주소 (LSB는 이전 행 표시 중 전송)
 *  LAT: 각 plane 마지막 픽셀
 *  OE : LAT 전후 LAT_BLANK 클럭 HIGH (고스팅 방지)        ── */
void clearFrame(int fid) {
    Frame *f = &FB[fid];
    int ri = f->n;
    do {
        --ri;
        u16 *row = f->rows[ri].pl(0);
        u16 ac   = (u16)ri << ADDR_SH;
        u16 ap   = (u16)((ri == 0) ? ROWS - 1 : ri - 1) << ADDR_SH;

        int xp = DEPTH * COLS;
        do { --xp; row[FX(xp)] = ac; } while (xp != COLS);
        do { --xp; row[FX(xp)] = ap; } while (xp);

        for (int d = DEPTH - 1; d >= 0; d--) {
            u16 *pl = f->rows[ri].pl(d);
            pl[FX(COLS - 1)] |= BIT_LAT;
            for (int b = 0; b < LAT_BLANK; b++) {
                pl[FX(b)]          |= BIT_OE;
                pl[FX(COLS - 1)]   |= BIT_OE;
                pl[FX(COLS-b-1)]   |= BIT_OE;
            }
        }
    } while (ri);
}

/* ── setBrightOE  —  원본 setBrightnessOE() 그대로 ── */
void setBrightOE(int fid, uint8_t brt) {
    Frame *f = &FB[fid];
    int ri = f->n;
    do {
        --ri;
        for (int ci = DEPTH - 1; ci >= 0; ci--) {
            int bp  = (2 * DEPTH - ci) % DEPTH;
            int bs  = (DEPTH - 1) >> 1;
            int rs  = std::max(bp - bs - 2, 0);
            int bpx = ((COLS - LAT_BLANK) * brt) >> (7 + rs);
            bpx = (bpx >> 1) | (bpx & 1);

            u16 *pl  = f->rows[ri].pl(ci);
            int xmax = (COLS + bpx + 1) >> 1;
            int xmin = (COLS - bpx + 0) >> 1;
            int xc   = COLS;
            do {
                --xc;
                if (xc >= xmin && xc < xmax) pl[FX(xc)] &= CLR_OE;
                else                          pl[FX(xc)] |= BIT_OE;
            } while (xc);
        }
    } while (ri);
}

/* ── setPixel  —  원본 updateMatrixDMABuffer(x,y,r,g,b) 그대로 ── */
void IRAM_ATTR setPixel(u16 x, u16 y, uint8_t r, uint8_t g, uint8_t b) {
    if (x >= COLS || y >= (u16)MATRIX_H) return;

    u16 r16 = CIE[r], g16 = CIE[g], b16 = CIE[b];
    u16 clr = CLR_RGB1, sh = 0;
    u16 ry  = y;

    if (y >= ROWS) { ry = (u16)(y - ROWS); clr = CLR_RGB2; sh = BITS_RGB2; }

    u16 ax = (u16)FX(x);
    for (int d = DEPTH - 1; d >= 0; d--) {
        u16 mask = (u16)PIX_MASK(d);
        u16 rgb  = 0;
        rgb |= (u16)((b16 & mask) != 0); rgb = (u16)(rgb << 1);
        rgb |= (u16)((g16 & mask) != 0); rgb = (u16)(rgb << 1);
        rgb |= (u16)((r16 & mask) != 0); rgb = (u16)(rgb << sh);

        u16 *pl = fb->rows[ry].pl(d);
        pl[ax]  = (u16)((pl[ax] & clr) | rgb);
    }
}

/* ── fillScreen  —  원본 updateMatrixDMABuffer(r,g,b) 그대로 ── */
void fillScreen(uint8_t r, uint8_t g, uint8_t b) {
    u16 r16 = CIE[r], g16 = CIE[g], b16 = CIE[b];
    for (int d = 0; d < DEPTH; d++) {
        u16 mask = (u16)PIX_MASK(d);
        u16 rgb  = 0;
        rgb |= (u16)((b16 & mask) != 0); rgb = (u16)(rgb << 1);
        rgb |= (u16)((g16 & mask) != 0); rgb = (u16)(rgb << 1);
        rgb |= (u16)((r16 & mask) != 0);
        u16 both = (u16)(rgb | (u16)(rgb << BITS_RGB2));
        for (int ri = 0; ri < ROWS; ri++) {
            u16 *pl = fb->rows[ri].pl(d);
            for (int x = 0; x < COLS; x++) { pl[x] &= CLR_RGB12; pl[x] |= both; }
        }
    }
}

void clearScreen(void) { fillScreen(0, 0, 0); }

/* ── buildChain  —  원본 setupDMA Step4 그대로
 *   row당: Step1(전체 depth 1회) + Step2(plane i를 2^(i-1)회)
 *   4-bit 기준: 1+1+2+4 = 8 desc × 32 rows = 256 desc/buffer ── */
/* lldesc_t 초기화 헬퍼 — ESP-IDF 버전별 구조체 차이를 흡수 */
static void _desc_set(lldesc_t *d, size_t sz, uint8_t *buf, lldesc_t *nxt) {
    memset(d, 0, sizeof(lldesc_t));
    d->size         = (uint32_t)sz;
    d->length       = (uint32_t)sz;
    d->buf          = buf;
    d->owner        = 1;
    d->qe.stqe_next = nxt;
}

void buildChain(lldesc_t *ds, Frame *f) {
    int    idx = 0;
    size_t ab  = (size_t)DEPTH * COLS * sizeof(u16);
    size_t pb  = (size_t)COLS  * sizeof(u16);

    for (int row = 0; row < ROWS; row++) {
        /* Step1: 전체 depth 한번에 */
        _desc_set(&ds[idx], ab, (uint8_t *)f->rows[row].pl(0), &ds[idx+1]);
        idx++;

        /* Step2: plane i를 2^(i-1)회 반복 */
        for (int i = 1; i < DEPTH; i++) {
            for (int k = 0; k < (1 << (i-1)); k++) {
                _desc_set(&ds[idx], pb, (uint8_t *)f->rows[row].pl(i), &ds[idx+1]);
                idx++;
            }
        }
    }
    /* 마지막 desc 를 체인 첫 번째로 순환 */
    ds[idx-1].eof          = 1;
    ds[idx-1].qe.stqe_next = ds;
    dma_last               = idx - 1;
}

/* ── flipDMABuffer  —  원본 flip_dma_output_buffer() 그대로
 *   out_link.addr 교체가 아닌 last desc의 next 포인터 변경
 *   → 현재 프레임 끝에서 자연스럽게 전환, 티어링 없음       ── */
void flipDMABuffer(void) {
    if (back_id == 1) {
        dmaB[dma_last].qe.stqe_next = &dmaB[0];
        dmaA[dma_last].qe.stqe_next = &dmaB[0];
    } else {
        dmaA[dma_last].qe.stqe_next = &dmaA[0];
        dmaB[dma_last].qe.stqe_next = &dmaA[0];
    }
    back_id ^= 1;
    fb = &FB[back_id];
}