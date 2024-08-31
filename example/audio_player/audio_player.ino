#include "Arduino.h"

#include <driver/i2s.h>

#define INMP441_WS 5

#define INMP441_SCK 4

#define INMP441_SD 6

#define MAX98357_LRC 17

#define MAX98357_BCLK 16

#define MAX98357_DIN 15

#define SAMPLE_RATE 44100

i2s_config_t i2sIn_config = {

  .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX),

  .sample_rate = SAMPLE_RATE,

  .bits_per_sample = i2s_bits_per_sample_t(16),

  .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,

  .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),

  .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,

  .dma_buf_count = 8,

  .dma_buf_len = 1024

};

const i2s_pin_config_t i2sIn_pin_config = {

  .bck_io_num = INMP441_SCK,

  .ws_io_num = INMP441_WS,

  .data_out_num = -1,

  .data_in_num = INMP441_SD

};

i2s_config_t i2sOut_config = {

  .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_TX),

  .sample_rate = SAMPLE_RATE,

  .bits_per_sample = i2s_bits_per_sample_t(16),

  .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,

  .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),

  .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,

  .dma_buf_count = 8,

  .dma_buf_len = 1024

};

const i2s_pin_config_t i2sOut_pin_config = {

  .bck_io_num = MAX98357_BCLK,

  .ws_io_num = MAX98357_LRC,

  .data_out_num = MAX98357_DIN,

  .data_in_num = -1

};

void setup() {

  // put your setup code here, to run once:

  Serial.begin(115200);

  i2s_driver_install(I2S_NUM_0, &i2sIn_config, 0, NULL);

  i2s_set_pin(I2S_NUM_0, &i2sIn_pin_config);

  i2s_driver_install(I2S_NUM_1, &i2sOut_config, 0, NULL);

  i2s_set_pin(I2S_NUM_1, &i2sOut_pin_config);

}

void loop() {

  // put your main code here, to run repeatedly:

  size_t bytes_read;

  int16_t data[512];

  esp_err_t result = i2s_read(I2S_NUM_0, &data, sizeof(data), &bytes_read, portMAX_DELAY);

  result = i2s_write(I2S_NUM_1, &data, sizeof(data), &bytes_read, portMAX_DELAY);
}