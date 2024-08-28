#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <base64.h>
#include <driver/i2s.h>
#include <Base64_Arturo.h>
#include <UrlEncode.h>

// I2S config for MAX98357A
#define I2S_OUT_PORT I2S_NUM_1
#define I2S_OUT_BCLK 16
#define I2S_OUT_LRC 17
#define I2S_OUT_DOUT 15

// INMP441 config
#define I2S_IN_PORT I2S_NUM_0
#define I2S_IN_BCLK 4
#define I2S_IN_LRC 5
#define I2S_IN_DIN 6

// WiFi credentials
const char* ssid = "H3C1";
const char* password = "bjlg2023";

// Baidu API credentials
const char* baidu_api_key = "mnCVxgi8S8fLfIldPeNUewAq";
const char* baidu_secret_key = "x0xcBGyUA7NTxzw7wEJyEz9uqmXMbxTd";

// Baidu 千帆大模型
char* qianfan_api_key = "Lb7o26wf56OZRO1r1ht5qpDV";
char* qianfan_secret_key = "YVlGevejLEQOy477sfW1BdC8wzidvET8";

const char* stt_api_url = "https://vop.baidu.com/server_api";                                                                 // 语音识别API
const char* ernie_api_url = "https://aip.baidubce.com/rpc/2.0/ai_custom/v1/wenxinworkshop/chat/ernie-lite-8k?access_token=";  // 千帆大模型API
const char* tts_api_url = "https://tsn.baidu.com/text2audio";                                                                 // 语音合成API

// Audio recording settings
#define SAMPLE_RATE 16000
#define RECORD_TIME_SECONDS 10
#define BUFFER_SIZE (SAMPLE_RATE * RECORD_TIME_SECONDS)

// Function prototypes
String getAccessToken(const char* api_key, const char* secret_key);
String speechToText(int16_t* audioData, size_t audioDataSize);
String getErnieBotResponse(String prompt);
char* textToSpeech(String text);
void playAudio(uint8_t* audioData, size_t audioDataSize);
void tts_send(String text);
String stt_send(uint8_t* audioData, int audioDataSize);

void setup() {
  Serial.begin(115200);

  // Connect to WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");

  // Initialize I2S for audio output
  i2s_config_t i2s_config_out = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
    .communication_format = (i2s_comm_format_t)(I2S_COMM_FORMAT_STAND_I2S),
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 1024,
  };
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_OUT_BCLK,
    .ws_io_num = I2S_OUT_LRC,
    .data_out_num = I2S_OUT_DOUT,
    .data_in_num = -1
  };
  i2s_driver_install(I2S_OUT_PORT, &i2s_config_out, 0, NULL);
  i2s_set_pin(I2S_OUT_PORT, &pin_config);

  // Initialize I2S for audio input
  i2s_config_t i2s_config_in = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,  // 注意：INMP441 输出 32 位数据
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 1024,
  };
  i2s_pin_config_t pin_config_in = {
    .bck_io_num = I2S_IN_BCLK,
    .ws_io_num = I2S_IN_LRC,
    .data_out_num = -1,
    .data_in_num = I2S_IN_DIN
  };
  i2s_driver_install(I2S_IN_PORT, &i2s_config_in, 0, NULL);
  i2s_set_pin(I2S_IN_PORT, &pin_config_in);
}

// void loop() {
//   // put your main code here, to run repeatedly:
//   size_t bytes_read;
//   int16_t data[512];
//   esp_err_t result = i2s_read(I2S_IN_PORT, &data, sizeof(data), &bytes_read, portMAX_DELAY);
//   for(int32_t i = 0; i < bytes_read/2; i++)
//   {
//       Serial.println(data[i]);
//   }
//   result = i2s_write(I2S_OUT_PORT, &data, sizeof(data), &bytes_read, portMAX_DELAY);
//   delay(100);
// }
uint8_t* pcm_data;

void loop() {
  if (Serial.available() > 0) {
    // 读取并打印输入的字符
    int incomingByte = Serial.read();  // 读取输入的字节
    Serial.println(incomingByte);      // 在串口监视器上打印读取的字节
  }

  // Record audio from INMP441
  // 分配内存
  pcm_data = (uint8_t*)ps_malloc(BUFFER_SIZE);
  if (!pcm_data) {
    Serial.println("Failed to allocate memory for pcm_data");
    return;
  }

  // 开始循环录音，将录制结果保存在pcm_data中
  int incomingByte = 0;
  size_t bytes_read = 0, recordingSize = 0;
  int8_t start_record = 0;
  int16_t data[512];
  while (1) {
    if (Serial.available() > 0) {
      incomingByte = Serial.read();  // 读取输入的字节
    }

    if (incomingByte == 50) {
      Serial.printf("start_record: %d\n", start_record);
      start_record = 1;
    }

    if (start_record) {
      esp_err_t result = i2s_read(I2S_IN_PORT, data, sizeof(data), &bytes_read, portMAX_DELAY);
      memcpy((uint8_t*)pcm_data + recordingSize, data, bytes_read);
      recordingSize += bytes_read;
      Serial.printf("%x recordingSize: %d bytes_read :%d\n", (uint8_t*)pcm_data + recordingSize, recordingSize, bytes_read);
    }

    if (incomingByte == 49 || recordingSize >= BUFFER_SIZE - bytes_read) {
      Serial.printf("record done: %d", recordingSize);
      break;
    }
  }

  Serial.println(recordingSize);
  if (recordingSize > 0) {
    // Perform speech to text
    // String recognizedText = speechToText((uint8_t*)pcm_data, recordingSize);
    // Serial.println("Recognized text: " + recognizedText);
    stt_send((uint8_t*)pcm_data, recordingSize);

    // Get response from Ernie Bot
    String ernieResponse = getErnieBotResponse("你好，说句话，10字以内");
    Serial.println("Ernie Bot response: " + ernieResponse);

    // Perform text to speech
    // char* synthesizedAudio = textToSpeech(ernieResponse);
    tts_send(ernieResponse);

    // Play audio via MAX98357A
    // Serial.printf("synthesizedAudio: %s %d", synthesizedAudio, strlen(synthesizedAudio));
    // playAudio((uint8_t *)synthesizedAudio, strlen(synthesizedAudio));
  }

  // 释放内存
  free(pcm_data);

  delay(1000);
}

String baidu_access_token = "";
String qianfan_access_token = "";

// Get Baidu API access token
String getAccessToken(const char* api_key, const char* secret_key) {
  String access_token = "";
  HTTPClient http;

  http.begin("https://aip.baidubce.com/oauth/2.0/token?grant_type=client_credentials&client_id=" + String(api_key) + "&client_secret=" + String(secret_key));
  int httpCode = http.POST("");

  if (httpCode == HTTP_CODE_OK) {
    String response = http.getString();
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, response);
    access_token = doc["access_token"].as<String>();

    Serial.printf("[HTTP] GET access_token: %s\n", access_token);
  } else {
    Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();

  return access_token;
}

String stt_send(uint8_t* audioData, int audioDataSize) {
  String recognizedText = "";

  if (baidu_access_token == "") {
    baidu_access_token = getAccessToken(baidu_api_key, baidu_secret_key);
  }

  //json包大小，由于需要将audioData数据进行Base64的编码，数据量会增大1/3
  int data_json_len = audioDataSize * sizeof(char) * 1.4;
  char* data_json = (char*)ps_malloc(data_json_len);
  if (!data_json) {
    Serial.println("Failed to allocate memory for data_json");
    return recognizedText;
  }

  // Base64 encode audio data
  String base64AudioData = base64::encode(audioData, audioDataSize);

  memset(data_json, '\0', data_json_len);
  strcat(data_json, "{");
  strcat(data_json, "\"format\":\"pcm\",");
  strcat(data_json, "\"rate\":16000,");
  strcat(data_json, "\"dev_pid\":1537,");
  strcat(data_json, "\"channel\":1,");
  strcat(data_json, "\"cuid\":\"57722200\",");
  strcat(data_json, "\"token\":\"");
  strcat(data_json, baidu_access_token.c_str());
  strcat(data_json, "\",");
  sprintf(data_json + strlen(data_json), "\"len\":%d,", audioDataSize);
  strcat(data_json, "\"speech\":\"");
  strcat(data_json, base64AudioData.c_str());
  strcat(data_json, "\"");
  strcat(data_json, "}");

  HTTPClient http_client;

  http_client.begin("http://vop.baidu.com/server_api");
  http_client.addHeader("Content-Type", "application/json");
  int httpCode = http_client.POST(data_json);

  if (httpCode > 0) {
    if (httpCode == HTTP_CODE_OK) {
      String payload = http_client.getString();
      Serial.println(payload);
    }
  } else {
    Serial.printf("[HTTP] POST failed, error: %s\n", http_client.errorToString(httpCode).c_str());
  }

  if (data_json) {
    free(data_json);
  }

  http_client.end();

  return recognizedText;
}

// Perform speech to text using Baidu STT API
String speechToText(uint8_t* audioData, size_t audioDataSize) {
  if (baidu_access_token == "") {
    baidu_access_token = getAccessToken(baidu_api_key, baidu_secret_key);
  }

  HTTPClient http;
  http.begin(stt_api_url);
  http.addHeader("Content-Type", "application/json");

  String recognizedText = "";

  // Base64 encode audio data
  String base64AudioData = base64::encode(audioData, audioDataSize);

  // Construct JSON request body
  DynamicJsonDocument doc(1 * 1024);
  doc["format"] = "pcm";
  doc["rate"] = SAMPLE_RATE;
  doc["dev_pid"] = 1537;
  doc["channel"] = 1;
  doc["cuid"] = "ESP32-S3";
  doc["token"] = baidu_access_token;
  doc["lan"] = "zh";
  doc["speech"] = "UklGRuSyAABXQVZFZm10IBAAAAABAAEAgD4AAAB9AAACABAAZGF0YcCyAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAADIAMAAwADIAMgA0ADwANgA2ADYAJgAsADwAOAA0ADoANAA2ADIAPgBCADAAOgA+ADgAOAA8AEQATAA8ADgAQABGAEQASAA+ADoAPAA2AEAAQgBEADwANgAuADQAOgA6ADYANAAyADAANgAyADQANAA2ACoALgAsACgAMAAsACoAJAAmAB4AJgAiAB4AIgAaABoAGAAYABoAFgAYABYAFgAUABIAEAAQABAADAAOAAgADgAMAA4ADAAMAAwACgAIAAoADAAIAAgAAAACAAAA/v8AAAAA/v/+//z/+v/+//r//P/+//z//P/2//j/8v/0/+z/7P/q/+r/7v/s//D/6v/u/+7/7v/s/+r/5P/m/+r/6P/o/+z/6v/o/+j/5P/k/+j/6P/m/+r/6v/s/+z/7P/q/+7/7P/u/+r/8P/u/+b/6P/k/+j/7P/q/+b/7P/w//L/9P/2//j//v/4//j/+P/4//r/+v/4//T//P/2//b/9v/2//L/9v/0//T/9v/2//b/9v/2//r/+v/2//j/+v/8/wAA/v/8/wAAAgAAAAQABAAEAAgACAAIAAwADAAMAAwADAAMAAwADgAMAA4ADgAMAA4ADgASABAADgAOABIAEAAQABIAEgAUABQADgAQAAwACgAIAAgACgAEAAYACAAGAAYABgAGAAIAAAACAAIAAAD+/wAAAAD+/wIA/v/+//7//v/+/wAA/v/+//7/+v/6//b/9v/2//T/9P/w//L/7v/s//D/8P/w//L/7v/w/+z/6v/o/+b/6v/i/+b/5v/m/+j/6v/m/+b/5v/k/+b/5P/i/+T/4v/i/+j/4v/k/+L/5P/o/+b/6P/o/+r/7P/s/+7/8v/y//b/9v/4//j//P/+//7/AAAAAAIAAgACAAQAAgACAAQAAgAEAAQABAAGAAYABgAGAAYABgAGAAYABgAGAAYABgAGAAYABgAGAAgABgAEAAQABAAEAAQABAAGAAQABAAEAAIABAAAAAIAAAACAAIAAAACAAIAAgAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAP7//P/8//j/+P/4//b/9v/y//T/8v/u/+7/7v/u/+z/7v/q/+z/5v/o/+j/5v/m/+T/5P/e/+L/4P/g/97/4P/a/+D/3v/a/+D/3P/e/+L/4P/i/+b/4P/m/+j/6P/k/+L/5v/o/+r/6v/o//L/8P/s//D/8P/w//T/+P/8/wAAAAAIAAQABgAKAAYAEAAUABIAFAAWABgAGgAaACAAHgAaABgAGAAUABAAEAAOAAoABgAGAAYABAACAAIAAgAEAAQAAgAAAP7//P/4//b/9P/0//D/8P/w//T/9P/y//T/8v/w/+7/6v/q/+j/5P/k/+T/5P/i/+D/4v/g/+D/4P/e/97/3v/c/97/4P/i/+b/5P/m/+b/6P/s//D/9P/4//r//P8AAAAAAgAGAAwADgAQABAAFAAUABYAFgAUABQAEgAQABAAEAAOAAwADAAQABAAEgAUABQAGAAYABYAGAAYABgAHAAiACIAJgAqACgALAAoACoALAAqAC4AKAAuACwALAAmACoAKAAcAB4AFgASAAYADAAGAAAA/P/y//D/6v/e/9j/2v/U/9L/0P/M/8r/yv/O/8r/xP/K/8b/yP/Q/9T/zv/Y/9j/2P/g/+b/5v/e/+r/8P/0//L/+P/4//z/+v/8//z//v8CAAAAAAD+/wYAAgAGAAYABgAGAAwACgAMAA4ADAASAAoAEAAQABIAEgAWABYAHAAaABwAIAAgACIAJAAsAC4AMAAyADQANgAyADgAOAA2AD4ALAA6ADoAQAA0AD4ANgAwADoAOgA2ACYANgBAAEAAMABAADgAQgA6AD4AOgA+AEIAOgA8AC4AOAAuAC4AIAAeABoAHAAWAAwADgAIAAgAAgAAAAQAAAAAAPr/+v/y//b/8P/y/+7/5v/k/9T/0v/O/8L/vP+y/6D/pP+M/4z/hv94/3r/bv9o/2b/XP9a/17/XP9e/2L/aP90/3j/iP+W/6L/qv+q/7L/tP+4/7j/uP+y/7D/uv/A/8j/yP/W/+T/9v8CAAgAEAAUABwAIgAuADgARgBYAGQAdACGAJoApACsALAAtAC6ALwAwgDKANgA4gDuAP4ABAEQARwBLAEwATIBNAE2AToBOAE2ATABLAEgARYBDgH8AO4A3ADSAL4ArACaAIgAfABsAGAAUgBIADIAIgASAAAA+v/0/+z/3v/S/8j/vP+o/47/ev9s/1r/RP8w/zD/Lv80/zL/MP8c/wT/8P7e/sz+tP6Y/n7+Vv4w/hr+/v3k/dD9vP28/cD9wv3Q/cD9vv2m/Zb9iP2M/YT9Zv1E/RD94vyk/Ib8cPxo/Er8Fvza+6j7lPuQ+477iPuG+477lPvI+zL8oPzu/Cb9cv3m/Yj+HP+2/z4A5gCcAWQCPgMKBNwEngVkBiAH7geyCIoJRgrmCooLLAy8DBQNcg3WDRIOHA4GDuwN5A2qDVoN0AwmDGYLnArYCfYIAAgMBxgGHAUMBP4CJAIsARgAAP8e/mz9rPzC+976Ovq4+Tb5mPgo+NT3Zvf29qL2hPZ89k72Hvb29Qj2DPYC9gD28vXs9er19vUI9hD2JPY+9j72UvZo9pT2hPZk9iT23PWU9YT1MvU49Ub1TvUq9TD1HPXA9JT0+vM48xTyUvGm8KbwfPB+8ETwXvBS8Tz0qvn6/uwBTgI4BLwIBg52EXATUhVYFwAZOBvcHiIiBiOiIfYgJCK+I9wjoCLuIOYeuhzAGg4ZEhc4FKoQSA2GClQISgYmBOABnv+4/TT8IvtE+pT5Avl0+M73bve292L4tviC+GD4yviU+ST6ePrY+j77avtE+zD7ZPuM+3z7Evvc+vz6UPtm+xb70vrE+r76ZvrQ+Vb57vhQ+Kj3QPco9wb3qPY+9v71DPYu9jb2LvZS9nr2wPYk96T3GPhc+Kr43Pgo+Vr5iPmU+Wz5UvlI+UT5GPnA+GD45vcw9072VvUc9aT0AvTe8q7x1vBk8ELwKPDg7+Tvju9G76rw1PRg+/b+/v6Q/pIChAkKDuAOgg/IEqYW1hi8GogeOiJGIwgiciL2JMwmuCU8I7Ah1CDgHuoblBn6F9gVgBLwDhAM2gmwB4YFPAOGAMT9xPvE+mL60vkM+UL41Pfs95b4iPku+iL63vlW+oz7pvy8/Jz8Yv2O/sj+AP68/Zb+7v7+/d78CP2w/Tr94vsi+4j7uvvK+oL58viu+Nz3OPYC9YD0RPSi8/DywvLI8jDzmvNS9Aj1uvVm9ij3Aviu+Gj5svlY+sr6oPt4/Bz9dv2S/ej9FP5O/uT91vzK+376qvhi9x71evRS8+Lx6u9w7azrrOpu6ezozuig50jlTOO058TvwvYe9XryxvVqAPgJTAqCCMgMYhXQGkIbSh3gJBoqQirSJ8QpYi4qMJQtyioQKlApQCZUIiQgXB7EGvgU4g9gDL4Jaga4Ah7/vvsI+Ub3MvZc9Yj0FvT+89jz+POg9LT1VPaW9mr34vgg+qr6hPuI/dD/jgDo/+L/UAGSAr4BBgDy/0gBPgEy/5j9eP7U//D+SPys+rb6RPpa+PD1vvQ29HjzTvLA8RTyUPKO8vry5vMQ9cj1tPa49/L4nPk0+tz65Pv4/Lz9wP4S/zT/Uv+Y/27//v74/cD8ZPpE+J71WPUw8+7wQO6y6sbpjOa84zLhTuDA4LbeBtkm2azguuyG8Ezr7umc9LYEjAloBuQGwBHuHPAdCB1+IzAu9DNqMN4ucjN2OJY46DSsMpAx+C02Ke4lFCSQIPgZYhMED5gMOAleBPT+5Ppy+NL2lPRA8gLxvPF48vbxuvFY8+r1EvcC98730Pm4+qz6Zvtm/nYA5P/I/mwA9gPgBHYCmgAQAi4EhgJg/pb8Mv4+/zD8iPjU9xz5kvgM9kL0GvS080byDPEm8cDxmvFQ8dzxjvPC9J71PPYc+Jb5jvre+sz7UP2E/kT/zv5w/4b/ZAAAAFD/GP8U/gj8gvmW9ir1zvIw78Lr1Oeo5e7ivt/q3ardpN3W2V7T2tX04WjuMuuE4LDkRPjyCOQGKv4YBuYXsB8aG+AYnCYsNLgzfiz8LeY3fj1uOtI1BDWeM/AueirQKLQmhiBaF2YRgg8QDvYJnAPq/lr8avpS92r0fvOm8wrzWvG88MryFvUI9s72pvmS/MD86vtu/mYDJgQCAdj/ggTCB+AE3ACMAkoHAAcWAbz9XgAiAyYAdPp8+Kr5UvmG9QTzSvNI9NjyrvDk77jw2vCq8HLwRPFq8ZDxlPKS9Hr2gveQ+LT5wvvA/Aj9kP0s/pz9xPz4+lj63vjQ9wj10vPQ8dLvfu2I6Wbm8uI04Ercktgw19TYqtUu0DrSiuIC8O7qityU5JYAqBEwCXL8HApkI4grICGUIOowJj/aOjYy2DZcQTRDZDwwN4Q3ojVYL3orFCp6JbAbnhM6EUYQjAqOArb8NvpQ+Kb1IPNu8jryPPEg8IDxLPTY9Fb0CPaM+t778vkk+4wBAAa2A4QAzAQ4C8YKVgQqAsgHXAs4BywAyP9cAwwCVPt29xj6pPuq9hDw6O7U8eDxmu6M7EDtZO6k7TrtCO4W8CTx5vF28gr0svXq9iD4IvlU+9b7uPxM/JD8oPz0+1T6SPjK8/jykPFI73zsxOXC5EzgRN7c14TU4NXm0xLPIsY6z97knO++46TX2un0CuoV3gRm/iQUeCtOLEofwiNkOCRB/DnMM7I6bkWGRTA/7Do+OKwzXi5GKxAoDiBSFZ4O4gzUCiIFcv1e+YT4ZPju9Gjv1OzA7+j1wvdA89TuRvL4+4ACJAAu/aoAdghwCVAFqAXuDDIRZgwsBvQGxgo2CjwHigagBiYB/vom+g7+CP1c9fLuju+y8jrxyup86Pjqbu3k6rjnvui+7Ijunu727trwwPQa9kD4KPkQ+3L6kvoE+5j7Pvog+OD03vWe8pLvSO365wzn8t8Q24LV/tGK1ObSgspQw8zHkN5M7cLm5NZ63vL98BCwCRD81AmEJWotaiSMIzI1bEeYQ/A5Rjq2QwxJWETuPLA4ajTeL1YrOCYaIEQZfhMWDiwI0gHW/XT71vm29BTuBOss8OT2lPU07cTqSPWKAc4BMvoO+uIDzAwMC0AHMArQERgUdBDkDRoPLhAGD84NqAscB84AYP/4AEr/pvc08YDxHPQe8VbrFuim6erpnOdU5k7oIOoU6l7pCut271LxAvK28u70TPUQ9dj0HvaO9eDzpu9m8jzveu586kjlZuP43BDZnNQ81CjVltGMwerCcNRI7H7qENdU1XzwugzYDED/Fv6gFVQpJChKIQorVjw2Qxo55jZoP0ZHGkR+PLg58DigMvgpACcwJjwhChXcC4QIBgnIBdb/FPj28R7wIvWW+Sz2Cu5O7vr4TgC+/Wj5lADkCgANRgaYBBwMThV4F9ATKg8sDXwO2hH4E0wQNAluA1ICxAEG/kj4hvWu9TrzJO0U6DbnlOiu6LTnvuWc5VrltOYm6Srr/ut664Lsru0O8V7ypvNe82rxuO+Y7YLvOu+26tTkGN5w2tzV/NIw06DTItD2vtzADtkC9ITxcNJezybylhe2Fcr/Xv0aGVoqlCOWICQvxkBGPj4yPDT0QZZFLD/IOWg70DeCLFwlPCk0KzYe2AvmBKQKHgzcACjzaPGA+fD8RvVg62bsEPZE/iT9AvqK+xoBcgRcBWAKyBL8F6oUVBAGEfYWZBrAGpoYpBNOC3gEzAQ2CWwIFP5G86LvrPBU757rbuhU6LTmHOT24gzlVudK5oDlsOWm6Y7qwOrq6SDs3OtC7X7s0OhA7ObqQus+6jDh5tz01lTQLNAs0CLSgsj2u6K91N+m+Cb0Jtc60Ib7yiHUH7gBogKqIRY2XC0EKdg4nEgCP7I23Dw2RqJDoDh+OOY4Qi64HyQeXCNIH/QL8P8YAoYGVvy07krtxvYq+kjwgOaY6Wb1kP0C/Zr7Pv8YA+gFZAr8FYAfRh6QFgAVPhsiIZwhJiCoHbYXeA7iB8IGTAfaBHL/xvfe8STsZOkc6szsWOuu5nzijuIG5EbjvuOE44rlLuQi5objvugY5/7n2uUQ4wTlJuSW4/7XcNMQzbzMVMn6yWrF2r+UuLa9ot4i+ZD1Kti40nD8iiZ0JfgQqA4CJNoyLjQOORhH9krGQug+pkJ6RgxDJkE2Q5Q5eiOkFdwbnCRMHIYGmvh2+Ab4APKg7PjvGvV68vDpwueM7gD6LAKkBf4FQAYUClYRAho0H64gDiA+It4lOiaoILoa/BhKGpoY3BF6B3z+1vhQ9sL0qPHO7ATnruH23LzantpQ3CzcttuG2YDYHNdW2ITbzN784eThcOEu3YDb2Nz03v7cbNZm0FDMwMrkxGrDnsc6yFLC1sOE5VwMVg7S6PDZhAfCPTxIwitSHTAuYDwIQFRJOFVeU6Q9MDFyOlhFukD4NBgtRCOQEs4GzgcCCgICwPX28G7uhOcU4Zrn+va6AFD9Fvhq99r+kAo4GrQlYiU+HGYYBCHQLKwxOCw4J9YgRBzwFDYTOBIYECgGePpW82zypvGS7O7mIOEg4CbfSOKS4fLf4Nrq2KbX3tpk3E7g6t+I3YTbItoa3ZbeAOCK3T7cgtpM1wbMrsXIwrbHRsROvhq9rsJCxTrGnOP8CigdrAFw6+4K8j80XB5LOjIYMoo7skzEWGZYBkroM7wy7jqYO1ou2hyaFDIN7gaYBDIEcvso6zzhxuVc7kDxpPIs+SgDoAY4BbwIIBRyItYtFDRANPYstiaCKV4ypjVeL6YlzB12E/QG6P6S/k4AEvxQ8rbmMtwy1urWGt044ubg+tso2NzYMts83kjhtuX25rrkQOAA37jhOuLC4D7fit8C387ZENVK0pTQxsz+xgrD8sGAwZ69VL6wxJbP+Ng86ZQHxiCYIUgSHBVWMxBNkFAKRUZByEVIRoBB3jwqOsYz1CswKWgotByYB3r5qv26B0AJQv/k9LTtNuvg7z78eAoQEvwTcBXOF4Aa7h7kKUw4DEB+O0IvQiaKJDwmRiWsH8QVPAskArj7xPVm7krnuuPc5ITmBuTo3uDbUN7y4iTmOuhs6yrvwvD07kDs7uq66o7qEuos6XDmzOGQ2+jW+tQc1MbSQNBOzCjJesWywizBNMCMwVrCmMRqyNbQEt868ZQHxBxKJ74jeB+OKW48mEgaSoJHGEQaO74uqChMKIwmOCHKHu4eNBjAB/r4rPfU/zYH3gjICAYIPAaiBe4IZA+iFRQcWiQ0LBouhimIJFQkDie0KBgnjiPOHhAZiBJcC5ADkvwg+aL5avpk9xLxPOvo6Lrobuka6yDuavH48mbx5O4a7fLs7u1O7/zvpO7Q6lrlVOAs3PTZENkC2YbYeNge1iDUINJI0WbSwtTi1lTYFNfa1PLU7NMs1nLYct+U6mz3GgYQE/YZ4hoKGxwgQiieLnQxMjEUL/IpFCWOHzgbOhcgFfwUxhQSEqQNgAqyCWwKBAvUC/IMMA/kEaoUahbqFmQW1hZuGTod0B9SIHIfhB6gHYgbvhhOFhAVHBQSE6ARWg92Cx4H1APsAQoA0PxK+Rz3wvZe9u70lPJ88MLuxO2E7ajtsu0q7RzsnOrA6KbmnOSs47Dj/OP245jjZuN+4pDiaOJi4zzk8uWK5srlvuSI4lbgFN1S29zZvtnQ2Zbc3N/O5DrtGvcEACIFLgjuC3IS8hbOGP4YgBrsHJ4eVh5IHLYZiBeYFkIWkBbOFlIXTBiAGXAZ0hf6Fe4U+BSYFaAWuBcqGDIXBBV+E8oSghK+EQQRThGAEpYTVBPuEV4PxAwQC3oKIAlYBlIEAgRwBGoDxABG/nj90v2u/d78aPzs+576lvik9sD0BPM08eDvru6w7RztfOum6pLpvOjW5yjobugc6P7oEulg6qzqSOqm6aDoQOiw5ybo5uZa5YDjUuKO4cThguHc4r7mVOyK9bj5gvyG++7/9gMqBIwE6gIGB44K1A1uDyYRvBIAE1QUmBdqGLgZfho0G1gcYhtgGowXcBaCFbgUqhO2E4YU+BRMFeYUIhTSE6QTtBKGERIQaA+8DiQOIAvUCAYHHgfgB3QGmATgA1wF3gVABlYDYALcAugBWgHi/gb9mPuQ+o748PXo9G7zrvIW8prxJPAO8ajvxO6U7u7sFO007DDsVOs27CTsBO4i7qLvWPBC8BzxlPBy8azxKvKy8SjyYPLE8rjzcPWS9YL2LPka+s76QPpW9/r52v02/0AAmgK0A7gG/gm4CbQI8AdKBxgG7AfwB4QI4gmiDIAOgg+eD4wOuA7kDvoMHAxYDHQMmg5KEYIRDhF0EWIQgA7iCn4FagOCAxgELgZQBnAIrApiC0gKYgdABPgCzgEMAdIBlAFsAgYE2AL6AnoAjv36+1b6zPk29zz3nvj0+PT50vlE++b7kPkm+Vz2aPYg9Qr08PS69RL2yva89hr2CPYa9bT0+POg9gT3Tvjw+Jj4lviY9dryMvBM71DwXPIa+dz8mgHOAjABQgBQ/jT/QPvU/Ej8DABKBXoHDgrsCCgHPgUcBSADegKaAYACZAOCBVgGIAh+C2YLgAyaDdoM8gtgCm4F9gCc/9j/hgGeA0IFSgcwCX4IrgaSBTQF/ARoBKIEFgYyBmgEgAKoAEoBvP8i/0j+9v4MAZ7/6P86ACAA5v4iAQIBkAB0AHj9aP3A/Ej7qPss+xL7OvzO+1b7evvU+9D6JPwQ/dT9pP98/8j92P4i/dL5RPhq9tT16vZo+cD6CP4CAFAB4gGCAKT+GP62/cj8Vv3W/oYAGAOoAUwB7gHcAT7/ivw2/ej80P9K/fz+jv/6ACwCXgH8BfoCAgfsBKAEmAKgAH4AbvwWAG79cgCg/w4CyAOaBBYFdASUBPoCNgTmAToAIgIy/97+8gB8/ZD+wP0c/+4A/gIiA+QEWAXSBAoC6P/U/Sb8FvwO/b7+cABGBP4DfgWuAtT/tP3K/Mj5KPt8/uD9+AGgArQCwANiAPj93vpg+Oz4jvgc/Fr+SgLSBYoFgAaAAS7/BP1U+6j79PtQ/gAACgSGAugDnAEI/mj/BP04/pT/oP2mAPYB/gDw/5L/CAH4/GD/wv9U/0j+vv5AA5gCUATYABYAZAXMAzoArP2m/i7/vPvQ+uL5cvzk/3j+4AAWAu4AGgFUAI7+9P2M/Fz8av4AAOwBGgJABYwEmgQuBm4BagPyAvb+MgA6/Rb91v7O+3r+EgCK/swB6ADGACICwP8q/pD/Cv1I/ub/jADaAiIFtAa4BSIJ9AIwAwT/zPr0+wT7mvuw/lwC1AEOB/gEvAP6AeoAIP1K/XT7nvxKAAYA7AFoAuQD4gACA+L/vP2S/FD8vPoc+l79Tv2u/oAAlAQGBe4BFAO+AlwAvP7q+SD5uPgq+o78BP8GA7gFVgYUBGwGMAbe/mz7avuO+VT7gPx4/ID/rAOOBKQENAWeA37+ggBoAnL97P10/CAAFAIgA9YBuP6wA1wB3AC6/8798PpY+0QC9gAkBagAFv+YAogCMACS+ZL6lPkM+Vz7jv3Y/TYAbAI2BcIGbAa2AmYA/ABi/TT9Hv0U/X7/PgHaAbAAMAMKA64Ahv/M/fj90vuI/Gz8bvsg/9r8bP5mASICigG0/4AFXgQcB2AEagEYAzoCJgO4/0r/Yv0g/Mj83P2y/Tr90P36/nQASgDI/qQB9gZGBsIDTAZ8Arj+nvzG+3z7QPsm+iz9OAM4A8YHfAdCBLADQAJ4+9r7Dvia9lL5/vk4/kj+/gIeBGAHgAcgBFb/jP7q+074yvny9Wz5Gv6iBHIGSgW+BroJSgbc/3D9qPri+pz5Svve+yD+AgMmBOwHjAYqAsYG5gAQAPL7YPqG+pj5uPwE+SwA+gBuA3YGGgXYCUAIVgFCAHT+dP7i/Fz9+PpU/94DPgQKCMAGaAX2+iwA5P/K+sj59vNK/NL/NAQgApj/HgPOAgwD6P0g+nz0Uvpe/v77Wv5K/fQAFAdiC7wH/gTCAyACwABa/Lr4EvOm97D8SP+wAN7+dgPIBuIIyv84/RD80vvi/qr6bPyE91wBbAeOBpwH0ANcCtgIhAl+/Wz5lP+++lT8+Ppc/gj5EAGeBaABmAGe+uz9Ov/WAUD63vdg/Fr/Xv6G/fwABv5eBRwEWAW4BBb+7gEiAZwEGv4O+qL8kABoAWL+pv08+7gDagBEAoQAPv6AAKT/rgOG/DoAOvx2AWoBiP82An78WggGBBQE2gHAAaYC5P8qAjz+/Pze/mL+OP/y/sYAuAMAA2oH2gGmA2gAEP0W/m75oPpm/O78FgHYAa4DMgfqAp4DcABK/QD+3Prw+cL6KPn+/OL+IP5kAtgBPgIsA94ATgLIANT+nv3E/YL+LP3c+0z+DAAm/uwAKgDgADwBkgDoAJT/VP/W/yb+ov56ApADDgRIA3YFlgRcAWb/gvzQ/hD9MvlS/GT8nv6G/8AATgG2ABoBzP1W/gQBiP+u/PL5XP8cBHAD0AV2AmgGUAZWCEQBVP0cAiT8BP4S/bb+QPqe/XQBFP0G/zz9zP88AlgEKgWqAnAFNAZM/8T83vri/cr+OPkq+8j6zP3WAfQDZP/gAEwDzv+6AwYBHABkAMb7uvz6/JD9/v0EAToDzAJ8B7AEbgVmAmACKv9s+MD8Bv4s/cb7yv4YAawBMgNkABIAGAK4/uYBLAMIAhIC5P6WACQDqvsa+hb7dvtc/8b83gHKAaIDKAc8BZAFpgI0/gr6mP5o/p77nvsg/Lj/rgLsAuYBkAGCAMwCmgS4A9j+HPjS/hoDnP4g/kT6CACSATgC/P+6/KwA8P9GAeT9fvzU+Az9UP4Q/ggBiv4CAdACvAX8AQL/Pv8wAcoB1P8a/qb7hALcBJICygAEAPYDYgKA/Vz7kPlo/Z78rPvM/VgBVAJcATwIpAWgB9oEYv9KAXwCIgBW/jT+iAHSAbIEtgXUA5IGov8wAzT9XPn6+Wb3HPyC+TL5+P62AEQD9gJSA84E3gI6AvQACgIiAlAAdAGoAVj/4gCG/Fj+bvsC+kb9ZPr4AL4AMAHwA5ABZgFSAKz/oP2M/kb8BP9E/+T9kgBG/twAFgHmAZ4AGgTOAo4E8v+6AIoA5vv2/sT8DvnK/pD+vPxEB7QANgV4A1wEegLm/4gByvnQ/Eb/GPrc/Y7+vgBeBMD/rgKkAngB4gFm//z8/gEi/gb/IgHsABoAjADQ/ir8Xv8y/aD8vv4eAUb/OgDGA0gCjgIqAuYAEgNs/k7/YgGU/Vz9sv4g/qQARAJsAz4ArAAaBHYCgv8c+wr9ePoK+8j9FP8U/jQA2ADiBHoI1AJKARIBXAFQAcIBSv3M+s7+vAAAAgACqv34AKYC+ADWAFb6cPuk/yz8aPtY/ej/SgQmATj/UANYA0wDKgCQ/vwBsAOw/1z/DPsG/8b/iv3SAob89ACSAIT/xP8a/8z9yP54/Zz8GACWAGoAkgKuAuIAzgJq/4b+lv5e/uD8oP/0/gYB0APsA74EMASEAroBfgA+/ur91Pse+5T7WvtY/YT9fgCKA6wCdAXeBooFegNIAVb/dP6i+cz4Avg8+zr8mP6IAsoF0AYyCKYJTgVmA67+vvto+vz44vYo+TD87gAuAqoF3AhaCOAIhAb4AmwALPvu+Bb50vga+Ab4RP3SABAEGAXQBAgEkgWYAyIBmv98/Kb7HP6A/eL9SgCwAQ4DjgAu/8T+oP62/cb84vuO/ED9rP4AAE4BBgGGAXoCcgHCAlABDADGAHYA4v7kALr/+P+OAnb/SgHMADoA3P+o/vr9FP2I/aT8Uv+c/sT/4gGcABYD3AImAbgAmP/W/jj/1P+K/sICpgE6BDYEogJUA7z/0P/o+6r96vqi/fD8QAB0/9gBqgTWAPYE7v+gAej/CgA4/bL+MP6O/uYApP7AAOL/AgJ8AK7/Qv/E/oT/cP8e/Vb93P6cAOb/KgLIArwAPgNMAZQCogBW/dL+nPwQ/FT/yvuq/ooACgGsBL4DlAUwAvAAdgC2/5z/Xvwy/LL4DP4KAtb/lv9k/mwDpAJSA9z/qv2uADwBpv8u/QT+FPvI/94AYABeAfL/zgPiA3YETAGC/uD+kP8K/u77UvxC+qwAZAEqAIABuP/iAXICQAScAaj/0v/iAB4A+P76/VL7eP/u/zoA6P/0/kQBoAJAAlAAJP4E/dL+GACk/nL9zvuY/wwDEgHOAcQA2gLYAnQDUAA+/+wBnP+iADIAGgA4/N79TgGI/mD+zv1m/iIBgAFsANIACgF2//7+Yv8i/1b8Mv34/h7+8v6i/S7+agACBKYBmAG8Ao4BngEKAm4Cbv5q/wj+dv5+/dr6aP2w/jYAuv5EACYDOgTSAhQBngAQACL/WPwQ/Zz9DP+s/yoBJgJCAsgB5gCCAND/pP5+/vb+Pv8WAZwAmgE2AbAASAAIAH7+Hv/e/lz/QADwAHwC2AEsAyICagLuAez/oP1q/Wj9qvw8/Lj9qP/kAVwC6AJCBUIE9AN4AgoBoP60/Z77rvmm+1z7OPwK//wAxgO8BgwFjgXgBfYBaAEU/UL6RPk++BD7Ivzk/fb/bATcBcIG0gWKBBoDsAAW/9j8mvzW+rD7iPv2/tIASgGYAtACyAMgBBADCgCu/0D/4v78/ST9dP1Y/z4AtAHeAR4CJAPiAaYBVgAM/1r+kv1I/TL9wv3y/XL+MAC0AHABWALeAgACpgDyAAQBBgGg/3D9QP6Q/6D/UP+c/ob/nAAcAeYAaAG6AcwBygH6ALoAmP/K/oT+2P3A/Vb9Jv0q/j7/wv9yAL4AOgHiATQB1gAcAHL/Pv/c/vL+Av+E/zAAXAFuAawBLgK0AXgBNABQ//T+4P6k/VL9uP42/9r/ngBSAY4C6AKmAv4BNgH4AD4Ahv/u/sD+cP7G/lr/JgBYAPwA9gAOAf4AhACAALT/XP+q/wAAAP/2/iT/0v/8/8z/6P8UAGwAHAHSANoAcgEIAYYBFgFa/7L9yP7M/gz+Jv6I/oL/pAD6AOoAVgAyAWIBqgASAZD/OP8W/9j/6P7S/gj/yP5gAHoAgACaAFgBqgGwAGIAfP+2/oT+5v32/b79fP5S/l7/VACCAK4AVACmAJT/fv8o/jj95vy8/LD88Psy/AT8GvwC/Tb+Ov+8/8j+xP6q/9IANAEAAPj+9v5K/0D/wP5I/pr+HP/I/3IADAF2AVwBMgHeAfABrAHkAJIAJAFyAeIBPAKgAsoC2gJGA84DnAPKAg4CHAKEAgQDlANIBAoF0gXUBlwHVgfYBjQGfAXaBGIELgQiBBIEIgREBKgE+gTOBBIEFAMqAgABoP9O/qL9VP0G/SD9sP0Q/1gA5gDcANwA/gBeAOz+LP3g+/j6Pvqq+Xb5pvkm+v76HvwC/VD9QP0k/S79xPza+8T6JPoC+g76KPpg+vr6tvtC/Iz8jPy0/JL8RPzk+9r7DPxU/O78yv12/t7+SP+C/1j/HP+U/qT9Dv2y/DT8mPva+xT8YPyi/Eb9iP0U/hj+6P0k/Jj52PiI9vDymO/E7cbrfOsk7Vju1PA4+l4Lrh2QKEAuEDVQPZw/KjboJU4VEgjo+hjtBuHQ28TdYuOS6wb3tARYEOIX4hvkHWwdiBjqDogErv2M+sr45Pbo9Xr46P3+AqIEwgM4AmYA1v3k+fT1CvOY8nr03Pe6+8r/VAQ4CKIKdgowCGYEBgBK+2D2YPL279LwgvR0+Rr+vAJWCEYNyA9+DuoKbgYCAjL94Pe68wDyjPOA9vD5oP2sAfAFRgnMChgKNAjOBT4DoADe/UL7CvlG+Aj5nvoQ/AD9KP7w/+AB9gLSAggCVAHMAOD/PP4+/Mr6TPo++hT6ZPk2+cD5+Prm+5b8GP2K/Wb+kP5c/oD9nPxc+wb6vPiO97z2Dva+9hz4TPok/CL+gAAoAooCqgA8/nr8dvpA93LzlPDA7xbwkPAw8Qbx9u/C787ypPVc9bj1sv5kFE4rMDb6NeI3+j8wRHw43iDUCh785O9C4IbSMND82dTntvPi/dYLKBwSKB4pqiK4GmwUfg1oA3r4LPEI8pb3Dv2+/wICgAU0CWgJCAQa/CT1fPA67R7ruutq71r2mv46Bq4MXBGGE5gRggxUBSL+Kvfq8Obrmul860DwzvZy/ZoDPgnSDToQtA5cClYEdv4y+aL0GvHs7sjvnvPc+QAAEAUeCYwMCg8iD2wMrgcuA3z/rPyI+vj45viw+ob+ZgIuBZYGJgeMB0wH5AXWApr/KP02/Ej8Zvy0/Fj93P5qABoBxgDg/xz/XP5S/er71vpw+qr68Po0+4z7Kvy+/Lr8FPxW++L6Yvr8+VL5KPnU+e76bPy2/fj+7P/YALYApP+W/jL9ZPss+Sj4eveO9uz1xvV+9lb3xviu+Zr52PiS+bL5Lvhq99r0lPHA7y7xdvLI8nD1pgBuFGAkMit+Lb4yyDn2OvAv9BsKCXj8CPR66tTfXNmk28bmyPVwAq4KRBFGGRAhEiQyH04V3AvWBQYCYP3M97Lz6PPw9+T8vP8kABIAyAB6ATAA+PyI+Yj3YPcy+Ej55vq2/WoB8ARGBwYIugfSBg4FuAFY/Sb5SvYs9fT0TvWM9lD5HP0WAQ4EhAXUBYIFjgQ+Asb+HPu4+Mj3yveO+CT6hPwC/4oBgANyBGYEggPsAV7/nPx0+tD5sPpo/Nr+NAKwBjoLyg6KEIwQCA90DMYIygM0/hj50vVa9Lj0CvYu+FT7VP+CA7YGOAgACPgGnAWUA3YAsvxg+Xz3Jveu9/z3evjg+Uj8rv4GACoArv8s/7j+/P2+/GD7kPqM+vT6Uvta+1L7lvsc/JT8fPw0/Ib7ZPu4+5b8UP1W/eL9pP4MAH4ADgAu/nT8/vse+4z5hvfw9Vz1fPVY9Y71dvaw97D5Ovpo+Uz6Xvww/VL7yvz8BfIT5h1uIRQk+iiELlouLiVaFY4FGvqW8rbr9uMc3jLesOaS9NgB8grwEJIWXBxaIOgejBf0DMQDVP5a+3b43vQC85L0gPmo/pYBngEmAAz/XP5q/Rj7VPgc98z4lvzcAMYEwgf2CX4LGAyOCnIGjgCa+vr17vIa8XjwhPFo9PT4hv6OAzYHSglMCkYKMAnKBhoDOv/g+5z51ves9hr2bvbI96b5tvty/S7/xABAAjADNAOkAhYC6gHAAaABSgEQAVgBMgLuAlIDfgOIA/IDZARuBLoDpgLQAYIBgAFEAYoAvv9k/5L/zv+I/+r+fv6y/nb/+v/g/5D/hP/G//L/oP96/jT9QvwU/Er8Nvy8+yT7cPuE/MT9Mv7g/WT9Uv2I/Zz9JP0c/DL7Dvue+zb8XvxM/Jz8gP18/hL//P66/lz+EP6O/Qj9iPyY+7T7RPzq/Ar9VP1c/WT98P26/U793Pzk++76NPp0+b74UPgs+Bz5qvmi+Oz4yPrs/EL8XvzMAWoM4BbEHTgiRiUsKJwokiTsGtgNmADU9fbtFujc4xri5uQi7Xr4qgOaDOYSTBfAGiAcqhkYE1AKZgLg/Gz5pvbE9DD0CPa6+f79AAHgAUwBAgAO//79mPwc+3r6MPt6/TQB+gR6B4AIxghgCOQGsAPs/rL5cPXo8kbyBvOG9PD20Pq+/3YE0gdiCWAJkgg6B+oEjgGm/Vr6GvgE97D21PaU9+r45Prs/Mr+IADQAOwA2AAkAbABFgJAAsACzgMeBQwGLAY2BbwDUgLoAFr/XP2E+5L67vqQ/Lb++AAyA3QFcgeyCNIIogeWBRIDzACq/vj8rPv0+jz7LvyE/ZL+SP9q/1j/Bv9W/nj9ePy0+yD7VvtC/I79zP7g/9YAmAEAAnoBHgAQ/rb7tvlq+Mj3WveA94L4nvpa/cr/dAFyAgAD4gIeAqoA8v4M/Vb7HPq2+RT6qvqc+6L8eP1G/gD/VP8y/+L+Kv4K/ab8Zvx8/Ib8hPwG/TL9cvyk+9z6BPrw+Uz4EPYa9s74mvvC/kwEcg3EF/IeJiPKJTomaiN4HWAUqglw/pz0eu1e6TrooOl+7fLzxPzsBZINuhKcFYwW3BUoEzwOEAgcAnT9LPpO+Hr3hvdy+F76rvzE/vb/LACu/7z+xP32/GT8CPwK/JL87v3q/+wBTgPqA+IDQAM6ArAAUP5m+8r49vYk9iT2sPby9z76bv3sAEwEwAYYCJIIXAgIB1QE8ACO/Zb6TvgG97T2aPcE+Wr7TP4WAXgDOAU+BlwGiAU2BMACbgH+/8b+PP5K/tD+hP96AFQBCgJ2ArYCvgJYAogBmAA+AE4AoAAcAdQBrAKEA2QE8ATUBAwEwgImAV7/gv3M+2T6ivlE+YT5nPpW/Dr+CABwAWYC8gL8AioCngCM/nT8zvq0+Sb5svjm+Aj69PsG/sD/6ACUAeABqgHgAGz/qv0i/Pr6RPrk+Qz6pPq0+/r8NP42//7/igB4ABYAjP/w/vj9Ev1W/Az87vvW+wz8uPyE/bj9BP5u/tb+Mv8Q/1L+vP2O/Zj98vwO/Lj7bvuO+iD6Fvoa+qr6Lvz4/hwDNgg0DoYV6hu0HxIg5B32GhQXThAyB8L9DPa48Krt9OyC7uLxpPZ+/BADZgkuDtIQNhFwEPoOoAwUCQAF/AAM/pL8Cvy++4D7pPti/E796v3K/TT9sPxY/Dr8YPwK/SD+rP9YAeACPgRYBdQFGgVQA+wAnP54/Ez6TPjE9lD2JPfu+AT78Pzm/gIBNAOaBOgEYgROAw4CigAM/zj9gvtW+hr6lvpw+9b8DP7c/4wBKgPmAygETgSqA6YCLgF+ADD/9P0K/cD8wPz8/HL90P34/hwAwADqAAwCxAPABAoFggXSBWgFuASCBIIE8gN4AiAB7ADoAAwAcv7+/CL8qvss+776lvra+mj7MPxq/db+AgD8ALABIAI4AugB6gB+/7z9/PuW+oz53vgi+Az48viW+lb88v1q/7QAogEAAtoBKgEIAMr+eP1Y/Hj7Hvsk+5b7UvxK/Wz+jP98AOAAGAE4AUAByAAEADT/vv5m/sr9Sv1A/W79QP0m/Tr9pP1Q/oz+Ov4E/ib+iv5s/tz9gv30/CL88vvq+277ZvtQ/Az+tAA8BA4JSg9MFZIZjhtMGwgaehdyEn4LlgMS/MD1cvF277bvhvHs9Kz5lP/mBUoL1g4iEBoQEg/sDHwJRAXqAFb9OPtW+iz6ePpO+778TP6M//j/zP9O/4j+gP1s/Nr79PvU/Pr9Lv+EACgC0gOmBF4EJgOoAQIALv42/Bz6hPjg90r4TvmI+v77xP32/+QBNgPaA+wDjgO8ApAB9P82/rb8vvs0+wj7VPsO/F792P5GAHIBZAIyA7ADzANeA5wClAGoAAYAeP/g/nD+Ov5e/vr+3P+IANwAMgHCAVoCiAJQAsQBSAH4AOQACAFaAboBFAKuAjoDegNiAwwDcAJSAdL/LP64/JT7jPqS+Tb50PkY+9j8fv7q/zwBXgIIA/oCDgJsAJz+8vyg+076Svn4+Gj5dPrU+0T9kv6e/1AAsACiACAAaP+A/pL9sPwy/BD8QPyM/PD8iP1c/iL/gP+6/97/9P/E/1b/4v6g/mz+Iv7o/bD9Pv0y/XL9Rv0C/Vb91v0A/oj+7v4+/5r/JgC8AJ4A6P9g/xT/ov5k/uT9xvx+/Nb9iP/kALQCKAbcCqYPkBOGFs4XVhe8FfISqA6MCJwBPPsy9uLyMPHg8BzyRPUg+sL/KgWuCTANog/WEDoQ0g1KClYGUgKu/n77GvkW+GD4ivkK+4z8Av5i/0AAQgCE/zT+9Pw2/Mz7rvsM/Dj99P4OAeACCASGBIgE6gNEAv7/XP0W+1D5IPiY9+j3MPk2+9L9MgAqAtwDIAWiBfYEdAOWAab/yv0o/BD7WPpE+q76zPtW/dT+MgAAAegBWAJ2AsgBNAHqAI4AIACa/+r/MACIAOwAyAFSAnYClALOAhwDuALyAZgB7AEsAswB9ACgAPIAFgF0AIb/yP5w/ib+yv2w/cr9AP44/oT+Dv/A/zgAagByADoA4v9M/3L+iP2M/Lz7PPsY+zz7avvO+4r8iP2G/mj/AABSAFIAHADc/3T/1v40/q79Wv0i/Q79Iv1c/Y79sv3o/UL+hP6O/o7+sP4E/1r/gv+O/6z/vv/e/9L/uv9q/yz/IP8I//r+zv72/h7/av94/4j/lv+0/+r/1v+I/0T/Jv8u/1L/KP/A/v79hv2Y/cr94Pyi+4b79vzs/rwAFgMaB3YM8hDkE1gVHhYqFkIUShBGC7AF+P+Q+kb21vMa86zzgPVu+Mr87gGKBtYJEgxgDbQN7gy4CoQH3AOsAB7+FPxo+lb5LPnQ+fb6LvxU/Ub+5v46/zL/4P5a/ub9qP2e/cj9Lv4E/wwA4gBiAZ4BbAHuAAoA0P5O/cr7lvrs+cj5AvqQ+pT7FP28/lwAoAF4Av4CKAPOAtwBmAAq/8j9vvwa/Mj7tvsi/CL9mP4eAHABoAKGAz4EagT2A9YCjgGiALT/oP6E/VD90P3C/r7/2AAYAiQDPgQIBVwFvASUA0QCQgFcABr/5v1O/bD9NP6s/gj/nv+kAGgBqgGuAbYBegH+AD4AbP+o/tL9Iv2e/E78LvwY/DT8lvwk/bj9Nv6Y/uT+Dv8i/yr/+v6Q/ib+yv1+/RD9nvyE/J78rPy8/Ar9qP0Q/lb+mv4g/57/8v/8/9L/sP92/07//P6u/mb+MP4y/k7+nP7g/kj/mP/C/yoApgDaAIwARgAEAMD/jv9A/9D+mP6a/ub+GP8W/y7/RP8+/07/IP9A/rT9xP0E/oL9XP0W/xoCQgVyCIQMmhAcE8QTYhOcEoIQeAwmB9wBVv2S+Xj2HvSU89T0pPc0+xr/MgNYBxILgg1UDoYN0gt0CYQG+AJE/xL89PkQ+ez4PvkG+nL7XP0+/4AA9AAMAfoArADw/9r+4v1M/U79ov0M/pL+Rv8uAM4A+ACWABgAdv+M/nb9QPxo+x77YPva+3D8Uv2Y/hQAPgHWAfIBwAFMAYgAlP9K/vz8BPym+8r7Mvzo/Mz9IP+GANgBwgJYA74DzgOOA/ACVgKEAbYAFgC4/3L/OP8g/0j/uP8qAGwAkAD0AIIB8AEGAgoCHgI8AjgCMgIkAhgC7gGgAWIBIAGuABIAdv/m/n7+Dv6a/Tj9+vzg/Mr83Pwa/XD90P0m/n7+0v4E/wL/4P6e/k7+Bv68/Yj9PP0C/Qb9Mv1W/XD9kv28/cb9vP20/cr95v0G/ib+VP6O/tz+Pv+M/67/rP+K/2D/OP/4/rj+gP5q/lb+gP7I/hr/Uv9g/5b/sP+I/x7/qP4y/tj97v36/e79Kv6I/iz/wv82AFwAAAB4/4L/cv+a/ub90v2G/v7/IgLEBDgIKgzUD4gS6hN+FCAU/BFcDuoJKAVUAKj7xPcc9cLz0PP89Ej3rPrK/u4CgAZGCT4LWgxqDFgL+AjeBbYC8v9+/Xb74vkG+Sb5GPpy+9T8Cv4U//j/fACSAFAAzv9A/9D+jP6G/sT+Qv/S/2AAzADuAMQAbAC8/4z+NP3++zL7xvqo+tz6jPuy/Cj+1P8qARoCugIKA/oCVAJGAQQAvv6q/er8hPw6/Ez8qPxq/WL+Uv9CAMgAYAHGAQACtAGCAZABkgFeAcAAngC8ACIBZgGgAYABegEYAgoD3APUAzYD5gJaA5QD3AJiASoAdv/O/sb9rvzU+zz7BvsA+4j7XvxQ/Tr+KP9MAGgBFAIgAsQBKgFSAED/4v2U/ID7zvqC+oj69Ppy+zD8LP08/hT/rP8OADwAHgC4/zj/sv4o/rT9WP0w/Sr9XP2q/ST+pv4U/1z/jP+8/67/jP9Q/yr/7P7G/rL+wP7M/sz+9P4q/z7/Nv9I/zr/MP9G/1b/Iv8i/xj/OP86/0z/kP9W/7L+Wv54/lL+/v00/bb8eP1A/yABdAPIBiQLaA+IEsYUUhYmFhgU0BCuDAAIZgLo/Fb4MvV+8/LypPOk9SL5dP3YAaYFqAj+CpAMHA0WDMYJxgbOAwIBdv4i/CT6Ivky+RL6NvtI/E79YP5i/+z/+v92/87+QP68/VD9+PwS/YL9Vv4k/9T/bgAEAXIBRgGYAIL/ev6Q/az8wPsI+9r6RPs4/ED9Ov46/0oAXAEMAjgC8gF+AeQARACe/9b+Jv7C/cT9Ev6U/iz/0v+OAFABAAJ6AtoCDAMeAxwD7AKqAjwCwgFAAbQADABo/+D+hP5S/mj+zP5w/0oARgFeAloDDgRiBGIE+gMyAwwCkADs/lL9FPwq+5L6Rvo4+rr6zPsW/Ur+Qv8UAMoASAFcAQIBQABc/3T+lv3i/DL8qvt++7D7APxk/Nz8cP3O/Qz+Ov6E/rT+yP64/pr+hv56/oT+Zv42/vz93P3C/bL9tv3o/UL+oP7i/kr/wv8sAFAAMAASANr/bP+6/h7+kv1Q/YD9tP3Q/Sz+yv6Q/w4AFgDq/2T/jP4O/pr9wPxY/D79PP/QAfYEHAlIDi4TVha2F6gXwBaAFDoQhgpKBFL+zPh49LbxsPAO8fzyMPaS+sD/ygT0CNoLxg24Dm4OugzQCQ4GSgIi/6j8lPoQ+Vj4zvgs+uj7YP2Q/nb/HABqAPz/FP8I/kr95Pza/CT91v0I/3AAxgHOAoADnAMQAwgCWgA2/hj8VvoE+T74EPig+BD6GPxW/ooAUAKYA3YEuAQmBJ4CrACw/vT8hPuE+gL6DPru+mr8aP5yAD4C0gMmBTQGiAYyBkAFLAQgAxwCGAEAAGT/OP+8/3QANAHkAZgCcAPkA84DFAMsAhgB5v+Q/jr9Vvz0+yj8jPwS/dr9vv6w/0gATgDS/zD/hv64/ab8kvv8+gj7jvs8/OT8pv2O/o7/XACeAFoAxv8a/1T+YP1e/JD7+vqk+qT6OPtE/Cj9BP70/jgAXgEYAhwCvAFOAa4A6P/M/qz9uvxM/Dz8SvyU/AD9Av40/0AA8gCEAQ4CTAIkAjgB/v8y/2j+XP2E/OT7CPzq+5j7svum/Ir9CP7s/Xr9bP4cAHIBLAJKBEQI6AzAEOAT3BZcGNwX4BWeEiIOIAhQAf76CPZi8uzvwu5Y727yHveU/OIBpAbiCp4OPhHCEQIQsAwGCVgFfAFq/Yz5yvai9QL2Hvdw+OL5uPvc/bD/qACkAFgAFAC0/wD/Sv4U/lT+CP/Y/4gANAHiAYICdAKwAVIAGP/6/aj8Qvv0+WL5pvmw+tz7Fv2U/n4AsAJWBCQFJAXEBBIE+AJeASr/0PzW+qj5LPkq+XL5PPrc+yb+ngDaAq4EGgY2B+IH3gcQB6QFAgRuAuAAkv+G/tT9jP2m/R7+3P6i/zoArgDwAAQB9ACsAD4Axv9i/xj/Av8e/0z/jv+4/9j/5P/c/4T/6P4g/kz9nvwM/Jb7Fvvg+ir75vvG/J79ZP4W/7b/HgA8AAAAeP/G/gj+YP3E/HD8SPxU/Ib87PyQ/UT+1v4K/yr/Sv96/3T/OP/e/q7+rv6+/vD+Bv/u/gr/fP+w/7b/1P/W/17/dv9g/+7+av42/lL+Dv5e/Wz8LvwG/AL8ZPs0+sL5GPve/BD+0P84A+wH/Ax6EVAV0hfOGMQYhhc0FZ4Qigo0BJD+vvm89Vby1O9w71bx8PQQ+TT9bgHaBXYKDA7GD5YPbg6kDFYKNgf0Arr+GPuk+Ab3EvaO9db1OPc4+WL7Fv2E/sb/tgAuAQABcgDS/1z/9v64/rb+8P6i/3gAMgGsAQoCFAK8AfAAuP9A/sD8cvtk+qr5Tvla+fb5MPvg/Mr+kAAMAmYDlAROBS4FVAQIA8gBogBY/xD+7vxq/JT8YP1s/nb/igDMASoDLASgBHIEAgR6A8YC0AGYAIL/rP5o/pD+6v5m/woABAEGAsACCgMOA8gCOgJcATwAAv/a/fT8QvzO+577tPsg/Mr8hP08/uD+cP/e/ywARAAkANr/ev8W/6z+Pv6+/VT9Mv1C/Vj9av2q/Sz+jv7c/hT/bP+6/9r/rP9M//z+kP4q/pj9FP26/MT8JP1c/dT9ev6q/6oAUAHYAToCYgIMApYBeAAS/6r9SPya+nb5vPh8+Er4UvgG+er5EPtu/Ib9sP2W/vz/EAHEAOwAEAMYBrgIGguWDkISlBT8FLIUahT6EmYPWAowBXwAHvz29/TzXPGo8M7x9vP09oj62v7KA2gIHgx4DswPIBCAD7INzAr0BrwCzv5m+2j41vUC9Fbz4PM29QD3CPk4+4b9jv8CAdwBTAJKAgICdAHIADYA2v/K//T/WADYAFYBpgHsAegBXgFoABz/uv12/ET7IvpM+Rr5yPkw+778Xv5AADYCBAQmBXwFBAUoBPgCkAECAFL+Cv1k/Hr8BP3k/fr+QgCwAcwCgAOMA0ADvAL2AfQAuv/A/hT+/P0o/pL+KP/w//wA4gF+AqgCvAKyAn4CEgKOAUABKAFCAVgBaAF6AZYBmAEyAWAAPP8C/tD8nvto+mb5Bvle+UD6Xvu2/Fb+DAC+AQgDvgPMA2oDrAKiAVYAyv5c/Sr8ZvsE+xT7ePsM/Oj8+P04/0AABAFqAYQBZAEKAXIAov/G/gD+hP1I/TT9SP2C/Qr+nv4e/5b/EgB2AKIAsACGACgAgP/K/sz9Av1E/Kz7vvos+nr6ovqQ+tD62PvA/ML9zv5SAPIBNgT0BmwJlgvQDfQP+hASETwQLg/EDXILWAgmBRQCMv+e/GL6fPhM99j2Avei96D49vm2+8D9vv+aAVAD7ARiBnwH+gf8B3YHkAY4BaYD4gHs/wD+SvwW+x76dPkq+Tj5gvn4+XT68vqG+yz85Py8/ab+ov/mAFIC6gNwBbIGmAcyCHQIMAgkB2oFQgPwAHD++Pu6+br3Yva+9fb1wvYm+Pz5CvxI/ogApgI+BGQFKAaOBp4GNgZkBUQEJgMiAigBGAAo/4T+EP7k/dj9AP5e/t7+gv82APAAlAEWAlACUgIyAvgBfgGyAMb/7v4+/oz97Pxq/Cb8Kvxo/Lj8KP2+/Wz+Ev+y/z4AugDwAOoApAAyAJz/wP7S/cj8wvvQ+jz6/PkI+k765vr6+2T95P5AAG4BegJcA8gDtAMqA14CZAFMAB7/EP5K/bz8dPyK/Bz98v3W/sb/wAC6AZYCQgOoA84DvAN+AyYDyAKAAkQCHAL6AfwBNgJ+AqoCqgKeAnACDAJ6AaoAxv/e/ij+tP2Y/bT9GP4E/2YAFgLSA0oFeAZkB/IHAghWBwAGNgQ8AiQAGv4y/HL6Ovmg+Lb4Pvku+nb7+Pyq/lIA0gHgApoDAAQeBOgDTAN4Ao4BvAD6/1j/zv58/nD+gv6s/sD+1v7u/vb+5P6g/l7+NP4y/kj+fP7c/lj/AgDEAHwBAAJSAmoCQgLsAU4BgACC/4D+mv3W/DD8tPt2+3j7yvtQ/PL8rv1y/jb/8P+KAPwAPAFSAU4BQAEeAeIAkgBMABwA/v/a/6j/hv94/3T/Zv9a/17/bP+A/5b/tP/a/xAAUACWANwAHAFeAaAB1AHyAfQB6AHIAZIBTAH0AIwAKADW/5D/WP8s/xb/GP8s/1D/fv+w/+j/JABeAI4AsADGAMwAwACgAHAANgDu/6T/YP8k//j+4P7Y/tz+7P4O/0L/cv+a/77/3P/y//b/7P/a/77/mP98/2j/Xv9e/2j/hP+o/9D/+P8aADoAUABWAFAAOAAYAPb/1v+4/6L/mP+e/7D/xv/k/wgALABIAFQAWABMAD4AKgAQAPz/6P/a/97/9P8SAEAAcACkANoABgEgASABEAHqALQAdAAqAOD/oP9y/1r/Vv9i/37/sv/0/zgAfACsANIA6ADwAOQAwgCMAFYAIADs/7z/kv90/2r/dP+C/5z/vv/k/wYAKAA4AEIAQgA6ACgAEgD8/+D/zP/G/8z/1P/m//7/FgAqADYAPgA8ACwAEADu/87/qP+E/2T/Sv8+/0T/VP9m/4L/pv/U//z/FgAqADYAOAAuABoA/v/a/7T/lv98/2j/YP9e/2z/hP+i/8T/5P8AABIAIgAqACgAGgAGAPL/4P/S/8T/vv/A/87/6P8AABoAMgBEAFYAXABSAD4AHgACAOb/yP+s/5T/jv+a/7L/yv/q/woANgBcAHIAfAB4AHAAXAA+ABoA/P/a/7z/rP+e/5r/nv+u/8L/2v/0/wYAFgAcACAAIAAUAAYA+v/u/+b/6v/s//j/AAAEABgALAAyADAAMgAqACQAFAAEAPL/4v/S/9b/5P/k//T/AgAYACQANgBEAEYATABQAE4AQAA2ACAAFAAKAP7/+P/q/+T/5v/s//T/+P8AAAwAFAAeACYAJgAgACIAIAAaABYAEAAMAAgAAgAAAAAAAAAAAPz//P/4//L/7P/o/+L/3v/a/9b/1v/W/9r/4P/m/+r/8P/y//b/+P/8//z//P/6//j/9v/0//D/7v/u/+7/8P/y//T/+P/6//z/AAAAAP7//v/8//z/+v/6//r/+v/8//z//v/+/wAAAAAAAAAA/v/+//z/AAAAAAAABgAIAA4AEgAWABQAGgAYABYAEgAKAAQAAAD8//L/8v/y//L/+v8AAAgAFAAeACYAKgAqACYAGgAYAAwAAgD+//j/+v8AABAAFgAsADwARgBWAFQAUgA6ACQAEAD2/9b/vv+w/6z/vP/M/+r/BAAoAEwAagB6AIQAiAB6AGYAQgAgAAwA8P/u/+j/2P/q//D/AAAgACoAQABOAEgAUABGAD4ANgAcAAoAAAD2//7/AAAGABYAIgAyAEYAWABmAHAAagBeAE4APAAqABQAAADs/97/3v/g/+7//P8MAB4AMAA8AEIAQAA2AC4AHgAQAPz/7v/i/97/3v/e/+j/8P/8/wQADgAUABYAGAAYABAADAAIAAAA9P/s/+7/6v/k/+b/6v/y//T/9v/4/wAAAAD+//j/9P/u/+j/4v/e/9r/3P/i/+b/7v/2/wAACAAOABAADgAKAAQA/P/0/+r/4v/c/9r/2v/e/+T/5v/s//D/+P/8//7/AAAAAAAAAAAAAPz/+P/2//T/+P/4//j/+v/8/wAAAgAGAAYABAAEAAAA/P/0//D/6v/m/+T/5v/k/+z/8v/2/wAACAAMABQAHgAeAB4AGgASABAACAAAAPr/7v/q/+r/5v/o/+7/9P/8/wgADgAWABoAIAAgACAAJgAiABYADgAGAAAAAAD6//j/+P/0//b/9v/y/+z/6P/m/+T/5v/q//D/9P/+/wQADAASAA4AEgAQABAADAAAAPT/7P/o/+T/4P/e/+j/7P/u//T/+P/+/wIAAAD4//r/+v/y//L/8v/u/+7/6v/s//L/9v/8//7//v/+/wAA/v/6//T/8v/y//T/9P/2//r//P8EAAgADgASABQAFgAWABAADAAEAP7/+v/0//D/9P/4/wAACgASABoAHAAgABoAFgAMAAYAAAD2//j/7v/u//L/9v/6/wIACAAMABYAHAAaABgAEgAIAAQA+v/0//L/9P/6/wAABgAQABYAEAAWAAwABgAAAPj/9P/u/+7/7P/2//r/AgAUABgAJAAwAC4ALAAiABgACAD+/+7/4P/c/9r/2v/g/+r/9v8GAA4AGAAeABoAEgAMAAAA/P/2//b//v8GAA4AGAAYABYAGAAGAP7/8v/m/+L/4P/i/+j/7P/0//7/BgAQABQAFgAaABgAFgAQAAAA9v/q/+D/2P/Q/8r/zv/S/9r/5v/w//z/AgACAAQABgAAAPj/7v/o/+T/2v/W/9b/3P/i/+b/7v/0//7/AAAAAAAA+P/u/97/4P/c/9b/0P/O/9b/3v/w//z/AAAEABIADAACAOr/4v/o/+T/9P/2//r/8v8KABQAEgAgABQAGgAKABAA/v/k/+z/6P/u/wAAAgD0/wQA+P/m/+T/0v/W/+T/7P/u/+7//P8IAPr/8v/2/+7/8P/o/9r/3P/Y/9b/0v/M/8j/xv/E/8j/zv/M/8T/wv/E/87/yv/M/8z/2v/q/+j/8P/w/wQADAAQABYAAgD+//r/7v/o/9r/yv/A/7z/yP/W/9j/6P/y//j/9v8GAA4ABgAAAPz/+v/6//r/9v/8/wAACAAIAAQAAgAAAP7/AAAAAAAAAgAGAAwACgAGAPr/8P/m/+b//P8MABwAKgA8AEQAOgA2ADYAOAAqABYAEAAcACQAHgAcABgAJgAoADAAMgAWAB4AHAD4/+r/4v/i/97/0v/i//7/HAAsAFAAbAB+AJoAigBwAEoALAAaABAACAAEAAoAEgAaABQAEgAMAAAA9P/u//D/8P/6/wYACgAUABAABgAOAA4ACgAOAAoADgAYABYAFgAQAAoAAgAAAAgAEgAWABwAKAAsAC4AIAAKAP7/+P/y/+j/5P/m//D/AAAMAAgAEAAWABQAIAAkACgAPABMAEAANAAgAAgAAgAEAAwAAgDy/+r/9v8EAAYAAgAAABYAKgAsADgATgBGAEwAPgAiAB4ABADs/+L/1v/e//T/BgAiACwANgA6ADYANAAyAC4AKgAgAA4A/v/w/+j/3v/Q/8j/wv/K/9L/4P/6/wIA+v/s/+L/2P/a/9j/2P/i/+T/0P/M/8L/sv+o/67/wP/C/8b/3P/8/yYARABCADQALAAoACYAKAAgAAIA7P/e/87/zv/A/77/vP+6/8j/2P8AAEQAbgCAAJAAgABsAE4AMAAUAPj/9v/+/wwAKgBKAFwAaABOACwAFAAAAOz/3v/G/7D/vP+8/8b/1v/c/9z/AAAeACIAEAACAPT/5P/O/7T/rP+0/8D/tv+y/7D/vP/S/+T/9v/6/wAAAgD8/97/yv+u/6r/sv+0/7r/vP/S/9j/4P/m/wYAHgAqAEIAVgBgAE4ANAAWAPT/0P/W/+L/7v/2/wwADADw/wAAAAAEACAAHgAWAC4AQAAyAAAAtP+C/3D/av94/6r/2v8EABoAIgAgABAA8P/m//L/+P/+//j/7v/0/wAA6v/O/7r/yP/Y/+z/9v/y/+7/6P/W/7T/pP+e/7b/2v8AABAAKABKAFoAXABOADYAGgAeACoALAAsADAALgAgAA4A9v/o/+b/5v/u/wAAGAAyAFYAXABWAE4ANgAkABYAEAASACAAMAA+ADoAMgAkAB4AGAAKAAwAFAAUABIAHAAkACAAEAD6/+b/0v/I/7r/zv/w/wYAHAAqACoAJgAgABQABgD4//j/AAAAAPz/9P/o/97/2P/U/+D/9P8AAAoAFgAUAAoA9v/S/7j/ov+a/7T/0P/0/xAAHAAaABoABADe/77/uv/S/9r/3v/s//j/7P/c/9T/0v/S/8b/yv/u/yIARABOAE4ASgBCACgACAD6//z/BgAaADQARgBOAEIAKgAaAAoAAgAEAAoACgAKAAwACgAEAPj/4v/Q/87/3v/0/wQAFAAaABQAAgDq/8r/xv/O/97/9v8OABwAJAAeAAAA4v/M/8D/sP+0/8z/7P8OACwAKAAaABQAFAAOAAAA9P/i/9z/0v/M/8z/0v/M/7b/sP/S//T/CgAwAEgAXABqAGQATgAkAAAA6v/e//T/DgAkAEgAcgCCAIYAZAA6ABoAAADw/9z/xv+4/7b/tv+4/8L/yv/Q//7/HgAsACIAGAD+/97/wP+k/5D/iv+M/4j/oP+s/7j/xv/e/+7/7v/0/wAABADs/9b/wv/I/8T/sP+e/5r/sP+6/8j/3P8EABgAJgA0ADQAPAA2ADYALAAMAOr/8v8GAPz/CAAMAAAA9P8SAC4APABMAEoAUAB+AI4AbAA8AAgA7v/E/7j/uv/Y/wIAHAAsAEYAUAA4ABgAFgAyAEIANAAoACQAHgAgABoABAD2//L/8P/6//L/4P/Y/9D/xv+4/6z/tv/K/9z/8P/+/wQACgD+//j//v/+//z/DgAeABwAFgAQAAoA/v8AAPb/9P/4//L/6v/q//D/9v8EABAAHAAwADYANAAiABYAEAAQAB4AKAA2AEAAQgA4ADYALAAmAB4ABgD2/+7/8P/y//D/6P/i/9b/0v/K/9b/8P8KACwASABKAD4ALAAUAAAA7P/Y/8r/wv/E/8L/yP/S/9j/4P/u//z/BAAKAP7/7P/g/9b/wP+0/67/qv+y/8D/2P/s//D/4P/k/+L/zv+2/7T/xP/M/8b/yv/I/8r/1P/U/9b/5v/2/wYAJABIAF4AVgA4ABwAFAAIAAYAGAAwAEoAVgBaAGQAWgA+ACYAKAA0ADwASABGACwAEgD4/9b/yv/M/8j/2v/u/wwAJgA2AEAAPgBKAFIAUAA6ADQAKAASAPT/1v+8/7T/vv/M/+D/+v8SABIACAD6/+b/7P/y/+b/4v/k/9j/yv/I/9L/1P/O/9b/5v/y/+j/xP+q/7D/wP/C/97/FgBMAFYAQAAsACgAGgD+/+r/3v/w/wAACgAUACIAHgAiABgAFgAYAAwACAAAAO7/1P++/6D/lP+e/67/wP/w/wwAHgAgABgA/v/Y/7j/oP+Q/47/nv+k/77/zP/W/+T//v8AAPT/BAASABQA8v/E/5z/qv/A/7b/qv+u/8j/7P8AAAAA8P/m//b/DAAaABQAGAAsAEAAMgAUAAgACAAEAAAABgAKAAYAHAAiABQAJgAqACgAKgA8ADwAMgAcAP7/6v/m//j/AgAaADAAPgBKAEwAPgAqAC4ASABMADYAIgAUABoAJAAYAP7/+v8AAAYAAgD6//j/AgACAPr/6v/Y/9D/1P/u/w4ALgBAAEoAQgAoAAYA5v/U/9r/4P/W/8z/xP/A/7r/xv/Q/+D//P8SACQAJAAYABoALAA4AEgARgAuAA4A8P/e/97/5P/+/yIAPABCACwADgDu/87/xP/S/+b/+P8GABQAHAAYAAwA/v/0/+j/5v8GAC4ATABgAF4ASAAwACwALAAiABYAGgAkACYADgDu/9z/0P/O/9b/9P8MABQAFgAWABQADgAAAOL/zv+2/6D/mv+q/8T/0P/S/8T/xv/I/8z/xP/S/+z//P/2/+b/zP/A/8z/zv/c//D/CgAgAC4ANABEADwAJAAKAAQAGAAmACIAIgAwAC4AGAAGAAIA+v8AAA4ANgBQAFgARgAmABQAIgAmABIABAD0//T//P8MABgAJgAUAAYACAASAAgA+P/0/+j/2P/I/8z/zv/E/8L/xv+2/7z/xP/I/+D/3v/O/9T/2v/o/wIAGgAeABIAAADs/9T/zP/Y/9z/3v/O/6z/lP+k/8r/5v8GAC4ATABYAFYALAAGAPD/6v/8/xYANgAwADQASgBeAEoAMAAOAAAAAgAGAA4AEAAAAOj/1P++/7T/wP/I/87/CAAgADAAKgAeAAAA5v/Q/8j/zv/K/9L/zv/c/+D/4v/Y/+T/5P/q/wAAEAASAPL/1v+0/7r/yP/A/67/sP+0/7j/xv/M/97/8v8AABgAIgAaABAAGAAiAAwA+P/2/wAAAADy/9L/vP+w/9b/AAAWACoAJAAiACQAGgD+//L/4v/a/9D/yv/A/7r/5v8AABYAMAAyADAATgBsAHIAbgBeADQABAD4/wAA8v/k/wAAPABIADwAKgAUAAQACgAeAAoAFAAOAAAA+P/W/8b/4v8AABIAAgD4//z/DgAyAC4AFgD+/+r/zP/K/+j/+v/8//D/+P8GABIAFAAoADQAOgBSAFAARgAoABAA7v/c/+D/AgA0AFYAWgA2ABIA8P/M/7b/yP/Y/+7/8v/o/+L/3P/K/7j/sP+k/6b/yP/y/wgABADs/+T/9v8EAPb/2P/U/+b/5v/O/8r/1v/W/+T//P8OACIAKgAYAPz/2v/A/8L/xP+2/7b/vP+8/8D/1P8AADAAOgAeAP7/4v/c/+T//v8YACYACgDs/9z/5P/m/8z/sv+s/8T/6v8YAEAAVgBEAC4AEAAIAPr/4P/a/+z/BAAYABoAAAAAAAAA+v/u/wYAPABmAGIARgAiAAwA9P/U/8D/yP/k//z/FAAmACgAAgDa/8T/0v/Q/8z/1P/g/+z/7P/s/+b/7v/y/+L/vP+0/8j/2P/s/+r/4P/a/9r/5P8AABwAIAAUAP7/5P+6/6b/oP+m/6j/rP+g/5b/pv/E/+b/CAA4AFQAYABeAEoAJgD6/9z/2P/y/xAAHAAuAFIAdAB2AGoATAAwAB4AGgAMAPz/5v/E/7D/qv+k/5z/qP/I/wIAJgA2ACwAGAD4/9T/vv+6/7L/qv+u/7T/yv/I/7z/rv+4/8z/4v8KADAAQAAuABAA6P/W/8T/rv+i/7r/2v/w/wYACAAEABAAAgD+/wgAIAA6AGQAfgBaAEYAWABiAEAAKgAQAPD/yv/U/wAALAA4ACIAMgBKAD4AIgAOAOj/7P/U/6b/fv+C/7T/5P8CABgAKgAkABAA/v8WADAARgA4AB4AGgAeABYAAgACAAoAAADa/77/sP+2/+T/5P+6/77/2P/o/w4APgA2ADYALAAAAOr/7P8CABYAKgA4ADwAFgAAAPL/9v8IAAYAGgBAAGgAbABkADYA+v/c/9r/6v8AAAwA/v/Y/8L/uv/G//D/FAAqAEAAUABWAGgASgAsABQA2P+k/5D/nv+0/87/8v8UAB4ADgDm/9L/5v8GAD4AbgBwAFoALAD6/8j/nv+g/8D/3P/0//7/AAAGAPr/8P/k/9r/2P/m/+T/2v/Y/87/tP+g/5r/sP/e//z/GAAuADYANAAkAAQA2P+s/6r/vv/E/8b/2P/U/77/vP/I/9z/9v8OACYAJgAUAA4AGAAaABIABgAMACYASABiAGYAagBeAEYAJAAIAPz/FgAkABoAGgAYAAQA1v+o/5r/rP/S/+b/+P8SABgADAD4/+L/5v/8/x4AQgBiAGoAQAAqAAYA2P+2/9L/+v8MABgAJAAoAAIA1v+s/6T/xv/8/wgA9P/c/9D/5v8EAB4AEgACABIAJAAsABYA5v/K/9j/4v/6/yoAVABsAGwAXgBCABwAAAACAAwABgDy/+r/6P/o/+z/AAAwAEwARgA8ADoAKAAAANr/wv/U//b/8v/a/9T/4v/u/wIAGAAyADoALAAgAAAAzv+c/3z/dv+U/8T/1v/e/+7/+v8KAA4AEAAEAAoACgAAAOb/5v/a/6z/kP+o/+T/DgAqABwAAADi//D//P/w/+T/1v/s/xAABADU/9b/4P/q/wwANABQAFgAcgB4AGgATgAuABwARABqAGAAPAAUAPj/1v/a//L/+P/+/zYAYACAAIIAYAAkAPr//v8QAAwADgAmADAAMAAUAPz/4v/S/8r/xP/M//L/GAAUANr/kP9s/2r/iv/I/xwASgA8ACAAAgDi/77/sv/I//D/AADw/8z/pP+c/6j/0P/y/xYAQgBeAHAAWgAUAND/wP/a//r/FAAeACoAGAAAAOb/2P/q/wAAIAA2AEwAWABgAD4ACADq/9r/4P/8/y4ATABMAD4ALgAmABYABgASADQATgBkAGgARAAiAA4ADAAMABwAOgBQAFgASAAcAPz/6P/g//L/GgA0ADgAKAAIAPL/zP+o/5L/iP98/4D/nv/K/+b/5v/O/6T/lv+2/9z/1v/c/wAAGAAiABAA7P/e/+L/3v/a/+L/5P/s//z/FgA0ADoAKAAMAPL/2v/I/9T/+P8SACwANAAiAAAA5v/Y/9T/2v/8/yYASABCABYA8P/S/8T/zP/u/xQALgAuACAAFAD6/9r/1v/e/+L/7v8IACAAIgAQAPL/2v/O/+r/BgAMABYAKAAmACgAEADu//D/+v/+/xAAKgAaAAoA+v/u/+z/7v8AABIAHgAQANz/uP/E/+D/5P8MAEQAdACAAGQAMgD0/7L/jv+0/9D/+P8AAAgAJAAmAOr/wv/A/+T/BAAUACoALAASAOj/tv9s/0b/Wv9+/57/AgA2AF4AYABMAB4A7v/Y/8z/0v/K/9L/3v/w/97/vP+a/6r/yP/g/woAQgBcADQABADc/97/5v/G/6b/qP+o/6j/xP/S/+D/AAAOABIAEAD0/+r/AgAwACwAEAAKABoAIAAKAPT/zv++/9D/9v8GAAoA8P/a/9L/1v/k/9b/vv/I/9D/yv/Q/+L/AAAOAAwAEgAsADQALgAyAEYAXgBYAC4AEAAYACwAHAD4//z/EgAcAB4AFgAAAPD/8P/2/+z/3v/O/9j/6P/u/+z/+P8WABwADgAMABoAJgAyADYAIgAIAOr/yv+6/8z/3v/w/wYAIABCAD4AHgD4/+r//v8eADIAMAAiAAoABAAAAAAAHABGAFwATgAyABYABADk/9D/zv/M/8r/2v/w/+7/5v/U/8r/xP+0/6D/uP/g/wAAIAAyADIALAAqACIAEAAAAPz/+v/y/+j/1v/I/8b/vv++/+D/CAAgACoAMAAeAAIAzv+W/3b/Zv9u/5L/vP/m//b/7v/y//j/8v/s/+L/4v/u/+z/7P/y/97/pv+G/4L/lP+s/7T/yv/m/wYADAAKAAYADgAUACAAKgBEAFoAUgBGADIAJgASAAAA/v8iADgANgAoACgANAAwABIA8P/u//j/AAAQADAAOgAyABoAAAD0//T/9v/y//b/BAAEAPL/2P++/7D/uP/W/+7/AAAIABYAGAAiABoAAAD6//7/7v/e/+r/9P8MABIAEgAIAAAA9v/g/8b/rv+a/57/sv+6/7T/vP/q/xwAQgBEADAAIAACANz/yP/E/9z/9v8IACAANAAuADYAPAA8AC4AEAD8/+b/yP+0/7D/kP9u/2T/gP+i/+T/GABIAFQAOgAWAPz/6v/Q/8T/wv/u/wAAAAD4/+z/3P/a/+L/+P8uAFYAVgA0AAwA5P/M/7j/tv/O/+b/+v8EAP7/BAASABoAKgA+AFgAYABiAGAAYABEABYADgAWAA4AGAAmABgA+P/s/wAADgAiABIABgAWADIASAAqAAAA7P/e/8z/4P8MACAAHgAWABoAJAAkABAAFgA0ADYALAAgABAAGAAsACQACAAAAAAA/P/0/+L/zP/E/87/1v/U/9j/9P8QABwAJAAgABoAFgAEAPT//P8GAAoAEAAUAAgA+v/w/+j/4v/y//7/CgAaABYAEgAQABYAFAAUAA4ADAAQAAoA/v/o/+r/9v8KACYAPABGADgAGgD+/+7/3P/a/9j/yP+6/7T/tP+4/7z/xP/S/9z/4P/U/97/9P8CAB4ANgBAAEAAPgAwABoA/v/m/9z/3P/i/+b/4v/e/9L/zP/W/+7//v8OABwAEAD8/9T/oP+C/3b/gP+g/77/3v/4//z//P/+//z/9v/k/+D/5P/c/9T/2v/Y/8D/tv++/9D/4v/m/+b/5v/s/+7/8P/0/wAADgAUABoAIgAqACIAFAAMAAoACgAEAAQAEgAaABIACgAEAAAA/v/2/+j/7P8AAAIACAASABYADAAAAAAAAAAOACAAMAA+ADwAJAAUAAYABAD8/wIAEAAIAAIA/v/s/9D/xP++/77/xP/S/+z/AAAEAAQACgAWACAAEgAAAP7/AAAAAP7/9P/Q/67/vv/q/wQADAAQAA4AAgDw/9j/xv/Q/9b/zv/G/9L/2v/S/9L/3v/6/wgACAAIAA4ABgDg/8T/sv/I/+j/5v/O/8L/2P/U/9b/2v/O/9L/1v/Y/8j/uP+w/7T/xv/W/9T/2v/e/+r//P8IAA4AHAAkADAAKAASAAIABAAWABYAGAASAA4AAADw/97/3v/m/woAMAA4ADoAMgAqABwADgD6//L/AgAMABIAJAAYABgAEgAKABIAHgAoACAALgBAAEAAKgAMAPD/1v/Q/9z/9P8UACwAKAAYAAIA7v/m/+T/5v/2//j/9P/+/wgACAACAOz/4P/2/wgAIAA4AEYARABAACwAHAAKAOr/4P/m//T/AAD8//7/CAAMAA4ADAAKAAwACgAGAAIAAgAAAAAABgAQABoAHAAgACoAMAAwADYAOAA6ADwANgAqABwAFgASABIAFgAeACgAMAA0ACwAJAAeABYADgAUABwAJAAqACoAMAAuACYAHgAUAAwACgAQABgAIAAkABwADAD8//T/8v/y//T/+v8AAAAAAAD8//z/AAAGABgAMgBCAEgAPgAsABgAAgD6//j/AAAOACIANABCAEIAOAAmABYABgAAAAQADAAYACIALAA2ADgAOAA4ADQAMAA0ADQALgAoACQAJgAoACYAHAAQAAIA+P/m/+D/5P/k/+z//P8AAAAAAAD4//T/7P/m/+T/6P/o/+z/8v/w//D/8P/y//r/+v8AAAoAFAAaABIAAgDw/97/zv/K/8z/0v/U/+D/6P/s/+z/5v/g/+D/1v/O/9T/1P/Q/8r/0P/e/+j/2v/W/9b/3v/k/+j/8v/2//7/AAAGABAAAADq/+7//P8IABgAHAAUABIAEAAOAAwABgACAPj/6v/e/+7/7v/m/+b/4v/o/9r/4v/k/+r/6v/s//b/+P8EAAYAAAD+//b/7P/a/87/yv/M/9b/2v/g/+L/4P/m/+7/8P/2//L/8P/2/+7/6v/s/+j/6P/o/+T/5P/i/+b/3v/q/+7/8P/y//D/9P8AABIABgAAAAIACgAOAAwABAAGAAgACAAMAAIA/v/8//z//v/+//z//v/8//r//P/4/+7/6P/s/+r/6P/k/+D/4v/m/+T/4v/g/+T/5P/g/+D/5P/g/9z/5v/k/+r/8P/u//L/+P/8//r/9v/4//7/AgAOABYAGAAWABoAEAAGAAIA/v8AAP7//P/8//7//v8AAAAA/P8AAPj/9P/4//b/9P/4//r/+P/+//z/+v/8//7/AAAEAAIAAAACAAAABAAGAAoACgAIAAYABAAEAAgABgAEAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAD8//7/+v/4//j/9v/2//L/8P/y//D/8v/y//T/9P/0//T/9P/w//D/7v/u//D/8P/w//D/8P/0//j//P/+/wAABAAGAAgADAAMAAwADAAMAAwADAAMAA4AEAAQAA4ADgAMAAwADAAKAAoACgAMAAoADgAOAA4ADAAQABIAEgASABAAEgAUABQAEAAMAAwACgAIAAYABAAIAAQAAAAAAP7//P/6//r/+P/4//r//P/6//j/9P/0//j/9P/2//j/+P/8//r//P/6//b/+P/4//b/+v/0//L/8P/u//L/7P/s/+b/5P/i/+T/5P/i/+D/3v/i/+L/5v/m/+D/5v/m/+b/5v/m/+j/6P/w//L/8P/y//L/8P/u/+7/6v/w/+7/7P/u/+z/6v/o/+j/5v/m/+b/6P/s/+j/5v/k/+T/5v/m/+T/5v/i/+b/6P/u/+7/7P/w//D/7v/u//D/8P/y//L/9P/2//j/+v/+/wAAAAAAAAQABAAGAAYABgAIAAQACAAGAAwADAAKAAwACgAKAAgABAAKAAoACgAKAA4ACgAOAA4ADAAOABAAEgAYABgAFgAaABYAHAAaABwAGgAcABwAHAAeACAAHgAeAB4AIAAiABwAIAAiAB4AIgAiACAAJgAkACQAIAAgAB4AHgAcABoAGgAYABoAGAAYABQAEAAQABIAEgAUABIAFAAUABIAFAAQABAAEAAQABAAEAAQABQAEgASABQAEgASABIAEAAUABIAEgAWABQAFAAWABIAFAAUABQAFAAUABQAEgASABIAEgAUABIAEgASABIAEgASABQAFgAUABQAFAAUABYAEgAUABQAEgASABQAEgAUABQAFAAYABYAFgAUABYAFgAUABIAEgASAA4ADgAMAA4ADgAOAAwADAAKAAwADAAKAAoACAAIAAgACgAIAAgABgAIAAQABgAEAAQABAACAAIAAAACAAAAAAAAAP7//v8AAP7//v8AAP7/AAD8//r//P/6//j/+v/4//z/+P/6//r/+v/6//r/+v/6//z/+v/+//z//P/8//z/+P/6//j/+v/2//b/+v/4//r/9v/4//r/+P/4//j/+v/6//r//P/6//z//v/8//r/+v/8//z//P/6//r//P/6//z//P/6//z/+v/8//r/+P/4//j/9P/2//T/9P/2//T/9P/2//b/9v/2//T/9P/0//b/9v/2//T/9v/4//T/9v/2//T/8v/0/+7/8v/u/+z/8P/u/+7/7v/q/+z/8P/s/+r/8v/y/+j/7v/y//b/8P/q//L/9P/y//D/8v/0//T/8v/y//T/+P/0//L/9P/0//L/8P/0//L/8P/u/+7/8v/2//L/8v/y//T/9v/2//j/+P/6//j/+v/6//r/+v/+//z//P/8//r/+v/6//r//P/6//j/+P/2//r/9v/6//j/+v/4//b/+P/4//b/+v/2//j/+P/2//r/9P/6//T/9v/0//T/9P/0//b/9P/2//T/9v/2//T/9v/0//L/9P/0//j/+P/2//b/9v/4//b/9v/2//T/9P/2//T/9P/2//L/9v/6//b/+P/4//r/+v/4//r/9v/4//b/+P/2//j/+v/+//z//P/+//z/AAD8//z/AAD8//7/AAD+/wAA/v/+//7//P/+/wAAAAAAAAAAAAD+//j//v/+//7//v/4//j/+v/6//j/+P/4//z/+P/4//r/9v/6//r//P/8//r/+P/6//r/+v/8//r//P/6//r//P/6//7//v/8//7//v8AAP7/AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAEAAIABAAEAAQACAAEAAgACAAIAAoACAAIAAwACgAMAAwACgAOAAwADAAOAA4ADAAOAAwADgAMAAwADgAOAA4ACgAMAAgADAAGAAoACAAGAAYABgAIAAQABgAEAAQABAAEAAIAAgAAAAIAAAAAAAAAAAD+//7//v/8//7//P/8//7/+v/8//7//P/+//7//v/+//z/+v/6//j/+v/6//r/+P/6//z//P/6//r/+v/6//r/9v/8//z/+P/6//j/+v/6//r//v/4//r/+P/8//b//P/0//z/+P/w//z/9P/6//T//P/4//z/9v/+//z//P/+//j//P/2//b//P/2//T/9v/2//j/+P/8//b/+v/0//z//v/w//j/+P/4//T/8v/6//D/+P/y//T/9v/2//j//P/8/wAA/v8AAAQAAAAKAP7/CgAAAAQACAAIAAwACgAKAAgACgAEAAYABAACAAQABgAGAAwAAgACAAIABgD+/wQA/P8CAAAA9v8CAPr//v/4/wAA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5P/q//b/AAD8//j/+v/+//r/5v/c/9j/2P/W/9r/3P/s/+b/2v/c/9z/5v/4/xIAHgAwAEAARABOAFgAWABWAFQATABGAEAAQAA8ACwAHgAQAAQAAAAEACgATgB6AKIArgCuALoAxADMAMYAsACeAJAAgAB0AGYATgA0ABoAFAAOABgAKgAwADQAPgBOAGIAdABwAGAAUgBcAGQAYgBOACwADAD0/+j/7v/2/wAABAD8/+7/3v/U/8j/yv/m/wgAGgAYAAQA7v/S/8b/0P/o/wgAFgASAAAA7P/Q/8j/zP/g//z/CAACAPL/4P/O/8j/zv/a//D/AAAEAAIAAAD2/+z/7v/8/xgAMAA0ADAAIAAYAA4AFAAgACgALAAuACwAJgAcABgAHAAgACYAJAAkAB4AGAAUABIADgAKAAoADAAOAA4ADAAKAAQAAAD8//z//v/6//z//P8AAAIABAAEAAQAAAAAAAIACAAOABAAEAAKAAYABAAAAAAAAgACAAAABAAAAPr/+P/0//r/AAAGAAYACgAOAAQAAgAEAAAAAAD6//b/7v/m/+D/1v/U/9L/0P/U/9z/4P/m/+j/6P/s/+z/9P8AAAgADAAKAAAA+v/4//L/+v8AAAAAAAD+//z//P/8/wAABgASABwAIgAkACQAIgAgACYAJgAqACgAJgAeABoAHAAcACAAIgAoACYAKAAiAB4AHAAiACQAJAAiABYAEgAKAAQA/v8AAAAABgAIAAQA/P/w/+z/5P/s/+7/8P/y//b/9v/q//D/8P/u//T/+v/+//z/+v/s//D/+v/6//z/AAAEAAAA+v/0/+7/8v/6//b/AAAEAAIAAADy//r/+v/+/wQACgAQAAoABgAGAAIACgASABgAHAAcABYADgAMAAoAEAAUABgAFAAQAAYABAAEAAQACAAEAAgACgAKAAoACAAIAAgADAAMAA4AEAAOABIADAAMAAwADgAOAAwADgAIAAgACgAIAAQAAAACAAIACAAEAAYABgAIAAwADAASABQAFAAYABoAFAASABIADAAIAAQAAgACAPr/9P/2//T/9v/y//T//P8CAAoACAACAAYACAAEAAgADAACAAAAAAD8//z/9v/2//T/7P/o/+r/7P/s/+r/6v/s/+7/8v/2//j//P/+//z//P/6//j/+v/6//j/9P/2//T/8v/0//T/9v/4//b/9P/2//r/+v/6//b/8v/y//T/8v/y//L/8P/0//D/9P/2//L/+P/2//T/+v/0//b/+P/y//L/9P/0//D/8P/w//b/+P/2//L/9P/2//L/9P/2//L/8v/0//T/8v/w/+7/7P/u//L/+P/0//j/+P/2//b/9v/+//r//P/4//T/9v/0//T/9P/2//b/9P/2//b/9v/4//r/+v/6//r//P/8//z//P/6//j/9v/2//L/8v/w//D/8v/u//D/7v/u//L/8P/y//T/9v/2//r//v8AAAAA/v/8//7//P/0/+7/6v/s/+7/6v/o/+7/8P/y//b//v/+//z//P/8//z/+P/6//j/9P/2//b/9v/0//b/9v/0//T/9P/y//T/9P/4//b/9v/0//b/+P/2//b/9v/6//j/9v/2//b/+P/4//j/9P/w//D/7v/u/+7/8v/y//D/8P/y//L/9P/2//b/9v/6//z//v/+/wAA/v8AAAIABAAEAAQAAAACAAIAAAAEAAAA/v/8//z//P/8//7//v8EAAAAAgAAAAAAAAAEAAIABAAKAAgACgACAAAAAAAAAAAAAgAAAAAAAAAGAAwAAAAGAAYACAAEAAIAAgAAAAAA/v/+/wIA/v8AAAAA/v8AAAIABgACAAQACAAGAAIAAAAEAAAAAAACAAIABgAIAAoADgAMAAYACAAGAAQAAgAAAAAAAAD8//j/+v8EAAwACgAIAA4AFgAaABoAGgAaABIAEgAUAAwACgAIAAIAAAAEAAQAAgAIAAYADgAQABAAFAAUABIAEAAOAAwADAAMAAgABgAGAAQABgAGAA4ADAAMABgAGgAYABQAFAAYACQAIAAeAB4AIAAeACAAHgAcABwAFAAUABIAFAAQABYAIAAeABYAEAAKAAgABAAGAAAA9v/2//r//v8AAAIAAAAGABAAGAAgACwAOABGAEwASgBOAEQAOAAmABgAEgAUABwAJgAoACYAIgAiACIAJgAeABAAGgASAAYACAAUABoAKAAiABoAGAAMAAQACAAAAAAA/P/w//7/BAAQABwAGAAUABIADgAeACYAIgAeABAACgASABoAIAAgABIAAADu/97/3P/o//T//v8EAAgACgAIAAAA/v/4/+7/7P/2//j//v/6//L/8P/s/+T/6P/u//b/AgAKAAwABgACAPz/9v/s//D/8P/2//j/8P/u/+r/5v/g/+j/7v/6/wAAAgAAAPz/+P/u//L/6v/w//b/9P/4//z/8P/y//L/+P8GAAQACAAEAAAA+P/0//D/+P8AAAgAGAAcACAAHgAYABAADgAKAAwACgAEAAAA9P/w//L/8v/0//7/AAAGAAoACAAGAA4ADgAcACYAJAAmABgABgD6/wAA9P/w//L/8P8AAAAAAAAGAAgAEAAQABQAEgAMABIADAAQAAQABgACAAAA/v/6/wAAAgAEAP7/AAAAAAQAAgAEAAAA+v/6//j/9v/2//j/9P/6//b/+P/4//7//P/6//r/9v/6//r//P/4//7/AgAAAAAAAAAAAAIABAAEAAQAAAACAAIAAAAEAAAA/v/8/wIAAgAAAAQA/v8EAAQABAAAAAYACgAKAAoACAAIAAwACAAIAAYAAAAIAAYA/v8AAAIABAAIAAQABAACAAYABAAGAAIA+P/0/97/3P/s//D/7P/m/+7/7v/w//T/9v/6//T/+v/+//r/AAD0/+z/8P/6//T/2v/M/77/tv+2/8L/1v/0/wYAFgAcABQAAAACAPr/5v/a/8T/qv+Y/47/gP+M/67/6P8WADIAOgA0ABAA7P/k/+b/7P/+/xQAIAAWAP7/6P/W/8z/yv+6/6j/tP+k/77/2v/i/w4AIgAeAAYA3P+4/5r/jP+E/5T/rP/M//D/9v8CAAQA9P/o/+D/0v/G/7z/vv/c//L/+v8AAAYAAgD6//D/7P/u//D/+P/2//L/9v/8/wgADgAOAAgACgAIAA4AGgAgAB4AGAAWAAwAAgAEAAYAFAAoADgAMAAeABIABgD8//T/7P/s/+j/3P/O/7r/qv+o/67/tv+4/7T/uP/E/9T/5v/s/+z/4v/S/8L/tP+w/7j/xv/U/9j/1v/S/8z/0v/e/+z/+v8CAAAA+v/0/+7/8P/2/wAABAAKABIAEgAKAAAA9P/u//D/+P8CAA4AEgAIAPj/7v/k/+T/7P/4//z/AAAAAPz//P8CAA4AHAAmABwAHgAaAA4ACgAQAA4AEgAOAAgABgAEAAwAEgAmACwAMgA0ADgAOAA2ADgANgA2ACwALgAmACIAIAAiACgAKAAmACgALAAuADAANAA2ADYANgA4ADoAOgA4ADgAOgA8ADwAPgBAAEAAPgA+ADwAPAA6ADoAOgA4ADgAOAA4ADgANgA2ADYANAAyADAALAAqACYAJAAgABwAGgAWABYAEgAQABAADgAMAAgABgAEAAYABgAIAAoACgAKAAgACgAKAAwADgAQABIAEAAOAAwADAAKAAgACAAEAAQAAgAAAAAAAAD+//r/+P/y//D/7v/u/+z/7P/s/+j/6P/o/+r/7v/0//j//P/+/wAA/P8AAAIABgAIAAYAAgD8/wAA+P/4//j/AgAEAAAABAD+/wAABgAQABYAGgAYABAACAAGAAAA/P/6//z/AAD6//j/7P/g/9r/3v/m//L//P8AAPr/9P/u/+j/6v/u//D//P8GAAgABAD6/+z/4v/k//D/AAAKACAAKAAYAAoACAAEAAAABgAcADIAPAA4ACoAGAACAPb/9P/+/wYACAAEAPj/7P/e/9T/zv/Q/9T/1v/W/9T/1v/e/+j/9P8AAAgACgAMAAYAAgD6/+7//v8AABYACgD8/+z/3P/6//z/9v/+//r/vv+q/7r/uv+6/+D/KgBCADQAGAAuAGAAagCKANYARgF6AVoB8ACMAEAAAADO/6j/jP8AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
  doc["len"] = 45804;
  String requestBody;
  serializeJson(doc, requestBody);

  // Serial.println("requestBody:");
  // Serial.println(requestBody);

  // Send POST request
  int httpCode = http.POST(requestBody);

  // String recognizedText = "";

  if (httpCode == HTTP_CODE_OK) {
    String response = http.getString();
    DynamicJsonDocument responseDoc(2048);
    deserializeJson(responseDoc, response);
    Serial.println(response);
    if (responseDoc["err_no"] == 0) {
      recognizedText += responseDoc["result"][0].as<String>();
    } else {
      Serial.println("Speech recognition error: " + responseDoc["err_msg"].as<String>());
    }
  } else {
    Serial.printf("[HTTP] POST... failed, error: %s\n", http.errorToString(httpCode).c_str());
  }

  http.end();
  return recognizedText;
}

// Get response from Baidu Ernie Bot
String getErnieBotResponse(String prompt) {
  if (qianfan_access_token == "") {
    qianfan_access_token = getAccessToken(qianfan_api_key, qianfan_secret_key);
  }

  // 创建http, 添加访问url和头信息
  HTTPClient http;
  http.begin(ernie_api_url + String(qianfan_access_token));
  http.addHeader("Content-Type", "application/json");

  // 创建一个 JSON 文档
  DynamicJsonDocument doc(1024);

  // 创建 messages 数组
  JsonArray messages = doc.createNestedArray("messages");

  // 创建 message 对象并添加到 messages 数组
  JsonObject message = messages.createNestedObject();
  message["role"] = "user";
  message["content"] = prompt;

  // 添加其他字段
  doc["disable_search"] = false;
  doc["enable_citation"] = false;

  // 将 JSON 数据序列化为字符串
  String requestBody;
  serializeJson(doc, requestBody);

  // 打印生成的 JSON 字符串
  Serial.println(requestBody);

  // 发送http访问请求
  String ernieResponse = "";
  int httpCode = http.POST(requestBody);

  // 访问结果的判断
  if (httpCode == HTTP_CODE_OK) {
    // 获取返回结果并解析
    String response = http.getString();
    Serial.println(response);

    DynamicJsonDocument responseDoc(2048);
    deserializeJson(responseDoc, response);

    ernieResponse = responseDoc["result"].as<String>();
  } else {
    Serial.printf("[HTTP] POST... failed, error: %s\n", http.errorToString(httpCode).c_str());
  }

  // 结束http访问
  http.end();

  // 返回响应数据
  return ernieResponse;
}

#define CHUNK_SIZE 1024
// Perform text to speech using Baidu TTS API
char* textToSpeech(String text) {
  if (baidu_access_token == "") {
    baidu_access_token = getAccessToken(baidu_api_key, baidu_secret_key);
  }

  HTTPClient http;
  http.begin(tts_api_url);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  // 进行 URL 编码
  String encodedText = urlEncode(urlEncode(text));
  Serial.println(encodedText);

  //String requestBody = "cuid=ESP32-S3&lan=zh&ctp=1&tok=" + baidu_access_token + "&tex=" + text + "&vol=5&per=4";

  // 拼接请求体
  String requestBody = "tex=" + encodedText + "&tok=" + baidu_access_token + "&cuid=aZUku0ho5CwNzwU4XElF01zJOlQPjb8J&ctp=1&lan=zh&spd=5&pit=5&vol=5&per=1&aue=3";
  Serial.println(requestBody);

  char* synthesizedAudio = NULL;
  int httpCode = http.POST(requestBody);
  if (httpCode == HTTP_CODE_OK) {
    String response = http.getString();
    Serial.println("response:");
    Serial.println(response.length());

    //    int binarySize = Base64_Arturo.decode((char*)pcm_data, (char*)response.c_str(), response.length());  // 从response中解码数据到chunk
    // Serial.println("binarySize:");
    // Serial.println(binarySize);

    //分段获取PCM音频数据并输出到I2S上
    int response_len = response.length();

    memcpy((uint8_t*)pcm_data, (uint8_t*)response.c_str(), response_len);
    for (int j = 0; j < response_len / 2; j++) {
      // uint8_t res1 = chunk[2*j];
      // uint8_t res2 = chunk[2*j+1];
      // int16_t res = (res2 << 8) | (res1);
      int16_t res = pcm_data[j];
      int16_t s = ((res & 0xFF) << 8) | (res >> 8);
      Serial.println(res);
    }

    // for (int i = 0; i < response_len; i += CHUNK_SIZE) {
    //   int remaining = min(CHUNK_SIZE, response_len);                                       // 计算剩余数据长度
    //   int16_t chunk[CHUNK_SIZE];                                                              // 创建一个缓冲区来存储读取的数据
    //   int decoded_length = Base64_Arturo.decode((char*)chunk, (char*)(response.c_str() + i), remaining);  // 从response中解码数据到chunk

    //   Serial.println("decoded_length:");
    //   Serial.println(decoded_length);
    //   for(int j = 0; j < decoded_length/2; j++)
    //   {
    //     // uint8_t res1 = chunk[2*j];
    //     // uint8_t res2 = chunk[2*j+1];
    //     // int16_t res = (res2 << 8) | (res1);
    //     Serial.println(chunk[j]);
    //   }
    // }
    // DynamicJsonDocument responseDoc(51200);
    // deserializeJson(responseDoc, response);

    // 获取音频数据长度
    // size_t binarySize = responseDoc["binary"].as<String>().length();
    // const char* binaryData = responseDoc["binary"].as<String>().c_str();
    // // 将音频数据复制到数组中
    // int decoded_length = Base64_Arturo.decode((char*)pcm_data, (char*)binaryData, binarySize);  // 从response中解码数据到chunk
    // Serial.printf("[synthesizedAudio]%d %d %d\n", binarySize, decoded_length, pcm_data[0]);
  } else {
    Serial.printf("[HTTP] POST... failed, error: %s\n", http.errorToString(httpCode).c_str());
  }

  http.end();
  return (char*)synthesizedAudio;
}

const int PER = 4;
const int SPD = 5;
const int PIT = 5;
const int VOL = 5;
const int AUE = 6;

void tts_send(String text) {
  if (baidu_access_token == "") {
    baidu_access_token = getAccessToken(baidu_api_key, baidu_secret_key);
  }

  // 进行 URL 编码
  String encodedText = urlEncode(urlEncode(text));

  const char* TTS_URL = "https://tsn.baidu.com/text2audio";
  String url = TTS_URL;

  const char* headerKeys[] = { "Content-Type", "Content-Length" };
  // 5、修改百度语音助手的token
  url += "?tok=" + baidu_access_token;
  url += "&tex=" + encodedText;
  url += "&per=" + String(PER);
  url += "&spd=" + String(SPD);
  url += "&pit=" + String(PIT);
  url += "&vol=" + String(VOL);
  url += "&aue=" + String(AUE);
  url += "&cuid=esp32s3";
  url += "&lan=zh";
  url += "&ctp=1";

  HTTPClient http;

  Serial.print("URL: ");
  Serial.println(url);

  http.begin(url);
  http.collectHeaders(headerKeys, 2);
  int httpResponseCode = http.GET();
  if (httpResponseCode > 0) {
    if (httpResponseCode == HTTP_CODE_OK) {
      Serial.print("Content-Type = ");
      Serial.println(http.header("Content-Type"));
      String contentType = http.header("Content-Type");
      Serial.println(contentType);
      if (contentType.startsWith("audio")) {
        Serial.println("合成成功，返回的是音频文件");
        // 处理音频文件，保存到SD卡或者播放
        // 读取音频数据并写入文件
        WiFiClient* stream = http.getStreamPtr();
        // 4. 读取数据并写入文件
        int16_t buffer[512];  // 缓冲区大小可以调整
        size_t bytesRead;
        size_t bytes_written;
        int total = 0;
        while (http.connected() && (bytesRead = stream->readBytes((uint8_t*)buffer, sizeof(buffer))) > 0) {
          // for (int j = 0; j < bytesRead / 2; j++) {
          //   // uint8_t res1 = chunk[2*j];
          //   // uint8_t res2 = chunk[2*j+1];
          //   // int16_t res = (res2 << 8) | (res1);
          //   Serial.println(buffer[j]);
          // }
          // total += bytesRead;
          i2s_write(I2S_OUT_PORT, buffer, sizeof(buffer), &bytes_written, portMAX_DELAY);
        }
        Serial.println("total:");
        Serial.println(total);
      } else if (contentType.equals("application/json")) {
        Serial.println("合成出现错误，返回的是JSON文本");
        // 处理错误信息，根据需要进行相应的处理
      } else {
        Serial.println("未知的Content-Type");
        // 可以添加相应的处理逻辑
      }
    } else {
      Serial.println("Failed to receive audio file");
    }
  } else {
    Serial.print("Error code: ");
    Serial.println(httpResponseCode);
  }
  http.end();
}

// Play audio data using MAX98357A
#define CHUNK_SIZE 2048
void playAudio(uint8_t* audioData, size_t audioDataSize) {
  size_t bytes_written = 0;
  int32_t divs = audioDataSize / CHUNK_SIZE;
  int32_t remaining = audioDataSize % CHUNK_SIZE;

  for (int32_t i = 0; i < divs; i++) {
    i2s_write(I2S_OUT_PORT, audioData + i * CHUNK_SIZE, CHUNK_SIZE, &bytes_written, portMAX_DELAY);
  }
  if (remaining) {
    i2s_write(I2S_OUT_PORT, audioData + divs * CHUNK_SIZE, remaining, &bytes_written, portMAX_DELAY);
  }
}