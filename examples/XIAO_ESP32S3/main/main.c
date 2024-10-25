#include "hal/i2s_types.h"
#include <driver/i2s_pdm.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/ringbuf.h>
#include <picotts.h>
#include <stdint.h>
#include <string.h>

static const char *TAG = "ESP32-S3 SAMPLE";

#define SAMPLE_RATE 48000 // Sample rate in Hz
#define BUFFER_SIZE 1024  // Buffer size for samples
#define TTS_CORE 1

const char *greeting =
    "Hello. Welcome back to my project. This is Chris! You are listening to the "
    "voice of TTS. I love this so much.";

const char *long_sentence =
    "There is lots of ways to be, as a person. And some people express their "
    "deep appreciation in different ways. But one of the ways that I believe "
    "people express their appreciation to the rest of humanity is to make "
    "something wonderful and put it out there.";

i2s_chan_handle_t i2s_tx_chan = NULL;
RingbufHandle_t ringbuf = NULL; // Handle for the ring buffer

void i2s_write_task(void *param);

static void on_samples(int16_t *buffer, unsigned count);

void init_i2s() {
  esp_err_t ret;

  i2s_chan_config_t chan_cfg =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);

  ret = i2s_new_channel(&chan_cfg, &i2s_tx_chan, NULL);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "i2s_new_channel failed");
  }

  const i2s_pdm_tx_config_t pdm_tx_cfg = {
      .clk_cfg = I2S_PDM_TX_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
      .slot_cfg =
          {
              .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
              .slot_bit_width = I2S_SLOT_BIT_WIDTH_16BIT,
              .slot_mode = I2S_SLOT_MODE_MONO,
              .sd_prescale = 0,
              .sd_scale = I2S_PDM_SIG_SCALING_MUL_1,
              .hp_scale = I2S_PDM_SIG_SCALING_DIV_2,
              .lp_scale = I2S_PDM_SIG_SCALING_MUL_1,
              .sinc_scale = I2S_PDM_SIG_SCALING_MUL_1,
              .line_mode = I2S_PDM_TX_ONE_LINE_CODEC,
              .hp_en = true,
              .hp_cut_off_freq_hz = 35.5,
              .sd_dither = 0,
              .sd_dither2 = 1,
          },
      .gpio_cfg =
          {
              .clk = GPIO_NUM_45,
              .dout = GPIO_NUM_44,
          },
  };

  ret = i2s_channel_init_pdm_tx_mode(i2s_tx_chan, &pdm_tx_cfg);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "i2s_channel_init_pdm_tx_mode failed");
  }

  ret = i2s_channel_enable(i2s_tx_chan);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "i2s_channel_enable failed");
  }

  // Create a ring buffer for inter-task communication
  ringbuf =
      xRingbufferCreate(BUFFER_SIZE * sizeof(int16_t), RINGBUF_TYPE_BYTEBUF);
  if (ringbuf == NULL) {
    ESP_LOGE(TAG, "Failed to create ring buffer");
  }

  // Create tasks
  xTaskCreatePinnedToCore(i2s_write_task, "I2SWriteTask", 2048, NULL, 1, NULL,
                          APP_CPU_NUM);

  vTaskDelay(pdMS_TO_TICKS(2000));

  ESP_LOGI("main", "PSRAM SIZE: %i",
           (int)heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
  ESP_LOGI("main", "PSRAM FREE: %i",
           (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

void i2s_write_task(void *param) {
  while (1) {
    size_t item_size;
    int16_t *buffer =
        (int16_t *)xRingbufferReceive(ringbuf, &item_size, portMAX_DELAY);
    if (buffer != NULL) {
      // Write the samples to the I2S channel
      size_t bytes_written = 0;

      printf("O");
      int ret = i2s_channel_write(i2s_tx_chan, buffer, item_size,
                                  &bytes_written, portMAX_DELAY);
      if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_write failed");
      } else if (bytes_written != item_size) {
        ESP_LOGE(TAG, "Incomplete I2S write");
      }

      // Return the buffer to the ring buffer to free memory
      vRingbufferReturnItem(ringbuf, (void *)buffer);

      vTaskDelay(1 / portTICK_PERIOD_MS);
    } else {
      ESP_LOGE(TAG, "Failed to receive buffer from ring buffer");
    }
  }
}

void speak(const char *text) {

  const size_t length = strlen(text) + 1; // +1 for null terminator

  char arr[length];  // Allocate array large enough to hold the string
  strcpy(arr, text); // Copy the string to

  unsigned prio = uxTaskPriorityGet(NULL);
  if (picotts_init(prio, on_samples, TTS_CORE)) {
    picotts_add(arr, sizeof(arr));
    vTaskDelay(pdMS_TO_TICKS(2000));
    picotts_shutdown();
  }
}

int16_t max = 0;
int16_t min = 0;

static void on_samples(int16_t *buffer, unsigned count) {
  printf("I");

  int16_t *buffer2 = malloc(count * 3 * sizeof(int16_t));

  for (size_t i = 0; i < count; i++) {
    if (buffer[i] >= max)
      max = buffer[i];
    if (buffer[i] <= min)
      min = buffer[i];

    buffer2[i * 3 + 0] = buffer[i];
    buffer2[i * 3 + 1] = buffer[i];
    buffer2[i * 3 + 2] = buffer[i];
  }

  xRingbufferSend(ringbuf, (void *)buffer2, count * 3 * sizeof(int16_t),
                  portMAX_DELAY);

  free(buffer2);
}

void app_main(void) {
  init_i2s();

  while (true) {
    speak(greeting);

    speak(long_sentence);

    printf("Min: %i Max: %i", min, max);
  }
}
