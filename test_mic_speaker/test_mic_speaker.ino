// =================================================================
// TEST_PINS v4 — Проверка микрофона и динамика
// Плата: Spotpear MUMA 1.54 / ESP32-1.54inch-AI-V2
//
// ИСПРАВЛЕНИЯ v4 (по рабочей прошивке MELVIN):
//   1. channel_format = ONLY_LEFT (моно!), не RIGHT_LEFT
//   2. Регистры ES8311 точно из рабочего main.cpp
//   3. Сброс ES8311: 0x01=0x1F → 0x00, не 0x80
// =================================================================

#include <Arduino.h>
#include <Wire.h>
#include <driver/i2s.h>
#include <math.h>

// --- ПИНЫ (Spotpear MUMA 1.54) ---
#define I2C_SDA     15
#define I2C_SCL     14
#define I2S_MCLK    16
#define I2S_BCLK     9
#define I2S_LRC     45
#define I2S_DOUT     8
#define I2S_DIN     10
#define PA_ENABLE   46

#define SAMPLE_RATE      16000
#define MCLK_FREQ        (SAMPLE_RATE * 256)   // 4 096 000 Hz
#define BLOCK_SIZE         512
#define MIC_SILENCE_THR    100

// =================================================================
// ES8311 I2C
// =================================================================
static void es_wr(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(0x18);
  Wire.write(reg); Wire.write(val);
  Wire.endTransmission();
}
static uint8_t es_rd(uint8_t reg) {
  Wire.beginTransmission(0x18);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)0x18, (uint8_t)1);
  return Wire.available() ? Wire.read() : 0xFF;
}

void initES8311() {
  Wire.begin(I2C_SDA, I2C_SCL);
  delay(50);

  Wire.beginTransmission(0x18);
  if (Wire.endTransmission() != 0) {
    Serial.println("  [ES8311] ❌ Кодек не отвечает!");
    return;
  }
  Serial.printf("  [ES8311] ✅ Найден на 0x18, CHIP_ID=0x%02X\n", es_rd(0xFD));

  // Сброс — точно как в рабочей прошивке MELVIN
  es_wr(0x01, 0x1F); delay(20);
  es_wr(0x01, 0x00); delay(20);  // <-- было 0x80, теперь 0x00!

  // Тактирование
  es_wr(0x02, 0x00);
  es_wr(0x03, 0x10);
  delay(10);

  // AIF: I2S, 16 бит
  es_wr(0x0B, 0x00);
  es_wr(0x0C, 0x00);

  // Power / System — из рабочего кода
  es_wr(0x10, 0x00);
  es_wr(0x11, 0xFC);
  delay(10);

  es_wr(0x00, 0x80);  // chip enable
  delay(10);

  es_wr(0x0D, 0x01);  // DAC timing
  es_wr(0x0E, 0x02);  // ADC timing
  delay(10);

  // MIC PGA +18dB (0x28)
  es_wr(0x12, 0x28);
  es_wr(0x13, 0x06);  // ADC volume (усиление)

  // ADC включить
  es_wr(0x16, 0x11);
  es_wr(0x17, 0x11);
  delay(10);

  // DAC включить
  es_wr(0x14, 0x1A);
  es_wr(0x15, 0x1A);
  delay(10);

  Serial.printf("  Reg[0x00]=0x%02X [0x10]=0x%02X [0x16]=0x%02X [0x17]=0x%02X\n",
                es_rd(0x00), es_rd(0x10), es_rd(0x16), es_rd(0x17));
  Serial.printf("  Reg[0x02]=0x%02X [0x0E]=0x%02X [0x12]=0x%02X [0x13]=0x%02X\n",
                es_rd(0x02), es_rd(0x0E), es_rd(0x12), es_rd(0x13));
}

// =================================================================
// I2S — ТЕСТ ДИНАМИКА (TX only, моно)
// =================================================================
void testSpeaker() {
  Serial.println();
  Serial.println("=============================" );
  Serial.println("ТЕСТ ДИНАМИКА (тон 1кГц, 3с)");
  Serial.println("=============================");

  i2s_config_t cfg_tx = {
    .mode             = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate      = SAMPLE_RATE,
    .bits_per_sample  = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format   = I2S_CHANNEL_FMT_ONLY_LEFT,  // моно!
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count    = 8,
    .dma_buf_len      = BLOCK_SIZE,
    .use_apll         = true,
    .tx_desc_auto_clear = true,
    .fixed_mclk       = MCLK_FREQ
  };
  i2s_pin_config_t pins = {
    .mck_io_num   = I2S_MCLK,
    .bck_io_num   = I2S_BCLK,
    .ws_io_num    = I2S_LRC,
    .data_out_num = I2S_DOUT,
    .data_in_num  = I2S_PIN_NO_CHANGE
  };
  i2s_driver_install(I2S_NUM_0, &cfg_tx, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pins);
  i2s_zero_dma_buffer(I2S_NUM_0);
  delay(100);

  digitalWrite(PA_ENABLE, HIGH);
  delay(50);

  int16_t buf[BLOCK_SIZE];
  size_t written = 0;
  int phase = 0;
  const int PERIOD = SAMPLE_RATE / 1000;  // 16 семплов
  const int TOTAL  = SAMPLE_RATE * 3;

  Serial.println("  ▶ Воспроизвожу тон 1кГц 3 секунды...");
  for (int i = 0; i < TOTAL; i += BLOCK_SIZE) {
    for (int j = 0; j < BLOCK_SIZE; j++) {
      buf[j] = (int16_t)(sinf(2.0f * M_PI * phase / PERIOD) * 16000);
      phase = (phase + 1) % PERIOD;
    }
    i2s_write(I2S_NUM_0, buf, sizeof(buf), &written, portMAX_DELAY);
  }
  Serial.println("  ✅ Тон отправлен! Слышишь звук?");

  i2s_driver_uninstall(I2S_NUM_0);
  digitalWrite(PA_ENABLE, LOW);
  delay(200);
}

// =================================================================
// I2S — ТЕСТ МИКРОФОНА (RX only, моно — как в рабочей прошивке!)
// =================================================================
void testMicrophone() {
  Serial.println();
  Serial.println("================================");
  Serial.println("ТЕСТ МИКРОФОНА (RX моно, 5с)");
  Serial.println("================================");
  Serial.println("  channel = ONLY_LEFT (моно)");
  Serial.println("  fixed_mclk = 4096000");
  Serial.println("  ГОВОРИ В МИКРОФОН!");
  Serial.println();

  // Убедимся что ADC активен
  es_wr(0x16, 0x11);
  es_wr(0x17, 0x11);
  delay(20);

  i2s_config_t cfg_rx = {
    .mode             = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate      = SAMPLE_RATE,
    .bits_per_sample  = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format   = I2S_CHANNEL_FMT_ONLY_LEFT,  // МОНО! Ключевое исправление
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count    = 8,
    .dma_buf_len      = BLOCK_SIZE,
    .use_apll         = true,
    .tx_desc_auto_clear = true,
    .fixed_mclk       = MCLK_FREQ   // MCLK всегда активен!
  };
  i2s_pin_config_t pins = {
    .mck_io_num   = I2S_MCLK,
    .bck_io_num   = I2S_BCLK,
    .ws_io_num    = I2S_LRC,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num  = I2S_DIN
  };
  i2s_driver_install(I2S_NUM_0, &cfg_rx, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pins);
  i2s_zero_dma_buffer(I2S_NUM_0);
  delay(200);  // ES8311 ADC нужно время на старт

  int16_t buf[BLOCK_SIZE];
  size_t bytes_read;
  unsigned long start = millis();
  int32_t maxPeak = 0;
  bool signalDetected = false;
  int debugFrames = 0;

  while (millis() - start < 5000) {
    i2s_read(I2S_NUM_0, buf, sizeof(buf), &bytes_read, pdMS_TO_TICKS(100));
    int samples = bytes_read / 2;
    if (samples == 0) { Serial.println("  [!] bytes_read=0!"); continue; }

    // Дамп первых кадров — смотрим что реально приходит
    if (debugFrames < 4) {
      Serial.printf("  RAW[%d] (%d bytes): ", debugFrames, bytes_read);
      for (int i = 0; i < min(8, samples); i++)
        Serial.printf("%6d ", buf[i]);
      Serial.println();
      debugFrames++;
    }

    int64_t sum = 0;
    int16_t peak = 0;
    for (int i = 0; i < samples; i++) {
      int16_t v = abs(buf[i]);
      sum += (int32_t)v * v;
      if (v > peak) peak = v;
    }
    int rms = (int)sqrtf((float)sum / samples);
    if (peak > maxPeak) maxPeak = peak;
    if (rms > MIC_SILENCE_THR) signalDetected = true;

    int bars = (int)map(constrain(rms, 0, 8000), 0, 8000, 0, 30);
    char bar[32] = {};
    for (int i = 0; i < 30; i++) bar[i] = (i < bars) ? '|' : ' ';
    Serial.printf("  RMS:%5d  Peak:%5d  [%s]%s\n",
                  rms, peak, bar,
                  rms > MIC_SILENCE_THR ? " ← СИГНАЛ!" : "");
  }

  i2s_driver_uninstall(I2S_NUM_0);
  delay(100);

  Serial.println();
  if (signalDetected) {
    Serial.printf("  ✅ МИКРОФОН РАБОТАЕТ! MaxPeak=%d\n", (int)maxPeak);
  } else if (maxPeak > 10) {
    Serial.printf("  ⚠️  Слабый сигнал (MaxPeak=%d). Говори ГРОМЧЕ!\n", (int)maxPeak);
  } else {
    Serial.println("  ❌ Сигнал = 0. Диагностика:");
    Serial.println("     Проверь регистры ES8311 выше (0x16, 0x17 должны быть 0x11)");
    Serial.println("     Проверь пайку GPIO10 → ES8311 pin7 (ASDOUT)");
    Serial.println("     Проверь питание: DVDD=1.8V, AVDD=3.3V");
    Serial.println("     Проверь микрофоны на MICIP1/MICIN1");
  }
}

void setup() {
  pinMode(PA_ENABLE, OUTPUT);
  digitalWrite(PA_ENABLE, LOW);

  Serial.begin(115200);
  delay(2000);

  Serial.println();
  Serial.println("#########################################");
  Serial.println("###  TEST_PINS v4 (моно + верн. реги)  ###");
  Serial.println("###  Spotpear MUMA 1.54               ###");
  Serial.println("#########################################");
  Serial.printf("  MCLK = %d Hz\n\n", MCLK_FREQ);

  Serial.println("[1/3] Инициализация ES8311...");
  initES8311();
  delay(300);

  Serial.println("\n[2/3] Тест динамика...");
  testSpeaker();
  delay(300);

  Serial.println("\n[3/3] Тест микрофона (ГОВОРИ 5 секунд!)...");
  testMicrophone();

  Serial.println("\n=== ТЕСТ ЗАВЕРШЁН ===");
  Serial.println("Для повтора — нажми RST.");
}

void loop() { delay(1000); }
