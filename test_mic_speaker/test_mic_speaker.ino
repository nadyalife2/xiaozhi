/**
 * TEST: Микрофон + Динамик (I2S loopback)
 * ESP32-S3 — проверка пинов I2S вход (микрофон) и выход (динамик)
 *
 * Что делает:
 *   1. Читает аудио с микрофона через I2S
 *   2. Измеряет уровень сигнала (громкость)
 *   3. Воспроизводит синусоидальный тон через динамик
 *   4. Выводит всё в Serial Monitor (115200 baud)
 *
 * ⚙️ НАСТРОЙ ПИНЫ ПОД СВОЮ ПЛАТУ:
 */

// ---- ПИНЫ МИКРОФОНА (I2S вход, INMP441 / SPM1423 или подобный) ----
#define MIC_WS    42   // LRCK (Left/Right Clock)
#define MIC_SCK   41   // BCLK (Bit Clock)
#define MIC_SD    2    // DOUT (Data Out микрофона → Data In ESP32)

// ---- ПИНЫ ДИНАМИКА (I2S выход, MAX98357 / NS4168 или подобный) ----
#define SPK_WS    15   // LRCK
#define SPK_BCK   14   // BCLK
#define SPK_DOUT  13   // DIN (Data Out ESP32 → Data In усилителя)

// ---- ПАРАМЕТРЫ АУДИО ----
#define SAMPLE_RATE     16000
#define BUFFER_SIZE     512
#define TEST_TONE_HZ    1000   // Тон для теста динамика (Гц)

#include <driver/i2s.h>
#include <math.h>

// I2S порты
#define I2S_MIC_PORT  I2S_NUM_0
#define I2S_SPK_PORT  I2S_NUM_1

int16_t mic_buffer[BUFFER_SIZE];
int16_t spk_buffer[BUFFER_SIZE];
size_t bytes_read, bytes_written;

// Фаза для синуса
float phase = 0.0f;
const float phase_inc = 2.0f * M_PI * TEST_TONE_HZ / SAMPLE_RATE;

// ======================================================
void setup_mic_i2s() {
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = BUFFER_SIZE,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };
  i2s_pin_config_t pins = {
    .mck_io_num = I2S_PIN_NO_CHANGE,
    .bck_io_num = MIC_SCK,
    .ws_io_num  = MIC_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num  = MIC_SD
  };
  esp_err_t err = i2s_driver_install(I2S_MIC_PORT, &cfg, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("[MIC] i2s_driver_install FAILED: %d\n", err);
  } else {
    Serial.println("[MIC] Driver OK");
  }
  err = i2s_set_pin(I2S_MIC_PORT, &pins);
  if (err != ESP_OK) {
    Serial.printf("[MIC] i2s_set_pin FAILED: %d\n", err);
  } else {
    Serial.println("[MIC] Pins set OK");
  }
}

void setup_spk_i2s() {
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = BUFFER_SIZE,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };
  i2s_pin_config_t pins = {
    .mck_io_num = I2S_PIN_NO_CHANGE,
    .bck_io_num = SPK_BCK,
    .ws_io_num  = SPK_WS,
    .data_out_num = SPK_DOUT,
    .data_in_num  = I2S_PIN_NO_CHANGE
  };
  esp_err_t err = i2s_driver_install(I2S_SPK_PORT, &cfg, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("[SPK] i2s_driver_install FAILED: %d\n", err);
  } else {
    Serial.println("[SPK] Driver OK");
  }
  err = i2s_set_pin(I2S_SPK_PORT, &pins);
  if (err != ESP_OK) {
    Serial.printf("[SPK] i2s_set_pin FAILED: %d\n", err);
  } else {
    Serial.println("[SPK] Pins set OK");
  }
}

// ======================================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n============================");
  Serial.println("  ESP32-S3 MIC + SPK TEST");
  Serial.println("============================");
  Serial.printf("Mic  WS=%d SCK=%d SD=%d\n", MIC_WS, MIC_SCK, MIC_SD);
  Serial.printf("Spk  WS=%d BCK=%d DOUT=%d\n", SPK_WS, SPK_BCK, SPK_DOUT);
  Serial.println();

  setup_mic_i2s();
  setup_spk_i2s();

  Serial.println("\n[START] Генерация тона 1kHz → динамик");
  Serial.println("[START] Чтение уровня → микрофон");
  Serial.println("-----------------------------------");
}

// ======================================================
void loop() {
  // --- Генерация тона (синус 1кГц) → динамик ---
  for (int i = 0; i < BUFFER_SIZE; i++) {
    int16_t sample = (int16_t)(sinf(phase) * 20000.0f);
    spk_buffer[i] = sample;
    phase += phase_inc;
    if (phase >= 2.0f * M_PI) phase -= 2.0f * M_PI;
  }
  i2s_write(I2S_SPK_PORT, spk_buffer, BUFFER_SIZE * sizeof(int16_t), &bytes_written, portMAX_DELAY);

  // --- Чтение с микрофона ---
  i2s_read(I2S_MIC_PORT, mic_buffer, BUFFER_SIZE * sizeof(int16_t), &bytes_read, 100);

  // Вычислить RMS (уровень) микрофона
  int samples_read = bytes_read / sizeof(int16_t);
  if (samples_read > 0) {
    long long sum = 0;
    int16_t peak = 0;
    for (int i = 0; i < samples_read; i++) {
      sum += (long long)mic_buffer[i] * mic_buffer[i];
      if (abs(mic_buffer[i]) > peak) peak = abs(mic_buffer[i]);
    }
    float rms = sqrtf((float)sum / samples_read);

    // Визуальный индикатор уровня
    int bars = (int)(rms / 800.0f);
    if (bars > 40) bars = 40;
    char bar[41] = {};
    for (int i = 0; i < bars; i++) bar[i] = '|';

    Serial.printf("MIC RMS: %6.0f  Peak: %6d  [%-40s]\n", rms, peak, bar);

    // Диагностика
    if (rms < 50) {
      Serial.println("  ⚠️  Тишина! Микрофон не отвечает — проверь пины/питание");
    } else if (rms > 100) {
      Serial.println("  ✅ Микрофон работает!");
    }
  }

  delay(200);
}
