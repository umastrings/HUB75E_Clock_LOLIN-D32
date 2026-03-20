/*==============================================================
 * hub75_i2s.cpp  —  ESP32 I2S1 병렬 DMA 드라이버 구현
 *
 * [핵심 설정값 — 원본 Bus_Parallel16::init() ESP32 분기 그대로]
 *   클럭 소스 : 80MHz PLL_D2  (clka_en=0)
 *   분주      : div_num=4, bck_div=2  →  10MHz 최종
 *   FIFO 모드 : tx_fifo_mod=1  (16-bit single channel)
 *   GPIO base : I2S1O_DATA_OUT8_IDX  (I2S1 16-bit 모드)
 *   flip 방식 : last_desc->next 포인터 변경  (티어링 없음)
 *==============================================================*/

#include "hub75_i2s.h"
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_private/periph_ctrl.h>
#include <esp_rom_gpio.h>
#include <driver/gpio.h>
#include <soc/i2s_reg.h>
#include <soc/i2s_struct.h>
#include <soc/gpio_sig_map.h>
#include <soc/i2s_periph.h>

static const char *TAG = "I2S";

/* ── routeGPIO  —  원본 Bus_Parallel16::init() GPIO 설정 그대로
 *   I2S1 16-bit 모드: signal base = I2S1O_DATA_OUT8_IDX
 *   pin_data[i] → DATA_OUT8+i                               ── */
void routeGPIO(void) {
    const int8_t pins[13] = {
        HUB75_PINS.r1, HUB75_PINS.g1, HUB75_PINS.b1,
        HUB75_PINS.r2, HUB75_PINS.g2, HUB75_PINS.b2,
        HUB75_PINS.lat, HUB75_PINS.oe,
        HUB75_PINS.a, HUB75_PINS.b, HUB75_PINS.c,
        HUB75_PINS.d, HUB75_PINS.e
    };
    for (int i = 0; i < 13; i++) {
        if (pins[i] < 0) continue;
        gpio_reset_pin((gpio_num_t)pins[i]);
        gpio_set_direction((gpio_num_t)pins[i], GPIO_MODE_OUTPUT);
        gpio_set_drive_capability((gpio_num_t)pins[i], (gpio_drive_cap_t)3);
        esp_rom_gpio_connect_out_signal(
            (gpio_num_t)pins[i], I2S1O_DATA_OUT8_IDX + i, false, false);
    }
    /* CLK → I2S1 WS */
    gpio_reset_pin((gpio_num_t)HUB75_PINS.clk);
    gpio_set_direction((gpio_num_t)HUB75_PINS.clk, GPIO_MODE_OUTPUT);
    gpio_set_drive_capability((gpio_num_t)HUB75_PINS.clk, (gpio_drive_cap_t)3);
    esp_rom_gpio_connect_out_signal(
        (gpio_num_t)HUB75_PINS.clk, I2S1O_WS_OUT_IDX, false, false);
}

/* ── configI2S  —  원본 Bus_Parallel16::init() 레지스터 설정 그대로 ── */
void configI2S(void) {
    auto dev = &I2S1;

    periph_module_reset(PERIPH_I2S1_MODULE);
    periph_module_enable(PERIPH_I2S1_MODULE);

    /* 클럭: 80MHz PLL_D2 / 4 / 2 = 10MHz */
    dev->sample_rate_conf.val          = 0;
    dev->sample_rate_conf.rx_bits_mod  = 16;
    dev->sample_rate_conf.tx_bits_mod  = 16;
    dev->sample_rate_conf.tx_bck_div_num = I2S_BCK_DIV;
    dev->sample_rate_conf.rx_bck_div_num = I2S_BCK_DIV;

    dev->clkm_conf.val          = 0;
    dev->clkm_conf.clka_en      = 0;   /* 80MHz PLL_D2 */
    dev->clkm_conf.clkm_div_a   = 1;
    dev->clkm_conf.clkm_div_b   = 0;
    dev->clkm_conf.clkm_div_num = I2S_DIV_NUM;
    ESP_LOGI(TAG, "I2S clk: 80MHz / %d / %d = %luMHz",
             I2S_DIV_NUM, I2S_BCK_DIV,
             80000000ul / I2S_DIV_NUM / I2S_BCK_DIV / 1000000);

    /* LCD 병렬 모드 */
    dev->conf2.val             = 0;
    dev->conf2.lcd_en          = 1;
    dev->conf2.lcd_tx_wrx2_en  = 0;
    dev->conf2.lcd_tx_sdx2_en  = 0;

    dev->conf.val = 0;

    /* FIFO: tx_fifo_mod=1 (16-bit single channel) */
    dev->fifo_conf.val                  = 0;
    dev->fifo_conf.rx_data_num          = 32;
    dev->fifo_conf.tx_data_num          = 32;
    dev->fifo_conf.dscr_en              = 1;
    dev->fifo_conf.tx_fifo_mod          = 1;   /* ★ 16-bit */
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

/* ── panelBegin  —  전체 초기화 순서
 *   1. 프레임버퍼 DMA SRAM 할당
 *   2. GPIO 라우팅
 *   3. I2S 레지스터 설정
 *   4. DMA 디스크립터 체인 구성
 *   5. 제어 비트 + 밝기 초기화
 *   6. DMA 시작 (buffer A 출력)                            ── */
bool panelBegin(void) {
    if (!fb_alloc()) return false;

    routeGPIO();
    configI2S();

    /* DMA 디스크립터 할당: 32rows × (1+1+2+4) = 256 desc/buf */
    int dc = ROWS * (1 + 1 + 2 + 4);
    dmaA = (lldesc_t *)heap_caps_calloc(dc, sizeof(lldesc_t), MALLOC_CAP_DMA);
    dmaB = (lldesc_t *)heap_caps_calloc(dc, sizeof(lldesc_t), MALLOC_CAP_DMA);
    if (!dmaA || !dmaB) { ESP_LOGE(TAG, "DMA alloc fail"); return false; }

    buildChain(dmaA, &FB[0]);
    buildChain(dmaB, &FB[1]);

    clearFrame(0); setBrightOE(0, BRIGHTNESS);
    clearFrame(1); setBrightOE(1, BRIGHTNESS);

    /* flipDMABuffer() 후: back_id=1, fb=FB[1], dmaA 순환 설정됨 */
    back_id = 0; fb = &FB[0];
    flipDMABuffer();

    /* DMA burst + 전송 시작 */
    I2S1.lc_conf.val    = I2S_OUT_DATA_BURST_EN | I2S_OUTDSCR_BURST_EN;
    I2S1.out_link.addr  = (uint32_t)dmaA;
    I2S1.out_link.stop  = 0;
    I2S1.out_link.start = 1;
    I2S1.conf.tx_start  = 1;

    ESP_LOGI(TAG, "Panel ready  %dx%d  depth=%d", MATRIX_W, MATRIX_H, DEPTH);
    return true;
}
