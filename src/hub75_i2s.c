/*==============================================================
 * hub75_i2s.c  —  ESP32 I2S1 병렬 DMA 드라이버 구현 (C)
 *
 * [노트 14 "버그와 해결 기록" 의 3대 함정 — 그대로 유지할 것]
 *   ① tx_fifo_mod = 1      (16-bit single channel). 3 으로 두면 화면 안 나옴
 *   ② clka_en    = 0       (80MHz PLL_D2). APB 160MHz 쓰면 타이밍 2배 빨라짐
 *   ③ signal base = I2S1O_DATA_OUT8_IDX   (OUT0 부터 연결하면 8핀 어긋남)
 *   추가: tx_bck_div_num 은 1 금지 (TRM 명시) → 2 사용
 *==============================================================*/

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_private/periph_ctrl.h>
#include <esp_rom_gpio.h>
#include <driver/gpio.h>
#include <soc/i2s_reg.h>
#include <soc/i2s_struct.h>
#include <soc/gpio_sig_map.h>

#include "hub75_i2s.h"

static const char *TAG = "I2S";

/* ── routeGPIO  —  13개 데이터 핀 + CLK 라우팅
 *   I2S1 16-bit 모드의 출력 신호 base 는 I2S1O_DATA_OUT8_IDX 이다.
 *   pin_data[i] → DATA_OUT8 + i, CLK → I2S1O_WS_OUT_IDX        ── */
void routeGPIO(void)
{
    static const int8_t pins[HUB75_DATA_PIN_CNT] = HUB75_DATA_PINS;

    for (int i = 0; i < HUB75_DATA_PIN_CNT; i++) {
        if (pins[i] < 0) continue;
        gpio_reset_pin((gpio_num_t)pins[i]);
        gpio_set_direction((gpio_num_t)pins[i], GPIO_MODE_OUTPUT);
        gpio_set_drive_capability((gpio_num_t)pins[i], GPIO_DRIVE_CAP_3);
        esp_rom_gpio_connect_out_signal(
            (gpio_num_t)pins[i], I2S1O_DATA_OUT8_IDX + i, false, false);
    }

    /* CLK → I2S1 WS */
    gpio_reset_pin((gpio_num_t)PIN_CLK);
    gpio_set_direction((gpio_num_t)PIN_CLK, GPIO_MODE_OUTPUT);
    gpio_set_drive_capability((gpio_num_t)PIN_CLK, GPIO_DRIVE_CAP_3);
    esp_rom_gpio_connect_out_signal(
        (gpio_num_t)PIN_CLK, I2S1O_WS_OUT_IDX, false, false);
}

/* ── configI2S  —  I2S1 을 LCD 병렬 16-bit 모드로 설정 ── */
void configI2S(void)
{
    i2s_dev_t *dev = &I2S1;

    periph_module_reset(PERIPH_I2S1_MODULE);
    periph_module_enable(PERIPH_I2S1_MODULE);

    /* 클럭: 80MHz PLL_D2 / div_num(4) / bck_div(2) = 10MHz */
    dev->sample_rate_conf.val            = 0;
    dev->sample_rate_conf.rx_bits_mod    = 16;
    dev->sample_rate_conf.tx_bits_mod    = 16;
    dev->sample_rate_conf.tx_bck_div_num = I2S_BCK_DIV;   /* ★ 1 금지 */
    dev->sample_rate_conf.rx_bck_div_num = I2S_BCK_DIV;

    dev->clkm_conf.val          = 0;
    dev->clkm_conf.clka_en      = 0;              /* ★ 0 = 80MHz PLL_D2 */
    dev->clkm_conf.clkm_div_a   = 1;
    dev->clkm_conf.clkm_div_b   = 0;
    dev->clkm_conf.clkm_div_num = I2S_DIV_NUM;

    ESP_LOGI(TAG, "I2S clk: 80MHz / %d / %d = %d MHz",
             I2S_DIV_NUM, I2S_BCK_DIV,
             (int)(80000000 / I2S_DIV_NUM / I2S_BCK_DIV / 1000000));

    /* LCD 병렬 모드 */
    dev->conf2.val            = 0;
    dev->conf2.lcd_en         = 1;
    dev->conf2.lcd_tx_wrx2_en = 0;
    dev->conf2.lcd_tx_sdx2_en = 0;

    dev->conf.val = 0;

    /* FIFO: tx_fifo_mod = 1 (16-bit single channel) */
    dev->fifo_conf.val                  = 0;
    dev->fifo_conf.rx_data_num          = 32;
    dev->fifo_conf.tx_data_num          = 32;
    dev->fifo_conf.dscr_en              = 1;
    dev->fifo_conf.tx_fifo_mod          = 1;      /* ★ 16-bit */
    dev->fifo_conf.rx_fifo_mod_force_en = 1;
    dev->fifo_conf.tx_fifo_mod_force_en = 1;

    dev->conf_chan.val         = 0;
    dev->conf_chan.tx_chan_mod = 1;
    dev->conf_chan.rx_chan_mod = 1;

    /* 리셋 시퀀스 */
    dev->conf.rx_fifo_reset = 1; dev->conf.rx_fifo_reset = 0;
    dev->conf.tx_fifo_reset = 1; dev->conf.tx_fifo_reset = 0;

    dev->lc_conf.in_rst   = 1; dev->lc_conf.in_rst   = 0;
    dev->lc_conf.out_rst  = 1; dev->lc_conf.out_rst  = 0;
    dev->lc_conf.ahbm_rst = 1; dev->lc_conf.ahbm_rst = 0;

    dev->in_link.val  = 0;
    dev->out_link.val = 0;

    dev->conf.rx_reset = 1; dev->conf.tx_reset = 1;
    dev->conf.rx_reset = 0; dev->conf.tx_reset = 0;

    dev->conf1.val        = 0;
    dev->conf1.tx_stop_en = 0;
    dev->timing.val       = 0;
}

/* row 당 descriptor 개수: Step1(1) + Step2(1+2+...+2^(DEPTH-2)) */
static int desc_per_row(void)
{
    int n = 1;
    for (int i = 1; i < DEPTH; i++) n += (1 << (i - 1));
    return n;                                   /* DEPTH=4 → 8 */
}

/* ── panelBegin  —  전체 초기화
 *   1. CIE LUT 생성   2. 프레임버퍼 DMA SRAM 할당
 *   3. GPIO 라우팅    4. I2S 레지스터 설정
 *   5. DMA 체인 구성  6. 제어 비트 + 밝기   7. DMA 전송 시작   ── */
bool panelBegin(void)
{
    cie_init();

    if (!fb_alloc()) return false;

    routeGPIO();
    configI2S();

    int dc = ROWS * desc_per_row();             /* 32 x 8 = 256 */
    dmaA = (lldesc_t *)heap_caps_calloc(dc, sizeof(lldesc_t), MALLOC_CAP_DMA);
    dmaB = (lldesc_t *)heap_caps_calloc(dc, sizeof(lldesc_t), MALLOC_CAP_DMA);
    if (!dmaA || !dmaB) {
        ESP_LOGE(TAG, "DMA desc alloc fail (%d desc)", dc);
        return false;
    }

    buildChain(dmaA, &FB[0]);
    buildChain(dmaB, &FB[1]);

    clearFrame(0); setBrightOE(0, BRIGHTNESS);
    clearFrame(1); setBrightOE(1, BRIGHTNESS);

    /* flip 후: back_id=1, fb=FB[1], dmaA 가 순환 체인으로 설정됨 */
    back_id = 0;
    fb      = &FB[0];
    flipDMABuffer();

    /* DMA burst 활성화 후 전송 시작 */
    I2S1.lc_conf.val    = I2S_OUT_DATA_BURST_EN | I2S_OUTDSCR_BURST_EN;
    I2S1.out_link.addr  = (uint32_t)dmaA;
    I2S1.out_link.stop  = 0;
    I2S1.out_link.start = 1;
    I2S1.conf.tx_start  = 1;

    ESP_LOGI(TAG, "Panel ready  %dx%d  depth=%d  desc=%d",
             MATRIX_W, MATRIX_H, DEPTH, dc);
    return true;
}
