#include "mcpwm_control.h"
#include "esp_log.h"
#include "driver/mcpwm.h"

static const char *TAG = "MCPWM";

/* ─── Unit selection ─── */
#define MCPWM_UNIT      MCPWM_UNIT_0
#define MCPWM_TIMER     MCPWM_TIMER_0

/* ─── Current active config ─── */
static InductionConfig s_cfg = {
    .enable          = false,
    .frequency_hz    = 40000,
    .duty_percent    = 45.0f,
    .dead_time_red_ns = 200,
    .dead_time_fed_ns = 200,
};

/* ─── Feedback simulation ─── */
static InductionFeedback s_fb = { };

/* ════════════════════════════════════════════
 *  Internal helpers
 * ════════════════════════════════════════════ */

static inline uint32_t ns_to_ticks(uint32_t ns) {
    uint32_t ticks = ns / NS_PER_TICK;
    if (ticks > 255) ticks = 255;
    return ticks;
}

/* ════════════════════════════════════════════
 *  Init  (Legacy MCPWM API)
 * ════════════════════════════════════════════ */

esp_err_t mcpwm_init(void) {
    ESP_LOGI(TAG, "Initialising MCPWM (legacy API) — %u Hz base", MCPWM_RESOLUTION_HZ);

    /* ── Assign GPIO pins ── */
    mcpwm_gpio_init(MCPWM_UNIT, MCPWM0A, GPIO_PWM_A);
    mcpwm_gpio_init(MCPWM_UNIT, MCPWM0B, GPIO_PWM_B);

    /* ── Base config ── */
    mcpwm_config_t cfg = {
        .frequency     = s_cfg.frequency_hz,
        .cmpr_a        = s_cfg.duty_percent,
        .cmpr_b        = 0.0f,                  // 0 = auto-complementary
        .duty_mode     = MCPWM_DUTY_MODE_0,
        .counter_mode  = MCPWM_UP_COUNTER,
    };
    ESP_ERROR_CHECK(mcpwm_init(MCPWM_UNIT, MCPWM_TIMER, &cfg));

    /* ── Dead time: ACTIVE_HIGH_COMPLIMENT_MODE
     *    gen A = PWM + RED  (delayed turn-on for high-side)
     *    gen B = NOT(PWM) + FED  (delayed turn-on for low-side)
     */
    ESP_ERROR_CHECK(mcpwm_deadtime_enable(
        MCPWM_UNIT, MCPWM_TIMER,
        MCPWM_ACTIVE_HIGH_COMPLIMENT_MODE,
        ns_to_ticks(s_cfg.dead_time_red_ns),
        ns_to_ticks(s_cfg.dead_time_fed_ns)
    ));

    /* ── Start with output disabled ── */
    mcpwm_stop(MCPWM_UNIT, MCPWM_TIMER);

    /* ── Master enable GPIO ── */
    gpio_config_t en_gpio = {
        .pin_bit_mask = BIT64(GPIO_ENABLE),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
    };
    gpio_config(&en_gpio);
    gpio_set_level(GPIO_ENABLE, 0);

    ESP_LOGI(TAG, "MCPWM ready  —  A:GPIO%d  B:GPIO%d  EN:GPIO%d  |  %u kHz  %.1f%%  RED:%u  FED:%u",
             GPIO_PWM_A, GPIO_PWM_B, GPIO_ENABLE,
             s_cfg.frequency_hz / 1000, s_cfg.duty_percent,
             s_cfg.dead_time_red_ns, s_cfg.dead_time_fed_ns);
    return ESP_OK;
}

/* ════════════════════════════════════════════
 *  Config apply
 * ════════════════════════════════════════════ */

esp_err_t mcpwm_apply_config(const InductionConfig &cfg) {
    mcpwm_set_frequency(cfg.frequency_hz);
    mcpwm_set_duty(cfg.duty_percent);
    mcpwm_set_dead_time(cfg.dead_time_red_ns, cfg.dead_time_fed_ns);
    mcpwm_set_enable(cfg.enable);

    s_cfg = cfg;
    ESP_LOGI(TAG, "Config: %ukHz  %.1f%%  RED:%uns  FED:%uns  %s",
             cfg.frequency_hz / 1000, cfg.duty_percent,
             cfg.dead_time_red_ns, cfg.dead_time_fed_ns,
             cfg.enable ? "ON" : "OFF");
    return ESP_OK;
}

esp_err_t mcpwm_get_config(InductionConfig &cfg) {
    cfg = s_cfg;
    return ESP_OK;
}

/* ════════════════════════════════════════════
 *  Setters
 * ════════════════════════════════════════════ */

esp_err_t mcpwm_set_frequency(uint32_t freq_hz) {
    if (freq_hz < PWM_FREQ_MIN || freq_hz > PWM_FREQ_MAX) {
        ESP_LOGW(TAG, "Frequency %u out of range", freq_hz);
        return ESP_ERR_INVALID_ARG;
    }
    mcpwm_set_frequency(MCPWM_UNIT, MCPWM_TIMER, freq_hz);
    s_cfg.frequency_hz = freq_hz;
    return ESP_OK;
}

esp_err_t mcpwm_set_duty(float duty_pct) {
    if (duty_pct < PWM_DUTY_MIN || duty_pct > PWM_DUTY_MAX) {
        ESP_LOGW(TAG, "Duty %.1f out of range", duty_pct);
        return ESP_ERR_INVALID_ARG;
    }
    mcpwm_set_duty(MCPWM_UNIT, MCPWM_TIMER, MCPWM_GEN_A, duty_pct);
    mcpwm_set_duty_type(MCPWM_UNIT, MCPWM_TIMER, MCPWM_GEN_A, MCPWM_DUTY_MODE_0);
    s_cfg.duty_percent = duty_pct;
    return ESP_OK;
}

esp_err_t mcpwm_set_dead_time(uint32_t red_ns, uint32_t fed_ns) {
    if (red_ns > DEADTIME_MAX || fed_ns > DEADTIME_MAX) {
        ESP_LOGW(TAG, "Dead time out of range");
        return ESP_ERR_INVALID_ARG;
    }

    // Disable then re-enable with new values
    mcpwm_deadtime_disable(MCPWM_UNIT, MCPWM_TIMER);
    ESP_ERROR_CHECK(mcpwm_deadtime_enable(
        MCPWM_UNIT, MCPWM_TIMER,
        MCPWM_ACTIVE_HIGH_COMPLIMENT_MODE,
        ns_to_ticks(red_ns),
        ns_to_ticks(fed_ns)
    ));

    s_cfg.dead_time_red_ns = red_ns;
    s_cfg.dead_time_fed_ns = fed_ns;
    return ESP_OK;
}

esp_err_t mcpwm_set_enable(bool en) {
    if (en) {
        mcpwm_start(MCPWM_UNIT, MCPWM_TIMER);
        gpio_set_level(GPIO_ENABLE, 1);
    } else {
        mcpwm_stop(MCPWM_UNIT, MCPWM_TIMER);
        gpio_set_level(GPIO_ENABLE, 0);
        gpio_set_level(GPIO_PWM_A, 0);
        gpio_set_level(GPIO_PWM_B, 0);
    }
    s_cfg.enable = en;
    return ESP_OK;
}

esp_err_t mcpwm_emergency_stop(void) {
    ESP_LOGW(TAG, "*** EMERGENCY STOP ***");

    mcpwm_stop(MCPWM_UNIT, MCPWM_TIMER);
    gpio_set_level(GPIO_ENABLE, 0);
    gpio_set_level(GPIO_PWM_A, 0);
    gpio_set_level(GPIO_PWM_B, 0);

    s_cfg.enable = false;
    s_cfg.duty_percent = 0.0f;
    return ESP_OK;
}

/* ════════════════════════════════════════════
 *  Feedback (simulated)
 * ════════════════════════════════════════════ */

void mcpwm_get_feedback(InductionFeedback &fb) {
    if (s_cfg.enable) {
        float freq_khz = s_cfg.frequency_hz / 1000.0f;
        float pct = s_cfg.duty_percent / 100.0f;
        s_fb.power_kw      = 15.0f * pct * (freq_khz / 40.0f);
        s_fb.voltage       = 220.0f + (s_fb.power_kw * 1.2f);
        s_fb.current_a     = (s_fb.power_kw * 1000.0f) / (s_fb.voltage + 1.0f);
        s_fb.temperature_c += (s_fb.power_kw * 0.002f) - 0.05f;
        if (s_fb.temperature_c < 25.0f)  s_fb.temperature_c = 25.0f;
        if (s_fb.temperature_c > 110.0f) s_fb.temperature_c = 110.0f;
    } else {
        s_fb.power_kw = 0.0f;
        s_fb.voltage  = 0.0f;
        s_fb.current_a = 0.0f;
        if (s_fb.temperature_c > 25.0f) s_fb.temperature_c -= 0.1f;
    }
    fb = s_fb;
}
