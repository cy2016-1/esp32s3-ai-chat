#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <base64.h>
#include <driver/i2s.h>
#include <UrlEncode.h>

// I2S config for MAX98357A
#define I2S_OUT_PORT I2S_NUM_1
#define I2S_OUT_BCLK 15
#define I2S_OUT_LRC 16
#define I2S_OUT_DOUT 7

// INMP441 config
#define I2S_IN_PORT I2S_NUM_0
#define I2S_IN_BCLK 4
#define I2S_IN_LRC 5
#define I2S_IN_DIN 6

// WiFi credentials
const char* ssid = "chging";
const char* password = "1993@Chg";

// Baidu API credentials
const char* baidu_api_key = "mnCVxgi8S8fLfIldPeNUewAq";
const char* baidu_secret_key = "x0xcBGyUA7NTxzw7wEJyEz9uqmXMbxTd";

// Baidu 千帆大模型
char* qianfan_api_key = "Lb7o26wf56OZRO1r1ht5qpDV";
char* qianfan_secret_key = "YVlGevejLEQOy477sfW1BdC8wzidvET8";

// Audio recording settings
#define SAMPLE_RATE 16000
#define RECORD_TIME_SECONDS 10
#define BUFFER_SIZE (SAMPLE_RATE * RECORD_TIME_SECONDS)

// Function prototypes
String getAccessToken(const char* api_key, const char* secret_key);
String baiduSTT_Send(uint8_t* audioData, int audioDataSize);
String baiduErnieBot_Get(String prompt);
void baiduTTS_Send(String text, uint8_t* audioData, size_t audioDataSize, size_t* ttsSize);
void playAudio(uint8_t* audioData, size_t audioDataSize);

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

void loop() {
  if (Serial.available() > 0) {
    // 读取并打印输入的字符
    int incomingByte = Serial.read();  // 读取输入的字节
    Serial.println(incomingByte);      // 在串口监视器上打印读取的字节
  }

  // Record audio from INMP441
  // 分配内存
  uint8_t* pcm_data = (uint8_t*)ps_malloc(BUFFER_SIZE);
  if (!pcm_data) {
    Serial.println("Failed to allocate memory for pcm_data");
    return;
  }

  // 开始循环录音，将录制结果保存在pcm_data中
  int incomingByte = 0;
  size_t bytes_read = 0, recordingSize = 0, ttsSize = 0;
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
      memcpy(pcm_data + recordingSize, data, bytes_read);
      recordingSize += bytes_read;
      Serial.printf("%x recordingSize: %d bytes_read :%d\n", pcm_data + recordingSize, recordingSize, bytes_read);
    }

    if (incomingByte == 49 || recordingSize >= BUFFER_SIZE - bytes_read) {
      Serial.printf("record done: %d", recordingSize);
      break;
    }
  }

  Serial.println(recordingSize);
  if (recordingSize > 0) {
    // 音频转文本（语音识别API访问）
    String recognizedText = baiduSTT_Send(pcm_data, recordingSize);
    Serial.println("Recognized text: " + recognizedText);

    // 访问千帆大模型（LLM大模型API访问）
    String ernieResponse = baiduErnieBot_Get(recognizedText.c_str());
    Serial.println("Ernie Bot response: " + ernieResponse);

    // 文本转音频tts（语音合成API访问）
    baiduTTS_Send(ernieResponse, pcm_data, BUFFER_SIZE, &ttsSize);
    Serial.println("ttsSize: ");
    Serial.println(ttsSize);

    // 通过 MAX98357A 播放音频
    playAudio(pcm_data, ttsSize);
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

  // 创建http请求
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

String baiduSTT_Send(uint8_t* audioData, int audioDataSize) {
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

  // 创建http请求
  HTTPClient http_client;

  http_client.begin("http://vop.baidu.com/server_api");
  http_client.addHeader("Content-Type", "application/json");
  int httpCode = http_client.POST(data_json);

  if (httpCode > 0) {
    if (httpCode == HTTP_CODE_OK) {
      // 获取返回结果
      String response = http_client.getString();
      Serial.println(response);

      // 从json中解析对应的result
      DynamicJsonDocument responseDoc(2048);
      deserializeJson(responseDoc, response);
      recognizedText = responseDoc["result"].as<String>();
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

// Get response from Baidu Ernie Bot
String baiduErnieBot_Get(String prompt) {
  String ernieResponse = "";

  if (qianfan_access_token == "") {
    qianfan_access_token = getAccessToken(qianfan_api_key, qianfan_secret_key);
  }

  if (prompt.length() == 0)
    return ernieResponse;

  // 创建http, 添加访问url和头信息
  HTTPClient http;

  // 千帆大模型API
  const char* ernie_api_url = "https://aip.baidubce.com/rpc/2.0/ai_custom/v1/wenxinworkshop/chat/ernie-lite-8k?access_token=";

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

  // 发送http访问请求
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

void baiduTTS_Send(String text, uint8_t* audioData, size_t audioDataSize, size_t* ttsSize) {
  if (baidu_access_token == "") {
    baidu_access_token = getAccessToken(baidu_api_key, baidu_secret_key);
  }

  if (text.length() == 0) {
    Serial.println("text is null");
    return;
  }

  const int PER = 4;
  const int SPD = 5;
  const int PIT = 5;
  const int VOL = 5;
  const int AUE = 6;

  // 进行 URL 编码
  String encodedText = urlEncode(urlEncode(text));

  // URL http请求数据封装
  const char* TTS_URL = "https://tsn.baidu.com/text2audio";
  String url = TTS_URL;

  const char* headerKeys[] = { "Content-Type", "Content-Length" };

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

  // http请求创建
  HTTPClient http;

  http.begin(url);
  http.collectHeaders(headerKeys, 2);

  // http请求
  int httpResponseCode = http.GET();
  if (httpResponseCode > 0) {
    if (httpResponseCode == HTTP_CODE_OK) {
      String contentType = http.header("Content-Type");
      Serial.println(contentType);
      if (contentType.startsWith("audio")) {
        Serial.println("合成成功，返回的是音频文件");

        // 获取返回的音频数据流
        WiFiClient* stream = http.getStreamPtr();
        uint8_t buffer[1024];
        size_t bytesRead = 0;
        size_t bytesTotal = 0;
        size_t bytesRemain = audioDataSize;

        while (http.connected() && bytesRemain > 0 && (bytesRead = stream->readBytes(buffer, sizeof(buffer))) > 0) {
          bytesRead = min(bytesRemain, bytesRead);
          memcpy(audioData + bytesTotal, buffer, bytesRead);
          bytesTotal += bytesRead;
          bytesRemain -= bytesRead;
        }
        *ttsSize = bytesTotal;
      } else if (contentType.equals("application/json")) {
        Serial.println("合成出现错误，返回的是JSON文本");
      } else {
        Serial.println("未知的Content-Type");
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
void playAudio(uint8_t* audioData, size_t audioDataSize) {
  if (audioDataSize > 0) {
    // 发送
    size_t bytes_written = 0;
    i2s_write(I2S_OUT_PORT, (int16_t*)audioData, audioDataSize, &bytes_written, portMAX_DELAY);

    // 清空I2S DMA缓冲区
    delay(200);
    i2s_zero_dma_buffer(I2S_OUT_PORT);
  }
}