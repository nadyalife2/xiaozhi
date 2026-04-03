// =================================================================
// TEST_PINS v3 — Проверка микрофона и динамика
// Плата: Spotpear MUMA 1.54 / ESP32-1.54inch-AI-V2
//
// ПОЧЕМУ БЫЛИ НУЛИ:
//   При I2S_MODE_RX без TX и fixed_mclk=0
//   ESP32-S3 НЕ выводит MCLK на GPIO16.
//   ES8311 без MCLK = ADC молчит = DMA нули.
//
// РЕШЕНИЕ: full-duplex TX|RX + fixed_mclk = SAMPLE_RATE*256
//   Тогда MCLK генерируется всегда.
// =================================================================

#include <Arduino.h>
#include <Wire.h>
#include <driver/i2s.h>
#include <math.h>

// --- ПИНЫ ---
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
#define TEST_TONE_HZ      1000
#define BLOCK_SIZE         256
#define MIC_SILENCE_THR    100

// =================================================================
// ES8311
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

  es_wr(0x00, 0x1F); delay(20);
  es_wr(0x00, 0x80); delay(20);

  // Тактирование: MCLK внешний (GPIO16)
  es_wr(0x01, 0x30);
  es_wr(0x02, 0x10);
  es_wr(0x03, 0x10);
  es_wr(0x04, 0x10);
  es_wr(0x05, 0x00);
  es_wr(0x06, 0x03);
  delay(10);

  // AIF: I2S, 16 бит
  es_wr(0x0B, 0x00);
  es_wr(0x0C, 0x00);
  delay(5);

  // Power
  es_wr(0x0D, 0x01); delay(10);
  es_wr(0x0E, 0x02);
  es_wr(0x0F, 0x44); delay(20);

  // MIC PGA +18dB
  es_wr(0x10, 0x28);
  es_wr(0x11, 0x28);
  delay(10);

  // ADC
  es_wr(0x15, 0x00);
  es_wr(0x16, 0x00);
  es_wr(0x17, 0x88);
  delay(10);

  // DAC
  es_wr(0x32, 0xBF);
  es_wr(0x37, 0x08);
  delay(10);

  Serial.printf("  [ES8311] MIC_PGA=0x%02X ADC_PWR=0x%02X ADC=0x%02X CLK=0x%02X\n",
                es_rd(0x10), es_rd(0x17), es_rd(0x16), es_rd(0x02));
}

// =================================================================
// I2S full-duplex (TX+RX одновременно)
// fixed_mclk гарантирует вывод MCLK на GPIO16
// =================================================================
void i2s_install_duplex() {
  i2s_config_t cfg = {
    .mode             = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
    .sample_rate      = SAMPLE_RATE,
    .bits_per_sample  = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format   = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count    = 8,
    .dma_buf_len      = BLOCK_SIZE,
    .use_apll         = true,
    .tx_desc_auto_clear = true,
    .fixed_mclk       = MCLK_FREQ   // ← МУСТ БЫТЬ! MCLK всегда активен
  };
  i2s_pin_config_t pins = {
    .mck_io_num   = I2S_MCLK,
    .bck_io_num   = I2S_BCLK,
    .ws_io_num    = I2S_LRC,
    .data_out_num = I2S_DOUT,
    .data_in_num  = I2S_DIN
  };
  esp_err_t err = i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
  if (err != ESP_OK) Serial.printf("  [I2S] install err: %d\n", err);
  i2s_set_pin(I2S_NUM_0, &pins);
  i2s_zero_dma_buffer(I2S_NUM_0);
  delay(100);
}

// =================================================================
// ТЕСТ ДИНАМИКА — тон 1кГц, 3 секунды
// =================================================================
void testSpeaker() {
  Serial.println("\n=============================");
  Serial.println("ТЕСТ ДИНАМИКА (тон 1кГц, 3с)");
  Serial.println("=============================");

  i2s_install_duplex();
  digitalWrite(PA_ENABLE, HIGH);
  delay(100);

  int16_t buf[BLOCK_SIZE * 2];
  size_t written = 0;
  int phase = 0;
  const int PERIOD = SAMPLE_RATE / TEST_TONE_HZ;
  const int TOTAL  = SAMPLE_RATE * 3;

  Serial.println("  ▶ Воспроизвожу тон 1кГц...");
  for (int i = 0; i < TOTAL; i += BLOCK_SIZE) {
    for (int j = 0; j < BLOCK_SIZE; j++) {
      int16_t s = (int16_t)(sinf(2.0f * M_PI * phase / PERIOD) * 8000);
      buf[j * 2]     = s;
      buf[j * 2 + 1] = s;
      phase = (phase + 1) % PERIOD;
    }
    i2s_write(I2S_NUM_0, buf, sizeof(buf), &written, portMAX_DELAY);
  }
  Serial.println("  ✅ Тон отправлен!");

  i2s_driver_uninstall(I2S_NUM_0);
  digitalWrite(PA_ENABLE, LOW);
  delay(200);
}

// =================================================================
// ТЕСТ МИКРОФОНА — full-duplex, 5 секунд
// TX шлёт тишину, RX читает микрофон
// =================================================================
void testMicrophone() {
  Serial.println("\n================================");
  Serial.println("ТЕСТ МИКРОФОНА (full-duplex, 5с)");
  Serial.println("================================");
  Serial.println("  ГОВОРИ В МИКРОФОН!\n");

  es_wr(0x17, 0x88);
  es_wr(0x16, 0x00);
  delay(20);

  i2s_install_duplex();

  int16_t buf_rx[BLOCK_SIZE * 2];
  int16_t buf_tx[BLOCK_SIZE * 2];
  memset(buf_tx, 0, sizeof(buf_tx));

  size_t bytes_read, written;
  unsigned long start = millis();
  int32_t maxPeak = 0;
  bool signalDetected = false;
  int debugFrames = 0;

  while (millis() - start < 5000) {
    i2s_write(I2S_NUM_0, buf_tx, sizeof(buf_tx), &written, 0);
    i2s_read(I2S_NUM_0, buf_rx, sizeof(buf_rx), &bytes_read, pdMS_TO_TICKS(50));
    int samples = bytes_read / 2;
    if (samples == 0) continue;

    if (debugFrames < 3) {
      Serial.printf("  RAW[%d]: ", debugFrames);
      for (int i = 0; i < min(8, samples); i++)
        Serial.printf("%6d ", buf_rx[i]);
      Serial.println();
      debugFrames++;
    }

    int64_t sum = 0;
    int16_t peak = 0;
    for (int i = 0; i < samples; i += 2) {
      int16_t v = abs(buf_rx[i]);
      sum += (int32_t)v * v;
      if (v > peak) peak = v;
    }
    int rms = (int)sqrtf((float)sum / (samples / 2));
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

  Serial.println();
  if (signalDetected) {
    Serial.printf("  ✅ Микрофон РАБОТАЕТ! MaxPeak=%d\n", (int)maxPeak);
  } else if (maxPeak > 0) {
    Serial.printf("  ⚠️  Слабый сигнал (MaxPeak=%d). Говори громче!\n", (int)maxPeak);
  } else {
    Serial.println("  ❌ Сигнал = 0.");
    Serial.printf("     • GPIO%d (DIN)  → ES8311 ASDOUT?\n", I2S_DIN);
    Serial.printf("     • GPIO%d (MCLK) → ES8311 MCLK?\n",   I2S_MCLK);
    Serial.println("     • Питание ES8311: DVDD=1.8V, AVDD=3.3V?");
    Serial.println("     • Микрофоны подпаяны к MICIP1/MICIN1?");
  }
}

// =================================================================
// SETUP
// =================================================================
void setup() {
  pinMode(45, OUTPUT); digitalWrite(45, LOW);
  delay(100);

  Serial.begin(115200);
  delay(2000);

  Serial.println();
  Serial.println("#####################################");
  Serial.println("###  TEST_PINS v3                 ###");
  Serial.println("###  Spotpear MUMA 1.54           ###");
  Serial.println("###  FIX: full-duplex+fixed_mclk  ###");
  Serial.println("#####################################");
  Serial.printf("\nMCLK = %d Hz\n\n", MCLK_FREQ);

  pinMode(PA_ENABLE, OUTPUT);
  digitalWrite(PA_ENABLE, LOW);

  Serial.println("[1/3] Инициализация ES8311...");
  initES8311();
  delay(300);

  Serial.println("\n[2/3] Тест динамика...");
  testSpeaker();
  delay(500);

  Serial.println("\n[3/3] Тест микрофона (5с, ГОВОРИ!)...");
  testMicrophone();

  Serial.println("\n=== ТЕСТ ЗАВЕРШЁН ===");
  Serial.println("Для повтора — нажми RST.");
}

void loop() { delay(1000); }
