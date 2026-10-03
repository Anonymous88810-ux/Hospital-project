// MAX30102 simule (I2C, adresse 0x57) - compatible bibliotheque SparkFun MAX3010x
#include "wokwi-api.h"
#include <stdlib.h>

#define FIFO_SIZE 32

typedef struct {
  uint8_t  regs[256];
  uint8_t  reg_ptr;
  bool     first_write;
  uint8_t  wr_ptr, rd_ptr;
  uint32_t fifo[FIFO_SIZE][3];   // rouge, infrarouge, vert
  uint8_t  byte_idx;
  float    phase;
  uint32_t seed;
  uint32_t attr_bpm, attr_spo2, attr_finger;
} chip_state_t;

// Forme d'onde d'une pulsation (0..1) : montee rapide, descente, petite bosse dicrote
static float pulse_shape(float p) {
  if (p < 0.15f) {
    float x = p / 0.15f;
    return 1.0f - (1.0f - x) * (1.0f - x);
  }
  float x = (p - 0.15f) / 0.85f;
  float v = (1.0f - x) * (1.0f - x);
  float d = x - 0.30f;
  if (d < 0) d = -d;
  if (d < 0.10f) v += 0.12f * (1.0f - d / 0.10f);
  return v;
}

static int active_leds(chip_state_t *c) {
  uint8_t mode = c->regs[0x09] & 0x07;
  if (mode == 2) return 2;
  if (mode == 3 || mode == 7) return 3;
  return 1;
}

static void reset_chip(chip_state_t *c) {
  for (int i = 0; i < 256; i++) c->regs[i] = 0;
  c->wr_ptr = 0;
  c->rd_ptr = 0;
  c->byte_idx = 0;
}

// Appelee 100 fois par seconde : ajoute un echantillon dans la FIFO
static void on_timer(void *user_data) {
  chip_state_t *c = (chip_state_t *)user_data;

  float bpm  = (float)attr_read(c->attr_bpm);
  float spo2 = (float)attr_read(c->attr_spo2);
  bool finger = attr_read(c->attr_finger) != 0;

  c->phase += bpm / 60.0f * 0.01f;
  if (c->phase >= 1.0f) c->phase -= 1.0f;

  c->seed = c->seed * 1103515245u + 12345u;
  int noise = (int)((c->seed >> 16) & 7) - 3;

  uint32_t ir, red;
  if (!finger) {
    ir = 3000 + noise;
    red = 2500 + noise;
  } else {
    float w = pulse_shape(c->phase) - 0.3f;
    float R = (110.0f - spo2) / 25.0f;          // SpO2 = 110 - 25*R
    float dc = 100000.0f, ac = 400.0f;
    ir  = (uint32_t)(dc + ac * w + noise);
    red = (uint32_t)(dc + ac * R * w + noise);
  }

  c->fifo[c->wr_ptr][0] = red & 0x3FFFF;
  c->fifo[c->wr_ptr][1] = ir & 0x3FFFF;
  c->fifo[c->wr_ptr][2] = 0;
  c->wr_ptr = (c->wr_ptr + 1) & (FIFO_SIZE - 1);
  if (c->wr_ptr == c->rd_ptr) c->rd_ptr = (c->rd_ptr + 1) & (FIFO_SIZE - 1);  // FIFO pleine : on perd le plus ancien
}

static bool on_i2c_connect(void *user_data, uint32_t address, bool read) {
  chip_state_t *c = (chip_state_t *)user_data;
  if (!read) c->first_write = true;
  return true;
}

static uint8_t on_i2c_read(void *user_data) {
  chip_state_t *c = (chip_state_t *)user_data;
  uint8_t r = c->reg_ptr;

  if (r == 0x07) {                               // donnees FIFO (le pointeur de registre ne bouge pas)
    int leds = active_leds(c);
    int ch = c->byte_idx / 3, k = c->byte_idx % 3;
    uint32_t v = c->fifo[c->rd_ptr][ch];
    uint8_t out = (k == 0) ? ((v >> 16) & 0x03) : (k == 1) ? ((v >> 8) & 0xFF) : (v & 0xFF);
    c->byte_idx++;
    if (c->byte_idx >= leds * 3) {
      c->byte_idx = 0;
      c->rd_ptr = (c->rd_ptr + 1) & (FIFO_SIZE - 1);
    }
    return out;
  }

  uint8_t out;
  if (r == 0x00)      out = (c->wr_ptr != c->rd_ptr) ? 0x40 : 0x00;   // etat interruption
  else if (r == 0x04) out = c->wr_ptr;
  else if (r == 0x05) out = 0;
  else if (r == 0x06) out = c->rd_ptr;
  else if (r == 0x1F) out = 25;                                       // temperature (entier)
  else if (r == 0x20) out = 0;
  else if (r == 0xFE) out = 0x00;                                     // revision
  else if (r == 0xFF) out = 0x15;                                     // identifiant (attendu par la bibliotheque)
  else                out = c->regs[r];
  c->reg_ptr++;
  return out;
}

static bool on_i2c_write(void *user_data, uint8_t data) {
  chip_state_t *c = (chip_state_t *)user_data;

  if (c->first_write) {                           // premier octet = numero de registre
    c->reg_ptr = data;
    c->first_write = false;
    return true;
  }

  uint8_t r = c->reg_ptr;
  if (r == 0x09 && (data & 0x40)) {               // reinitialisation logicielle
    reset_chip(c);
    return true;
  }
  if (r == 0x04) { c->wr_ptr = data & (FIFO_SIZE - 1); c->byte_idx = 0; }
  else if (r == 0x06) { c->rd_ptr = data & (FIFO_SIZE - 1); c->byte_idx = 0; }
  else if (r != 0x07) c->regs[r] = data;
  c->reg_ptr++;
  return true;
}

static void on_i2c_disconnect(void *user_data) {}

void chip_init(void) {
  chip_state_t *chip = (chip_state_t *)calloc(1, sizeof(chip_state_t));
  chip->seed = 12345;
  chip->attr_bpm    = attr_init("bpm", 72);
  chip->attr_spo2   = attr_init("spo2", 97);
  chip->attr_finger = attr_init("finger", 1);

  const i2c_config_t i2c_config = {
    .address = 0x57,
    .scl = pin_init("SCL", INPUT),
    .sda = pin_init("SDA", INPUT),
    .connect = on_i2c_connect,
    .read = on_i2c_read,
    .write = on_i2c_write,
    .disconnect = on_i2c_disconnect,
    .user_data = chip,
  };
  i2c_init(&i2c_config);

  const timer_config_t timer_config = {
    .callback = on_timer,
    .user_data = chip,
  };
  timer_t timer = timer_init(&timer_config);
  timer_start(timer, 10000, true);               // 10 ms = 100 echantillons/s
}
