// =================================================================
// ТЕСТ МИКРОФОНА И ДИНАМИКА — MUMA 1.54 / Spotpear ESP32-S3
// Пины взяты из рабочей прошивки MELVIN v3.7.4
// =================================================================

#include <Arduino.h>
#include <driver/i2s.h>
#include <Wire.h>
#include "Audio.h"

// --- ПИНЫ (из рабочей прошивки MELVIN) ---
#define I2C_SDA    15
#define I2C_SCL    14
#define I2S_MCLK   16
#define I2S_BCLK    9
#define I2S_LRC    45
#define I2S_DOUT    8
#define I2S_DIN    10
#define PA_ENABLE  46   // Пин включения усилителя

Audio audio;
bool i2sDriverInstalled = false;

// =================================================================
// ES8311 — инициализация кодека (точная копия из рабочей прошивки)
// =================================================================
void initES8311() {
  Wire.begin(I2C_SDA, I2C_SCL);
  delay(10);
  auto wr = [](uint8_t r, uint8_t v) {
    Wire.beginTransmission(0x18); Wire.write(r); Wire.write(v); Wire.endTransmission();
  };
  wr(0x01, 0x30); delay(10);
  wr(0x01, 0x00); wr(0x02, 0x00); wr(0x03, 0x10);
  wr(0x16, 0x11); wr(0x17, 0x11); wr(0x14, 0x1A); wr(0x15, 0x1A);
  wr(0x0B, 0x00); wr(0x0C, 0x00); wr(0x10, 0x00); wr(0x11, 0xFC);
  wr(0x00, 0x80);
  wr(0x0D, 0x01); wr(0x0E, 0x02); wr(0x12, 0x28); wr(0x13, 0x06);
  pinMode(PA_ENABLE, OUTPUT);
  digitalWrite(PA_ENABLE, LOW); // Усилитель выключен по умолчанию
}

// =================================================================
// I2S RX — микрофон (fixed_mclk=0, как в рабочей прошивке!)
// =================================================================
void i2s_install_rx() {
  i2s_config_t cfg = {
    .mode             = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate      = 16000,
    .bits_per_sample  = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format   = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count    = 8,
    .dma_buf_len      = 512,
    .use_apll         = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk       = 0    // ← КЛЮЧЕВОЕ ОТЛИЧИЕ: 0, не 4096000!
  };
  i2s_pin_config_t pins = {
    .mck_io_num   = I2S_MCLK,
    .bck_io_num   = I2S_BCLK,
    .ws_io_num    = I2S_LRC,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num  = I2S_DIN
  };
  i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pins);
  i2s_zero_dma_buffer(I2S_NUM_0);
  i2sDriverInstalled = true;
}

// =================================================================
// I2S TX — динамик (восстановление через Audio.h)
// =================================================================
void i2s_restore_tx() {
  if (i2sDriverInstalled) {
    i2s_driver_uninstall(I2S_NUM_0);
    delay(100);
  }
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT, I2S_MCLK);
  i2sDriverInstalled = true;
  audio.setVolume(15);
  delay(50);
  digitalWrite(PA_ENABLE, HIGH); // Включаем усилитель после инициализации I2S
}

// =================================================================
// ТЕСТ ДИНАМИКА — синусоидальный тон 1 кГц, 2 секунды
// =================================================================
void testSpeaker() {
  Serial.println("\n--- ТЕСТ ДИНАМИКА ---");
  Serial.println("Генерирую тон 1 кГц на 2 секунды...");

  i2s_restore_tx();

  const int SAMPLE_RATE = 16000;
  const int FREQ = 1000;
  const int DURATION_MS = 2000;
  const int AMPLITUDE = 20000;
  const int TOTAL_SAMPLES = SAMPLE_RATE * DURATION_MS / 1000;

  int16_t buf[256];
  int sent = 0;
  size_t written = 0;

  while (sent < TOTAL_SAMPLES) {
    int chunk = min(256, TOTAL_SAMPLES - sent);
    for (int i = 0; i < chunk; i++) {
      buf[i] = (int16_t)(AMPLITUDE * sin(2.0 * PI * FREQ * (sent + i) / SAMPLE_RATE));
    }
    i2s_write(I2S_NUM_0, buf, chunk * sizeof(int16_t), &written, pdMS_TO_TICKS(100));
    sent += chunk;
  }

  delay(200);
  digitalWrite(PA_ENABLE, LOW);
  Serial.println("✅ Тон отправлен. Если слышишь звук — динамик работает!");
}

// =================================================================
// ТЕСТ МИКРОФОНА — запись 3 секунды, анализ уровня сигнала
// =================================================================
void testMicrophone() {
  Serial.println("\n--- ТЕСТ МИКРОФОНА ---");
  Serial.println("Записываю 3 секунды... (говори что-нибудь!)");

  // Важно: сначала остановить TX, потом ставить RX
  audio.stopSong();
  delay(50);

  if (i2sDriverInstalled) {
    i2s_driver_uninstall(I2S_NUM_0);
    delay(50);
    i2sDriverInstalled = false;
  }

  i2s_install_rx();

  const int SAMPLE_RATE = 16000;
  const int RECORD_MS   = 3000;
  const int BUF_SIZE    = 512;
  const int TOTAL_BYTES = SAMPLE_RATE * (RECORD_MS / 1000) * 2;

  int16_t buf[BUF_SIZE / 2];
  size_t bytes_read = 0;
  int total_read    = 0;

  long long sum_sq  = 0;
  int32_t peak      = 0;
  long sample_count = 0;

  unsigned long start = millis();

  while (millis() - start < RECORD_MS) {
    esp_err_t err = i2s_read(I2S_NUM_0, buf, BUF_SIZE, &bytes_read, pdMS_TO_TICKS(100));
    if (err == ESP_OK && bytes_read > 0) {
      int n = bytes_read / 2;
      for (int i = 0; i < n; i++) {
        int32_t s = buf[i];
        sum_sq += (long long)s * s;
        if (abs(s) > peak) peak = abs(s);
      }
      sample_count += n;
      total_read   += bytes_read;

      // Каждые ~100мс печатаем уровень
      if ((millis() - start) % 100 < 20) {
        long rms = (sample_count > 0) ? (long)sqrt((double)sum_sq / sample_count) : 0;
        // Визуальный уровень
        int bars = map(constrain(rms, 0, 5000), 0, 5000, 0, 30);
        Serial.printf("MIC RMS: %5ld  Peak: %6ld  [", rms, (long)peak);
        for (int b = 0; b < 30; b++) Serial.print(b < bars ? "|" : " ");
        Serial.println("]");
      }
    }
  }

  long final_rms = (sample_count > 0) ? (long)sqrt((double)sum_sq / sample_count) : 0;

  Serial.println("\n--- РЕЗУЛЬТАТ ---");
  Serial.printf("Прочитано байт: %d\n", total_read);
  Serial.printf("RMS (средний): %ld\n", final_rms);
  Serial.printf("Peak (пик):    %ld\n", (long)peak);

  if (final_rms > 100) {
    Serial.println("✅ Микрофон работает! Сигнал обнаружен.");
  } else if (final_rms > 20) {
    Serial.println("⚠️  Тихий сигнал. Попробуй говорить громче или проверь пины.");
  } else {
    Serial.println("❌ Сигнала нет. Проверь пины I2S_DIN, I2S_BCLK, I2S_LRC, I2S_MCLK.");
  }

  // Восстанавливаем TX
  i2s_driver_uninstall(I2S_NUM_0);
  delay(50);
  i2sDriverInstalled = false;
  i2s_restore_tx();
}

// =================================================================
// SETUP
// =================================================================
void setup() {
  // Страп-пин 45 — прижать LOW сразу (как в рабочей прошивке)
  pinMode(45, OUTPUT); digitalWrite(45, LOW);
  delay(100);

  Serial.begin(115200);
  delay(2000);

  Serial.println("\n================================");
  Serial.println("  ТЕСТ МИКРОФОНА И ДИНАМИКА");
  Serial.println("  MUMA 1.54 / ESP32-S3");
  Serial.println("================================");

  Serial.println("[1] Инициализация ES8311...");
  initES8311();
  delay(200);

  Serial.println("[2] Инициализация I2S TX (динамик)...");
  i2s_restore_tx();
  delay(200);

  Serial.println("[3] Готов!\n");
  Serial.println("Команды в Serial Monitor:");
  Serial.println("  's' — тест динамика (тон 1 кГц)");
  Serial.println("  'm' — тест микрофона (3 сек запись)");
  Serial.println("  'b' — оба теста подряд");
}

// =================================================================
// LOOP
// =================================================================
void loop() {
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 's' || cmd == 'S') {
      testSpeaker();
    } else if (cmd == 'm' || cmd == 'M') {
      testMicrophone();
    } else if (cmd == 'b' || cmd == 'B') {
      testSpeaker();
      delay(500);
      testMicrophone();
    }
  }
  audio.loop();
  delay(10);
}
