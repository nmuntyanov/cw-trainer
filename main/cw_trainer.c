/**
 * @file cw_trainer.c
 * @brief Main entry point for the CW Trainer Pro project.
 * 
 * Target: ESP32 Audio Kit V2.2 A404 (ES8388 codec)
 * Approach: Direct register access to ES8388 for high-performance audio control.
 * 
 * Features:
 * - Morse code decoding with rhythmic analysis.
 * - Koch method trainer via web dashboard.
 * - Real-time audio generation using I2S.
 */
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <inttypes.h>
#include "esp_timer.h"
#include "esp_timer.h"
#include <math.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "board_pins.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "cw_trainer.h"
#include "morse_logic.h"

static const char *TAG = "cw_trainer";

/* ── Pin definitions ─────────────────────────────────────────────────────── */
#define I2C_PORT        I2C_NUM_0
#define I2C_FREQ_HZ     100000
#define ES8388_ADDR     0x10        /* ADDR pin tied to GND */
#define PA_ENABLE_GPIO  GPIO_NUM_21 /* Power Amplifier enable */

/* ── ES8388 Register map (subset needed for DAC playback) ────────────────── */
#define ES8388_CHIPPOWER      0x02
#define ES8388_CONTROL1       0x00
#define ES8388_CONTROL2       0x01
#define ES8388_MASTERMODE     0x08
#define ES8388_DACPOWER       0x04
#define ES8388_ADCPOWER       0x03
#define ES8388_ADCCONTROL1    0x09
#define ES8388_ADCCONTROL2    0x0a
#define ES8388_ADCCONTROL3    0x0b
#define ES8388_ADCCONTROL4    0x0c
#define ES8388_ADCCONTROL5    0x0d
#define ES8388_ADCCONTROL8    0x10
#define ES8388_ADCCONTROL9    0x11
#define ES8388_DACCONTROL1    0x17
#define ES8388_DACCONTROL2    0x18
#define ES8388_DACCONTROL3    0x19
#define ES8388_DACCONTROL4    0x1a
#define ES8388_DACCONTROL5    0x1b
#define ES8388_DACCONTROL16   0x26
#define ES8388_DACCONTROL17   0x27
#define ES8388_DACCONTROL20   0x2a
#define ES8388_DACCONTROL21   0x2b
#define ES8388_DACCONTROL23   0x2d
#define ES8388_DACCONTROL24   0x2e
#define ES8388_DACCONTROL25   0x2f
#define ES8388_DACCONTROL26   0x30
#define ES8388_DACCONTROL27   0x31

/* ── Audio constants ─────────────────────────────────────────────────────── */
#define I2S_SAMPLE_RATE   44100
#define TONE_FREQ_HZ      600
#define SINE_TABLE_SIZE   256

/* ── State ───────────────────────────────────────────────────────────────── */
static int16_t sine_table[SINE_TABLE_SIZE];
static i2s_chan_handle_t tx_chan;

/* Global state for Web API control */
uint32_t freq = TONE_FREQ_HZ;
uint8_t vol_val = 30;
uint32_t wpm = 20;
uint8_t noise_level = 0;
char callsign[16] = "CALLSIGN";
char qth_locator[16] = "LOCATOR";
float phase_step = 0.0f;

/* Playback state machine */
typedef enum {
    PB_IDLE,
    PB_TONE,
    PB_ELEMENT_GAP,
    PB_CHAR_GAP,
    PB_WORD_GAP
} pb_state_t;

static char pb_string[64];
static int pb_str_idx = 0;
static const char *pb_seq = NULL;
static bool pb_active = false;
static bool pb_is_tone = false;
static uint32_t pb_blocks_left = 0;
static pb_state_t pb_state = PB_IDLE;

/* Biquad Filter for Realistic Noise */
typedef struct {
    float b0, b1, b2, a1, a2;
    float x1, x2, y1, y2;
} biquad_t;
static biquad_t noise_filter;

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

static void update_filter_coeffs(uint32_t f0)
{
    float Fs = (float)I2S_SAMPLE_RATE;
    float Q = 2.0f; // Narrowness of the filter
    float omega = 2.0f * M_PI * (float)f0 / Fs;
    float alpha = sinf(omega) / (2.0f * Q);
    float a0 = 1.0f + alpha;
    
    noise_filter.b0 = alpha / a0;
    noise_filter.b1 = 0.0f;
    noise_filter.b2 = -alpha / a0;
    noise_filter.a1 = (-2.0f * cosf(omega)) / a0;
    noise_filter.a2 = (1.0f - alpha) / a0;
    
    // Reset state
    noise_filter.x1 = noise_filter.x2 = noise_filter.y1 = noise_filter.y2 = 0.0f;
}

void trigger_playback(char c)
{
    char s[2] = {c, '\0'};
    trigger_playback_string(s);
}

void trigger_playback_string(const char* s)
{
    if (!s) return;
    strncpy(pb_string, s, sizeof(pb_string)-1);
    pb_string[sizeof(pb_string)-1] = '\0';
    pb_str_idx = 0;
    pb_seq = NULL;
    pb_state = PB_IDLE; // Reset state machine
    pb_blocks_left = 0;
    pb_active = true;
    ESP_LOGI(TAG, "Triggered string playback: %s", pb_string);
}

/* ══════════════════════════════════════════════════════════════════════════ */
/*  I2C helpers                                                               */
/* ══════════════════════════════════════════════════════════════════════════ */

static esp_err_t es8388_write_reg(uint8_t reg, uint8_t val)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ES8388_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, val, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ES8388 write reg 0x%02x = 0x%02x FAILED: %s", reg, val, esp_err_to_name(ret));
    }
    return ret;
}

/**
 * @brief Initialize the I2C bus for codec communication.
 * 
 * @note Includes a robust scan loop to detect the ES8388 and other devices.
 *       Includes safety delays to prevent interrupt watchdog timeouts.
 * 
 * @return esp_err_t ESP_OK on success.
 */
static esp_err_t init_i2c(void)
{
    i2c_config_t cfg = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = I2C_SDA_IO,
        .scl_io_num       = I2C_SCL_IO,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_param_config(I2C_PORT, &cfg), TAG, "i2c_param_config failed");
    ESP_RETURN_ON_ERROR(i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0), TAG, "i2c_driver_install failed");

    /* Scan to confirm codec presence */
    ESP_LOGI(TAG, "Scanning I2C bus...");
    for (uint8_t addr = 1; addr < 127; addr++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        /* Use absolute timeout and a bit more headroom for the probe */
        esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(50));
        i2c_cmd_link_delete(cmd);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "  Found I2C device at 0x%02x", addr);
        }
        /* Crucial: small delay between iterations to let hardware recover if bus is stuck/noisy */
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return ESP_OK;
}

static void save_settings(uint32_t f, uint8_t v, uint32_t w, uint8_t n, const char* c, const char* q)
{
    nvs_handle_t h;
    if (nvs_open("storage", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, "freq", f);
        nvs_set_u8(h, "vol", v);
        nvs_set_u32(h, "wpm", w);
        nvs_set_u8(h, "noise", n);
        if (c) nvs_set_str(h, "callsign", c);
        if (q) nvs_set_str(h, "qth", q);
        nvs_commit(h);
        nvs_close(h);
    }
}

void update_settings(uint32_t new_freq, uint8_t new_vol, uint32_t new_wpm, uint8_t new_noise, const char* new_callsign, const char* new_qth)
{
    if (new_freq >= 300 && new_freq <= 1500) {
        freq = new_freq;
        phase_step = (float)SINE_TABLE_SIZE * freq / I2S_SAMPLE_RATE;
        update_filter_coeffs(freq);
    }
    if (new_vol <= 33) {
        vol_val = new_vol;
        es8388_write_reg(ES8388_DACCONTROL24, vol_val); 
        es8388_write_reg(ES8388_DACCONTROL25, vol_val);
        es8388_write_reg(ES8388_DACCONTROL26, vol_val);
        es8388_write_reg(ES8388_DACCONTROL27, vol_val);
    }
    if (new_wpm >= 5 && new_wpm <= 60) {
        wpm = new_wpm;
        morse_logic_init(wpm);
    }
    if (new_noise <= 100) {
        noise_level = new_noise;
    }
    if (new_callsign) {
        strncpy(callsign, new_callsign, sizeof(callsign)-1);
        callsign[sizeof(callsign)-1] = '\0';
    }
    if (new_qth) {
        strncpy(qth_locator, new_qth, sizeof(qth_locator)-1);
        qth_locator[sizeof(qth_locator)-1] = '\0';
    }
    /* Broadcast update to web UI via WebSocket */
    char update_json[128];
    snprintf(update_json, sizeof(update_json), "{\"volume\":%d,\"freq\":%"PRIu32"}", vol_val, freq);
    web_server_broadcast(update_json);

    save_settings(freq, vol_val, wpm, noise_level, callsign, qth_locator);
    ESP_LOGI(TAG, "Settings updated: Freq=%"PRIu32"Hz, Vol=%d, WPM=%"PRIu32", Noise=%d, CS=%s, QTH=%s", freq, vol_val, wpm, noise_level, callsign, qth_locator);
}

static void load_settings(uint32_t *_freq, uint8_t *_vol, uint32_t *_wpm, uint8_t *_noise, char *_callsign, char *_qth)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &h);
    if (err == ESP_OK) {
        uint32_t f;
        uint8_t v;
        uint32_t w;
        uint8_t n;
        size_t cs_len = 16;
        size_t qth_len = 16;
        if (nvs_get_u32(h, "freq", &f) == ESP_OK) *_freq = f;
        if (nvs_get_u8(h, "vol", &v) == ESP_OK) *_vol = v;
        if (nvs_get_u32(h, "wpm", &w) == ESP_OK) *_wpm = w;
        if (nvs_get_u8(h, "noise", &n) == ESP_OK) *_noise = n;
        if (nvs_get_str(h, "callsign", _callsign, &cs_len) != ESP_OK) {
            strcpy(_callsign, "CALLSIGN");
        }
        if (nvs_get_str(h, "qth", _qth, &qth_len) != ESP_OK) {
            strcpy(_qth, "LOCATOR");
        }
        nvs_close(h);
        ESP_LOGI(TAG, "Settings loaded from NVS");
    } else {
        ESP_LOGW(TAG, "No NVS settings found (err %s), using defaults", esp_err_to_name(err));
    }
}

/* ══════════════════════════════════════════════════════════════════════════ */
/*  ES8388 initialisation (DAC playback only, I2S slave mode)                */
/*  Derived from ADF es8388.c es8388_init()                                  */
/* ══════════════════════════════════════════════════════════════════════════ */

static esp_err_t init_es8388(uint8_t initial_vol)
{
    esp_err_t ret = ESP_OK;

    /* 0. Unmute DAC, disable soft-ramp */
    ret |= es8388_write_reg(ES8388_DACCONTROL3, 0x00);  /* 0x04=mute, 0x00=unmute */

    /* 1. Chip power-up */
    ret |= es8388_write_reg(ES8388_CONTROL2,  0x50);
    ret |= es8388_write_reg(ES8388_CHIPPOWER,  0x00);   /* power-up all */

    /* 2. Disable internal DLL (improves low-sample-rate stability) */
    ret |= es8388_write_reg(0x35, 0xA0);
    ret |= es8388_write_reg(0x37, 0xD0);
    ret |= es8388_write_reg(0x39, 0xD0);

    /* 3. I2S slave mode */
    ret |= es8388_write_reg(ES8388_MASTERMODE, 0x00);   /* slave */

    /* 4. DAC setup */
    ret |= es8388_write_reg(ES8388_DACPOWER,    0xC0);  /* disable DAC outputs initially */
    ret |= es8388_write_reg(ES8388_CONTROL1,    0x12);  /* Play mode */
    ret |= es8388_write_reg(ES8388_DACCONTROL1, 0x18);  /* 16-bit I2S */
    ret |= es8388_write_reg(ES8388_DACCONTROL2, 0x02);  /* single speed, RATIO=256 */

    /* 5. Mixer: DAC L→Lout1, DAC R→Rout1 (no bypass) */
    ret |= es8388_write_reg(ES8388_DACCONTROL16, 0x00); /* LIN1/RIN1 */
    ret |= es8388_write_reg(ES8388_DACCONTROL17, 0x90); /* L DAC→L mixer, 0 dB */
    ret |= es8388_write_reg(ES8388_DACCONTROL20, 0x90); /* R DAC→R mixer, 0 dB */
    ret |= es8388_write_reg(ES8388_DACCONTROL21, 0x80); /* DAC LRCK as internal LRCK */
    ret |= es8388_write_reg(ES8388_DACCONTROL23, 0x00); /* VROI=0 */

    /* 7. Digital DAC volume pass-through: DACCONTROL4/5 (LDACVOL/RDACVOL) */
    ret |= es8388_write_reg(ES8388_DACCONTROL4, 0x00);  /* 0dB digital */
    ret |= es8388_write_reg(ES8388_DACCONTROL5, 0x00);  /* 0dB digital */

    /* 8. Analog gain: set to initial_vol specifically */
    ret |= es8388_write_reg(ES8388_DACCONTROL24, initial_vol);  /* LOUT1 gain */
    ret |= es8388_write_reg(ES8388_DACCONTROL25, initial_vol);  /* ROUT1 gain */
    ret |= es8388_write_reg(ES8388_DACCONTROL26, 0);            /* LOUT2 min */
    ret |= es8388_write_reg(ES8388_DACCONTROL27, 0);            /* ROUT2 min */

    /* 9. Enable DAC + LOUT1/ROUT1 (headphones). LOUT2/ROUT2 stays enabled but volume 0. */
    ret |= es8388_write_reg(ES8388_DACPOWER, 0x3C);

    /* 8. Power down ADC (not used) */
    ret |= es8388_write_reg(ES8388_ADCPOWER, 0xFF);

    /* 9. Start (re-confirm DACCONTROL21 → state machine restart) */
    ret |= es8388_write_reg(ES8388_DACCONTROL21, 0x80);
    ret |= es8388_write_reg(ES8388_CHIPPOWER,    0xF0); /* state machine reset */
    ret |= es8388_write_reg(ES8388_CHIPPOWER,    0x00); /* state machine start */

    /* 10. Power up DAC outputs again after state machine restart */
    ret |= es8388_write_reg(ES8388_DACPOWER, 0x3C);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ES8388 init failed");
    } else {
        ESP_LOGI(TAG, "ES8388 init OK (Vol index %d)", initial_vol);
    }
    return (ret == ESP_OK) ? ESP_OK : ESP_FAIL;
}

static void disable_pa(void)
{
    gpio_config_t io = {
        .pin_bit_mask = BIT64(PA_ENABLE_GPIO),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(PA_ENABLE_GPIO, 0); /* Force speaker amplifier OFF */
    ESP_LOGI(TAG, "PA disabled (GPIO %d)", PA_ENABLE_GPIO);
}

/* ── I2S ─────────────────────────────────────────────────────────────────── */
static esp_err_t init_i2s(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &tx_chan, NULL), TAG, "i2s_new_channel failed");

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(I2S_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_MCLK_IO,
            .bclk = I2S_BCK_IO,
            .ws   = I2S_WS_IO,
            .dout = I2S_DO_IO,
            .din  = I2S_DI_IO,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx_chan, &std_cfg), TAG, "i2s_channel_init_std_mode failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(tx_chan), TAG, "i2s_channel_enable failed");
    return ESP_OK;
}

/* ── Buttons ────────────────────────────────────────────────────────────── */
static void init_buttons(void)
{
    /* GPIO 36 is input-only, no pull-up possible */
    gpio_config_t io = {
        .pin_bit_mask  = BIT64(BUTTON_1_IO),
        .mode          = GPIO_MODE_INPUT,
        .pull_up_en    = GPIO_PULLUP_DISABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    /* Other buttons: GPIOs with internal pull-up */
    io.pin_bit_mask = BIT64(BUTTON_3_IO) | BIT64(BUTTON_4_IO) |
                      BIT64(BUTTON_5_IO) | BIT64(BUTTON_6_IO) |
                      BIT64(EXT_KEY_IO);
    io.pull_up_en   = GPIO_PULLUP_ENABLE;
    gpio_config(&io);
}

/* ── Sine table ─────────────────────────────────────────────────────────── */
static void build_sine_table(void)
{
    for (int i = 0; i < SINE_TABLE_SIZE; i++) {
        sine_table[i] = (int16_t)(sinf(2.0f * (float)M_PI * i / SINE_TABLE_SIZE) * 32000.0f);
    }
}

/* ══════════════════════════════════════════════════════════════════════════ */
/*  app_main                                                                  */
/* ══════════════════════════════════════════════════════════════════════════ */
/**
 * @brief Main application entry point.
 * 
 * Initializes all hardware peripherals (I2C, I2S, Buttons),
 * loads configuration from NVS, and starts the system services
 * (WiFi, Web Server, Morse Logic).
 */
void app_main(void)
{
    ESP_LOGI(TAG, "CW Trainer starting [v2 persistent] – ESP32 Audio Kit V2.2 A404");

    /* Initialize NVS */
    /* 1. NVS */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    build_sine_table();
    init_buttons();

    /* 2. Hardware Drivers (I2C should be first to ensure codec is ready before I2S) */
    ESP_ERROR_CHECK(init_i2c());
    vTaskDelay(pdMS_TO_TICKS(100)); // Brief pause after I2C init
    ESP_ERROR_CHECK(init_i2s());

    /* 3. Load settings AND apply to hardware */
    load_settings(&freq, &vol_val, &wpm, &noise_level, callsign, qth_locator);
    update_settings(freq, vol_val, wpm, noise_level, callsign, qth_locator);
    update_filter_coeffs(freq);
    ESP_LOGI(TAG, "Current settings: Freq %"PRIu32" Hz, Vol index %d, WPM %"PRIu32, freq, (int)vol_val, wpm);

    /* Phase 3: Morse Logic (depends on loaded WPM) */
    morse_logic_init(wpm); 

    /* ES8388 register init with loaded volume */
    ESP_ERROR_CHECK(init_es8388(vol_val));
    disable_pa();

    /* Phase 1: WiFi & Web Server */
    ESP_ERROR_CHECK(wifi_manager_init());
    ESP_ERROR_CHECK(web_server_init());

    /* Analog gain volume control (Registers 46/47/48)
     * 30 = 0dB, 33 = +4.5dB, 0 = -45dB. Step = 1.5dB. */
    float phase = 0.0f;
    int16_t buf[512];

    uint32_t last_freq_ms = 0;
    uint32_t last_vol_ms = 0;
    const uint32_t FREQ_DEBOUNCE_MS = 200;
    const uint32_t VOL_DEBOUNCE_MS = 120;

    bool key1_pressed = false;
    while (1) {
        /* Use high-resolution timer for Morse: microsecond precision reported as ms */
        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);

        /* Morse decoder timeouts */
        char c_timeout = morse_logic_update(now_ms);
        if (c_timeout) {
            ESP_LOGI(TAG, "Decoded: [%c] Score: %d", c_timeout, morse_logic_get_match_score());
            char b[64];
            snprintf(b, sizeof(b), "{\"char\":\"%c\",\"score\":%d}", c_timeout, morse_logic_get_match_score());
            web_server_broadcast(b);
        }

        /* KEY6: vol up */
        if (gpio_get_level(BUTTON_6_IO) == 0 && vol_val < 33 &&
            (now_ms - last_vol_ms) > VOL_DEBOUNCE_MS) {
            update_settings(freq, vol_val + 1, wpm, noise_level, NULL, NULL);
            last_vol_ms = now_ms;
        }
        /* KEY5: vol down */
        if (gpio_get_level(BUTTON_5_IO) == 0 && vol_val > 0 &&
            (now_ms - last_vol_ms) > VOL_DEBOUNCE_MS) {
            update_settings(freq, vol_val - 1, wpm, noise_level, NULL, NULL);
            last_vol_ms = now_ms;
        }

        /* KEY4: freq up */
        if (gpio_get_level(BUTTON_4_IO) == 0 && freq < 1200 &&
            (now_ms - last_freq_ms) > FREQ_DEBOUNCE_MS) {
            update_settings(freq + 50, vol_val, wpm, noise_level, NULL, NULL);
            last_freq_ms = now_ms;
        }
        /* KEY3: freq down */
        if (gpio_get_level(BUTTON_3_IO) == 0 && freq > 300 &&
            (now_ms - last_freq_ms) > FREQ_DEBOUNCE_MS) {
            update_settings(freq - 50, vol_val, wpm, noise_level, NULL, NULL);
            last_freq_ms = now_ms;
        }

        /* KEY1 or External Key: transmit tone, else silence. OR automated playback tone. */
        bool key1_now = (gpio_get_level(BUTTON_1_IO) == 0) || (gpio_get_level(EXT_KEY_IO) == 0);
        bool pb_tone_now = false;

        /* String Playback Logic (State Machine) */
        if (pb_active) {
            if (pb_blocks_left == 0) {
                uint32_t dot_ms = 1200 / wpm;
                uint32_t dot_blocks = dot_ms * I2S_SAMPLE_RATE / (256 * 1000);

                switch (pb_state) {
                    case PB_IDLE:
                        pb_str_idx = 0;
                        pb_state = PB_CHAR_GAP; // Start with a small gap
                        pb_blocks_left = 1; 
                        break;

                    case PB_TONE:
                        /* Finished a dot or dash. Every element is followed by 1 dot gap. */
                        pb_is_tone = false;
                        pb_blocks_left = dot_blocks;
                        pb_state = PB_ELEMENT_GAP;
                        break;

                    case PB_ELEMENT_GAP:
                        /* Finished gap after pulse. Check if character is done. */
                        if (pb_seq && *pb_seq != '\0') {
                            /* Next pulse in current character */
                            char pulse = *pb_seq++;
                            pb_is_tone = true;
                            pb_blocks_left = (pulse == '.') ? dot_blocks : dot_blocks * 3;
                            pb_state = PB_TONE;
                        } else {
                            /* Character done. Gap between characters is 3 dots total (already had 1). */
                            pb_is_tone = false;
                            pb_blocks_left = dot_blocks * 2;
                            pb_state = PB_CHAR_GAP;
                        }
                        break;

                    case PB_CHAR_GAP:
                    case PB_WORD_GAP:
                        /* Gap finished, try next character in string */
                        {
                            char c = pb_string[pb_str_idx];
                            if (c == '\0') {
                                pb_active = false;
                                pb_state = PB_IDLE;
                            } else {
                                pb_str_idx++;
                                if (c == ' ') {
                                    /* Already had some gap, word gap is 7 dots total. */
                                    pb_is_tone = false;
                                    pb_blocks_left = dot_blocks * 7;
                                    pb_state = PB_WORD_GAP;
                                } else {
                                    pb_seq = morse_logic_get_sequence(c);
                                    if (pb_seq && *pb_seq != '\0') {
                                        char pulse = *pb_seq++;
                                        pb_is_tone = true;
                                        pb_blocks_left = (pulse == '.') ? dot_blocks : dot_blocks * 3;
                                        pb_state = PB_TONE;
                                    } else {
                                        /* Skip unknown char, wait 1 dot */
                                        pb_blocks_left = dot_blocks;
                                        pb_state = PB_CHAR_GAP;
                                    }
                                }
                            }
                        }
                        break;
                }
            }
            if (pb_active) {
                if (pb_is_tone) pb_tone_now = true;
                if (pb_blocks_left > 0) pb_blocks_left--;
            }
        }

        if (key1_now != key1_pressed) {
            char c = morse_logic_handle_key(key1_now, now_ms);
            
            /* Broadcast current bit sequence for real-time visual feedback */
            char mb[64];
            snprintf(mb, sizeof(mb), "{\"morse\":\"%s\"}", morse_logic_get_current_bits());
            web_server_broadcast(mb);

            if (c) {
                ESP_LOGI(TAG, "Decoded: [%c] Score: %d", c, morse_logic_get_match_score());
                char b[64];
                snprintf(b, sizeof(b), "{\"char\":\"%c\",\"score\":%d}", c, morse_logic_get_match_score());
                web_server_broadcast(b);
            }
            key1_pressed = key1_now;
        }

        if (key1_now || pb_tone_now || noise_level > 0) {
            static uint32_t seed = 0x12345678;
            for (int i = 0; i < 256; i++) {
                /* White noise generation (LFSR-like) */
                seed = seed * 1103515245 + 12345;
                float raw_noise = ((float)((int32_t)(seed >> 16) - 32768) / 32768.0f);
                
                /* Apply Biquad Bandpass Filter */
                float filtered = noise_filter.b0 * raw_noise + noise_filter.b1 * noise_filter.x1 + noise_filter.b2 * noise_filter.x2
                                 - noise_filter.a1 * noise_filter.y1 - noise_filter.a2 * noise_filter.y2;
                noise_filter.x2 = noise_filter.x1;
                noise_filter.x1 = raw_noise;
                noise_filter.y2 = noise_filter.y1;
                noise_filter.y1 = filtered;

                int16_t noise = (int16_t)(filtered * 8000.0f * noise_level / 100.0f);
                
                int16_t s = 0;
                if (key1_now || pb_tone_now) {
                    s = sine_table[(int)phase & (SINE_TABLE_SIZE - 1)];
                    phase += phase_step;
                    if (phase >= SINE_TABLE_SIZE) phase -= SINE_TABLE_SIZE;
                }
                
                buf[i * 2]     = s + noise;
                buf[i * 2 + 1] = s + noise;
            }
        } else {
            memset(buf, 0, sizeof(buf));
            phase = 0.0f;
        }
        size_t written;
        i2s_channel_write(tx_chan, buf, sizeof(buf), &written, portMAX_DELAY);
    }
}
