#include "encoders.h"
#include <Arduino.h>
#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "detent_counter.h"
#include "pins.h"
#include "quad_decoder.h"

static constexpr int kLimit = 10000;       // driver accumulates past this (accum_count)
static constexpr uint32_t kGlitchNs = 1000;  // ignore pulses < 1 us (EC11 bounce is longer;
                                             // x4 level gating cancels bounce edges)

struct Knob {
  int pinA, pinB;
  pcnt_unit_handle_t unit = nullptr;
  DetentCounter detents{4};
  QuadDecoder poll{1, 1};  // fallback only
};

static Knob knobs[2] = {{ENC1_PHASE_A_PIN, ENC1_PHASE_B_PIN}, {ENC2_PHASE_A_PIN, ENC2_PHASE_B_PIN}};

// Direction matches QuadDecoder (bench-verified): CW = AB 11 -> 01 -> 00 -> 10 counts up.
static bool startPcnt(Knob &k) {
  pcnt_unit_config_t ucfg = {};
  ucfg.low_limit = -kLimit;
  ucfg.high_limit = kLimit;
  ucfg.flags.accum_count = 1;
  if (pcnt_new_unit(&ucfg, &k.unit) != ESP_OK) return false;

  pcnt_glitch_filter_config_t fcfg = {};
  fcfg.max_glitch_ns = kGlitchNs;
  pcnt_chan_config_t acfg = {};
  acfg.edge_gpio_num = k.pinA;
  acfg.level_gpio_num = k.pinB;
  pcnt_chan_config_t bcfg = {};
  bcfg.edge_gpio_num = k.pinB;
  bcfg.level_gpio_num = k.pinA;
  pcnt_channel_handle_t a = nullptr, b = nullptr;
  bool ok = pcnt_unit_set_glitch_filter(k.unit, &fcfg) == ESP_OK &&
            pcnt_new_channel(k.unit, &acfg, &a) == ESP_OK &&
            pcnt_new_channel(k.unit, &bcfg, &b) == ESP_OK &&
            pcnt_channel_set_edge_action(a, PCNT_CHANNEL_EDGE_ACTION_DECREASE,
                                         PCNT_CHANNEL_EDGE_ACTION_INCREASE) == ESP_OK &&
            pcnt_channel_set_level_action(a, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                          PCNT_CHANNEL_LEVEL_ACTION_INVERSE) == ESP_OK &&
            pcnt_channel_set_edge_action(b, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                         PCNT_CHANNEL_EDGE_ACTION_DECREASE) == ESP_OK &&
            pcnt_channel_set_level_action(b, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                          PCNT_CHANNEL_LEVEL_ACTION_INVERSE) == ESP_OK &&
            pcnt_unit_add_watch_point(k.unit, kLimit) == ESP_OK &&
            pcnt_unit_add_watch_point(k.unit, -kLimit) == ESP_OK &&
            pcnt_unit_enable(k.unit) == ESP_OK && pcnt_unit_clear_count(k.unit) == ESP_OK &&
            pcnt_unit_start(k.unit) == ESP_OK;
  if (!ok) {
    pcnt_unit_disable(k.unit);  // best-effort cleanup; falls back to polling
    if (a) pcnt_del_channel(a);
    if (b) pcnt_del_channel(b);
    pcnt_del_unit(k.unit);
    k.unit = nullptr;
  }
  return ok;
}

void encodersBegin() {
  for (Knob &k : knobs) {
    pinMode(k.pinA, INPUT_PULLUP);
    pinMode(k.pinB, INPUT_PULLUP);
    if (!startPcnt(k)) {
      Serial.printf("PCNT setup failed on GPIO%d/%d, polling instead\n", k.pinA, k.pinB);
      k.poll = QuadDecoder(digitalRead(k.pinA), digitalRead(k.pinB));
    }
    // Keep the internal pull-ups on whatever the PCNT driver configured
    gpio_pullup_en(static_cast<gpio_num_t>(k.pinA));
    gpio_pullup_en(static_cast<gpio_num_t>(k.pinB));
  }
}

static int readKnob(Knob &k) {
  if (!k.unit) return k.poll.step(digitalRead(k.pinA), digitalRead(k.pinB));
  int count = 0;
  if (pcnt_unit_get_count(k.unit, &count) != ESP_OK) return 0;
  return k.detents.update(count);
}

void encodersRead(int &knob1, int &knob2) {
  knob1 = readKnob(knobs[0]);
  knob2 = readKnob(knobs[1]);
}
