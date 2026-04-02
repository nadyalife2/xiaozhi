/**
 * TEST v2.0: Микрофон + Динамик через кодек ES8311
 * Плата: ESP32-1.54inch-AI-V2 (схема ESP32-S3R8 + ES8311 + NS4150B)
 *
 * Что делает:
 *   1. Инициализирует кодек ES8311 по I2C (GP14=SCL, GP15=SDA)
 *   2. Настраивает I2S (GP9=SCLK, GP45=LRCK, GP16=MCLK, GP8=DSDIN, GP10=ASDDOUT)
 *   3. Включает усилитель NS4150B (GP46=PA_CTRL)
 *   4. Генерирует тон 1кГц → динамик
 *   5. Читает уровень с микрофона и выводит в Serial Monitor
 *
 * Библиотека: установи "ES8311" от Espressif в Arduino Library Manager
 * Serial Monitor: 115200 baud
 */

#include <Wire.h>
#include <driver/i2s.h>
#include <math.h>

// ============================================================
// ПИНЫ (из схемы ESP32-1.54inch-AI-V2-1)
// ============================================================
#define I2C_SCL       14
#define I2C_SDA       15

#define I2S_MCLK      16
#define I2S_SCLK       9   // BCLK
#define I2S_LRCK      45   // WS
#define I2S_DSDIN      8   // ESP32 → ES8311 (динамик)
#define I2S_ASDDOUT   10   // ES8311 → ESP32 (микрофон)

#define PA_CTRL       46   // Усилитель NS4150B: HIGH=вкл, LOW=выкл

// ============================================================
// ПАРАМЕТРЫ АУДИО
// ============================================================
#define SAMPLE_RATE   16000
#define BUFFER_SIZE   512
#define TEST_TONE_HZ  1000

#define I2S_PORT      I2S_NUM_0
#define ES8311_ADDR   0x18   // I2C адрес ES8311

// ============================================================
// РЕГИСТРЫ ES8311 (минимальная инициализация)
// ============================================================
void es8311_write(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(reg);
  Wire.write(val);
  uint8_t err = Wire.endTransmission();
  if (err != 0) {
    Serial.printf("[ES8311] I2C write ERR reg=0x%02X err=%d\n", reg, err);
  }
}

uint8_t es8311_read(uint8_t reg) {
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(ES8311_ADDR, (uint8_t)1);
  return Wire.available() ? Wire.read() : 0xFF;
}

bool es8311_init() {
  // Проверка связи
  uint8_t chip_id = es8311_read(0xFD);
  Serial.printf("[ES8311] Chip ID: 0x%02X (ожидается 0x83)\n", chip_id);
  if (chip_id != 0x83) {
    Serial.println("[ES8311] ОШИБКА: кодек не обнаружен! Проверь I2C пины GP14/GP15");
    return false;
  }

  Serial.println("[ES8311] Инициализация...");

  es8311_write(0x00, 0x1F);  // Reset
  delay(10);
  es8311_write(0x00, 0x00);  // Normal operation
  es8311_write(0x01, 0x30);  // MCLK
  es8311_write(0x02, 0x00);  // Clock config
  es8311_write(0x03, 0x10);  // Clock divider
  es8311_write(0x04, 0x10);  // Clock divider
  es8311_write(0x05, 0x00);  // Clock divider
  es8311_write(0x06, 0x03);  // Clock config
  es8311_write(0x07, 0x00);  // LRCK divider Hi
  es8311_write(0x08, 0xFF);  // LRCK divider Lo
  es8311_write(0x09, 0x00);  // I2S format
  es8311_write(0x0A, 0x0C);  // I2S format: 16bit
  es8311_write(0x0B, 0x00);  // ADC config
  es8311_write(0x0C, 0x00);  // DAC config
  es8311_write(0x0D, 0x01);  // System power

  // ADC (микрофон)
  es8311_write(0x44, 0x08);  // ADC volume
  es8311_write(0x1C, 0x6A);  // ADC PGA gain
  es8311_write(0x1D, 0x40);  // ADC digital volume
  es8311_write(0x1E, 0x00);
  es8311_write(0x1F, 0x08);

  // DAC (динамик)
  es8311_write(0x31, 0x00);  // DAC digital volume
  es8311_write(0x32, 0x00);
  es8311_write(0x37, 0x08);  // DAC volume

  // Power up
  es8311_write(0x13, 0x10);
  es8311_write(0x14, 0x1A);
  es8311_write(0x15, 0x00);
  es8311_write(0x16, 0x00);
  es8311_write(0x17, 0xBF);  // DAC output enable
  es8311_write(0x18, 0x08);
  es8311_write(0x40, 0x02);  // ADC power
  es8311_write(0x41, 0x70);  // ADC power

  Serial.println("[ES8311] OK!");
  return true;
}

// ============================================================
bool setup_i2s() {
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 6,
    .dma_buf_len = BUFFER_SIZE,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };
  i2s_pin_config_t pins = {
    .mck_io_num   = I2S_MCLK,
    .bck_io_num   = I2S_SCLK,
    .ws_io_num    = I2S_LRCK,
    .data_out_num = I2S_DSDIN,
    .data_in_num  = I2S_ASDDOUT
  };
  esp_err_t err = i2s_driver_install(I2S_PORT, &cfg, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("[I2S] driver_install FAILED: %d\n", err);
    return false;
  }
  err = i2s_set_pin(I2S_PORT, &pins);
  if (err != ESP_OK) {
    Serial.printf("[I2S] set_pin FAILED: %d\n", err);
    return false;
  }
  i2s_zero_dma_buffer(I2S_PORT);
  Serial.println("[I2S] OK!");
  return true;
}

// ============================================================
int16_t spk_buffer[BUFFER_SIZE * 2];  // стерео
int16_t mic_buffer[BUFFER_SIZE * 2];
size_t bytes_written, bytes_read;
float phase = 0.0f;
const float phase_inc = 2.0f * M_PI * TEST_TONE_HZ / SAMPLE_RATE;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n================================");
  Serial.println(" ESP32-S3 + ES8311 AUDIO TEST");
  Serial.println("================================");
  Serial.printf("I2C: SCL=GP%d SDA=GP%d\n", I2C_SCL, I2C_SDA);
  Serial.printf("I2S: MCLK=GP%d SCLK=GP%d LRCK=GP%d\n", I2S_MCLK, I2S_SCLK, I2S_LRCK);
  Serial.printf("     DSDIN=GP%d ASDDOUT=GP%d\n", I2S_DSDIN, I2S_ASDDOUT);
  Serial.printf("PA_CTRL=GP%d\n", PA_CTRL);
  Serial.println();

  // Усилитель ВЫКЛ на время инициализации
  pinMode(PA_CTRL, OUTPUT);
  digitalWrite(PA_CTRL, LOW);

  // I2C
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);

  // ES8311 кодек
  bool codec_ok = es8311_init();

  // I2S
  bool i2s_ok = setup_i2s();

  if (codec_ok && i2s_ok) {
    // Включить усилитель
    digitalWrite(PA_CTRL, HIGH);
    Serial.println("[PA] Усилитель включён");
    Serial.println("\n[START] Генерация тона 1кГц → динамик");
    Serial.println("[START] Чтение уровня  → микрофон");
  } else {
    Serial.println("\n[FAIL] Инициализация не удалась — проверь подключение!");
  }
  Serial.println("--------------------------------");
}

void loop() {
  // --- Генерация тона 1кГц (стерео L+R) → динамик ---
  for (int i = 0; i < BUFFER_SIZE; i++) {
    int16_t s = (int16_t)(sinf(phase) * 20000.0f);
    spk_buffer[i * 2]     = s;  // L
    spk_buffer[i * 2 + 1] = s;  // R
    phase += phase_inc;
    if (phase >= 2.0f * M_PI) phase -= 2.0f * M_PI;
  }
  i2s_write(I2S_PORT, spk_buffer, sizeof(spk_buffer), &bytes_written, portMAX_DELAY);

  // --- Чтение с микрофона ---
  i2s_read(I2S_PORT, mic_buffer, sizeof(mic_buffer), &bytes_read, 100);

  int samples = bytes_read / sizeof(int16_t);
  if (samples > 0) {
    long long sum = 0;
    int16_t peak = 0;
    for (int i = 0; i < samples; i++) {
      sum += (long long)mic_buffer[i] * mic_buffer[i];
      if (abs(mic_buffer[i]) > peak) peak = abs(mic_buffer[i]);
    }
    float rms = sqrtf((float)sum / samples);

    int bars = (int)(rms / 600.0f);
    if (bars > 40) bars = 40;
    char bar[41] = {};
    for (int i = 0; i < bars; i++) bar[i] = '|';

    Serial.printf("MIC RMS: %6.0f  Peak: %6d  [%-40s]", rms, peak, bar);

    if (rms < 80)        Serial.print("  ТИШИНА");
    else if (rms < 500)  Serial.print("  тихо");
    else if (rms < 3000) Serial.print("  норма  OK");
    else                 Serial.print("  ГРОМКО");

    Serial.println();
  }

  delay(200);
}
