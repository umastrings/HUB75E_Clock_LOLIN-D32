/*==============================================================
 * hub75_framebuffer.c  —  BAM 비트플레인 프레임버퍼 구현 (C)
 *
 * [노트 15 "최적화 기록" 반영 사항]
 *   ① setPixel   : CIE[x]>>MASK_OFF 로 마스크 비교 제거 (12회→4회)
 *   ② fillScreen : plane 상수 사전계산 + u32 쌍 기입 + IRAM 상주
 *   ③ clearFrame : FX 호출 512→0, u16 512회 쓰기 → u32 256회
 *   ④ IRAM_ATTR  : hot path 함수만 내부 RAM 상주
 *==============================================================*/

#include <string.h>
#include <math.h>
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_log.h>

#include "hub75_framebuffer.h"

static const char *TAG = "FB";

/* u32 쌍 기입 최적화는 COLS 가 짝수여야 성립한다. */
_Static_assert(COLS % 2 == 0, "COLS must be even for u32 pair writes");

/* ── 전역 변수 정의 ── */
u16           CIE[256];
Frame         FB[2];
Frame        *fb       = &FB[0];
volatile int  back_id  = 0;
lldesc_t     *dmaA     = NULL;
lldesc_t     *dmaB     = NULL;
int           dma_last = 0;

/* ── cie_init  —  CIE 1931 감마 LUT 런타임 생성 (부팅 시 1회)
 *   L = x*100/255,  Y = (L<=8) ? L/903.3 : ((L+16)/116)^3
 *   원본 C++ 의 constexpr 템플릿 버전과 값이 동일하다.      ── */
void cie_init(void)
{
    for (int x = 0; x < 256; x++) {
        float L = (float)x * (100.0f / 255.0f);
        float t = (L + 16.0f) / 116.0f;
        float Y = (L <= 8.0f) ? (L / 903.3f) : (t * t * t);
        float v = Y * 65535.0f;
        if (v < 0.0f)       v = 0.0f;
        if (v > 65535.0f)   v = 65535.0f;
        CIE[x] = (u16)v;
    }
}

/* ── DMA SRAM 할당 ── */
bool fb_alloc(void)
{
    for (int f = 0; f < 2; f++) {
        FB[f].n = 0;
        for (int r = 0; r < ROWS; r++) {
            FB[f].rows[r].data = (u16 *)heap_caps_calloc(
                (size_t)DEPTH * COLS, sizeof(u16),
                MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
            if (!FB[f].rows[r].data) {
                ESP_LOGE(TAG, "alloc fail fb=%d row=%d", f, r);
                return false;
            }
            FB[f].n++;
        }
    }
    ESP_LOGI(TAG, "framebuffer %d B x 2", (int)(ROWS * DEPTH * COLS * sizeof(u16)));
    return true;
}

/* ── clearFrame  —  제어 비트(행주소 / LAT / OE) 초기화
 *
 *  ABCDE 행 주소를 data[] 전체에 기입:
 *   plane1~3 구간 → 현재 행 주소
 *   plane0   구간 → 이전 행 주소 (LSB는 이전 행 표시 중 전송)
 *  LAT: 각 plane 마지막 픽셀
 *  OE : LAT 전후 LAT_BLANK 클럭 HIGH (고스팅 방지)
 *
 *  [최적화] 주소 기입 구간은 값이 모두 동일 → FIFO 바이트 스왑(FX)의
 *  영향을 받지 않는다. 따라서 순차 u32 쌍 기입이 가능하다.
 *  plane 경계(COLS)가 짝수라 u32 경계와 정확히 맞아떨어진다.
 *  단, LAT/OE 는 위치 의존이므로 FX 를 그대로 유지해야 한다.   ── */
void clearFrame(int fid)
{
    Frame *f = &FB[fid];

    for (int ri = 0; ri < f->n; ri++) {
        u16      *row   = ROW_PL(f, ri, 0);
        uint32_t *row32 = (uint32_t *)row;

        u16 ac = (u16)((u16)ri << ADDR_SH);                                  /* 현재 행 */
        u16 ap = (u16)((u16)((ri == 0) ? (ROWS - 1) : (ri - 1)) << ADDR_SH); /* 이전 행 */

        uint32_t pac = ((uint32_t)ac << 16) | ac;
        uint32_t pap = ((uint32_t)ap << 16) | ap;

        /* plane0 : 이전 행 주소 */
        for (int i = 0; i < COLS / 2; i++)
            row32[i] = pap;
        /* plane1..DEPTH-1 : 현재 행 주소 */
        for (int i = COLS / 2; i < (DEPTH * COLS) / 2; i++)
            row32[i] = pac;

        /* LAT / OE 는 위치 의존 → FX 적용 */
        for (int d = 0; d < DEPTH; d++) {
            u16 *pl = ROW_PL(f, ri, d);
            pl[FX(COLS - 1)] |= BIT_LAT;
            for (int b = 0; b < LAT_BLANK; b++) {
                int i0 = b;
                int i1 = COLS - 1;
                int i2 = COLS - b - 1;
                pl[FX(i0)] |= BIT_OE;
                pl[FX(i1)] |= BIT_OE;
                pl[FX(i2)] |= BIT_OE;
            }
        }
    }
}

/* ── setBrightOE  —  원본 setBrightnessOE() 와 동일 ── */
void setBrightOE(int fid, uint8_t brt)
{
    Frame *f = &FB[fid];

    for (int ri = 0; ri < f->n; ri++) {
        for (int ci = DEPTH - 1; ci >= 0; ci--) {
            int bp  = (2 * DEPTH - ci) % DEPTH;
            int bs  = (DEPTH - 1) >> 1;
            int rs  = bp - bs - 2;
            if (rs < 0) rs = 0;                       /* std::max 대체 */

            int bpx = ((COLS - LAT_BLANK) * (int)brt) >> (7 + rs);
            bpx = (bpx >> 1) | (bpx & 1);

            u16 *pl  = ROW_PL(f, ri, ci);
            int xmax = (COLS + bpx + 1) >> 1;
            int xmin = (COLS - bpx + 0) >> 1;

            for (int xc = COLS - 1; xc >= 0; xc--) {
                if (xc >= xmin && xc < xmax) pl[FX(xc)] &= CLR_OE;
                else                         pl[FX(xc)] |= BIT_OE;
            }
        }
    }
}

/* ── setPixel  —  back buffer 픽셀 1개 기입
 *
 *  [최적화 ①] MASK_OFF = 16-DEPTH 이므로 CIE[x] >> MASK_OFF 의
 *  하위 DEPTH 비트가 곧 각 비트플레인의 on/off 값이다.
 *  → 매 plane 마다 mask 를 만들어 & 비교할 필요가 없다.      ── */
void IRAM_ATTR setPixel(u16 x, u16 y, uint8_t r, uint8_t g, uint8_t b)
{
    if (x >= (u16)COLS || y >= (u16)MATRIX_H) return;

    u16 r4 = (u16)(CIE[r] >> MASK_OFF);
    u16 g4 = (u16)(CIE[g] >> MASK_OFF);
    u16 b4 = (u16)(CIE[b] >> MASK_OFF);

    u16 clr = CLR_RGB1;
    u16 sh  = 0;
    u16 ry  = y;

    if (y >= (u16)ROWS) {                 /* 하단 절반 → RGB2 채널 */
        ry  = (u16)(y - ROWS);
        clr = CLR_RGB2;
        sh  = BITS_RGB2;
    }

    int   ax = FX((int)x);
    Frame *f = fb;

    for (int d = 0; d < DEPTH; d++) {
        u16 rgb = (u16)((((b4 >> d) & 1u) << 2) |
                        (((g4 >> d) & 1u) << 1) |
                        ((r4 >> d) & 1u));
        rgb = (u16)(rgb << sh);

        u16 *pl = ROW_PL(f, ry, d);
        pl[ax] = (u16)((pl[ax] & clr) | rgb);
    }
}

/* ── fillScreen  —  전체 화면 단색 채우기
 *
 *  [최적화 ②] 1) plane 별 상수를 루프 밖에서 미리 계산
 *             2) u32 로 2픽셀씩 기입 → 메모리 접근 절반
 *             3) 모든 픽셀이 같은 값 → FX 스왑 무관 (순차 기입 가능)
 *                단, 여기서 건드리는 건 RGB 비트뿐이고 행주소/LAT/OE는
 *                CLR_RGB12 마스크로 보존되므로 안전하다.       ── */
void IRAM_ATTR fillScreen(uint8_t r, uint8_t g, uint8_t b)
{
    u16 r4 = (u16)(CIE[r] >> MASK_OFF);
    u16 g4 = (u16)(CIE[g] >> MASK_OFF);
    u16 b4 = (u16)(CIE[b] >> MASK_OFF);

    uint32_t bth32[DEPTH];
    for (int d = 0; d < DEPTH; d++) {
        u16 rgb  = (u16)((((b4 >> d) & 1u) << 2) |
                         (((g4 >> d) & 1u) << 1) |
                         ((r4 >> d) & 1u));
        u16 both = (u16)(rgb | (u16)(rgb << BITS_RGB2));  /* 상/하 동시 */
        bth32[d] = ((uint32_t)both << 16) | both;
    }

    const uint32_t mask32 = ((uint32_t)CLR_RGB12 << 16) | CLR_RGB12;
    Frame *f = fb;

    for (int d = 0; d < DEPTH; d++) {
        uint32_t v = bth32[d];
        for (int ri = 0; ri < ROWS; ri++) {
            uint32_t *p32 = (uint32_t *)ROW_PL(f, ri, d);
            for (int i = 0; i < COLS / 2; i++)
                p32[i] = (p32[i] & mask32) | v;
        }
    }
}

void clearScreen(void) { fillScreen(0, 0, 0); }

/* ── lldesc_t 초기화 헬퍼 — ESP-IDF 버전별 구조체 차이를 흡수 ── */
static void desc_set(lldesc_t *d, size_t sz, uint8_t *buf, lldesc_t *nxt)
{
    memset(d, 0, sizeof(lldesc_t));
    d->size         = (uint32_t)sz;
    d->length       = (uint32_t)sz;
    d->buf          = buf;
    d->owner        = 1;
    d->qe.stqe_next = nxt;
}

/* ── buildChain  —  DMA 디스크립터 링 구성
 *   row 당: Step1(전체 depth 1회) + Step2(plane i 를 2^(i-1)회)
 *   4-bit 기준: 1 + 1 + 2 + 4 = 8 desc x 32 rows = 256 desc/buffer ── */
void buildChain(lldesc_t *ds, Frame *f)
{
    int    idx = 0;
    size_t ab  = (size_t)DEPTH * COLS * sizeof(u16);   /* 전체 plane */
    size_t pb  = (size_t)COLS * sizeof(u16);           /* plane 1개 */

    for (int row = 0; row < ROWS; row++) {
        /* Step1: 전체 depth 한 번에 */
        desc_set(&ds[idx], ab, (uint8_t *)ROW_PL(f, row, 0), &ds[idx + 1]);
        idx++;

        /* Step2: plane i 를 2^(i-1)회 반복 → BAM 가중치 */
        for (int i = 1; i < DEPTH; i++) {
            for (int k = 0; k < (1 << (i - 1)); k++) {
                desc_set(&ds[idx], pb, (uint8_t *)ROW_PL(f, row, i), &ds[idx + 1]);
                idx++;
            }
        }
    }
    /* 마지막 desc 를 체인 첫 번째로 순환 */
    ds[idx - 1].eof          = 1;
    ds[idx - 1].qe.stqe_next = ds;
    dma_last                 = idx - 1;
}

/* ── flipDMABuffer  —  프레임 교체
 *   out_link.addr 교체가 아니라 마지막 desc 의 next 포인터를 바꾼다.
 *   → 현재 프레임을 끝까지 출력한 뒤 자연스럽게 전환, 티어링 없음 ── */
void flipDMABuffer(void)
{
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
