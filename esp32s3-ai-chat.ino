#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <base64.h>
#include <driver/i2s.h>
#include <Base64_Arturo.h>

// I2S config for MAX98357A
#define I2S_OUT_PORT I2S_NUM_0
#define I2S_OUT_BCLK 16
#define I2S_OUT_LRC 17
#define I2S_OUT_DOUT 15

// INMP441 config
#define I2S_IN_PORT I2S_NUM_1
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

const char* stt_api_url = "https://vop.baidu.com/server_api";                                                                 // 语音识别API
const char* ernie_api_url = "https://aip.baidubce.com/rpc/2.0/ai_custom/v1/wenxinworkshop/chat/ernie-lite-8k?access_token=";  // 千帆大模型API
const char* tts_api_url = "https://tsn.baidu.com/text2audio";                                                                 // 语音合成API

// Audio recording settings
#define SAMPLE_RATE 16000
#define RECORD_TIME_SECONDS 5
#define BUFFER_SIZE (SAMPLE_RATE * RECORD_TIME_SECONDS)
int16_t audioData[4096];        //i2s audio录音缓存区
int16_t pcm_data[BUFFER_SIZE];  //一帧录音缓存区

// Function prototypes
String getAccessToken(const char* api_key, const char* secret_key);
String speechToText(int16_t* audioData, size_t audioDataSize);
String getErnieBotResponse(String prompt);
char* textToSpeech(String text);
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
    .use_apll = false
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
    .use_apll = false
  };
  i2s_pin_config_t pin_config_in = {
    .bck_io_num = I2S_IN_BCLK,
    .ws_io_num = I2S_IN_LRC,
    .data_out_num = -1,
    .data_in_num = I2S_IN_DIN
  };
  i2s_driver_install(I2S_IN_PORT, &i2s_config_in, 0, NULL);
  i2s_set_pin(I2S_IN_PORT, &pin_config_in);
  // esp_err_t err = i2s_zero_dma_buffer(I2S_IN_PORT);
  // if (err != ESP_OK) {
  //   Serial.println("Error in initializing dma buffer with 0");
  // }

  // err = i2s_start(I2S_IN_PORT);
  // if (err != ESP_OK) {
  //   Serial.printf("I2S start failed (I2S_IN_PORT): %d\n", err);
  //   while (true)
  //     ;
  // }
}

void loop() {
  if (Serial.available() > 0) {
    // 读取并打印输入的字符
    int incomingByte = Serial.read();  // 读取输入的字节
    Serial.println(incomingByte);      // 在串口监视器上打印读取的字节
  }

  // Record audio from INMP441
  // 分配内存
  // pcm_data = reinterpret_cast<int16_t*>(ps_malloc(BUFFER_SIZE * 2));
  // if (!pcm_data) {
  //   Serial.println("Failed to allocate memory for pcm_data");
  // }

  // 开始循环录音，将录制结果保存在pcm_data中
  int incomingByte = 0;
  size_t bytes_read = 0, recordingSize = 0;
  int8_t start_record = 0;
  while (1) {
    if (Serial.available() > 0) {
      incomingByte = Serial.read();  // 读取输入的字节
    }

    if (incomingByte == 50) {
      Serial.printf("start_record: %d\n", start_record);
      start_record = 1;
    }

    if (start_record) {
      esp_err_t result = i2s_read(I2S_IN_PORT, audioData, sizeof(audioData), &bytes_read, portMAX_DELAY);
      memcpy(pcm_data + recordingSize, audioData, bytes_read);
      recordingSize += bytes_read / 2;
    }

    if (incomingByte == 49 || recordingSize >= BUFFER_SIZE) {
      Serial.printf("record done: %d", incomingByte);
      break;
    }
  }

  Serial.println(recordingSize);
  if (recordingSize > 0) {
    // Perform speech to text
    String recognizedText = speechToText(pcm_data, recordingSize);
    Serial.println("Recognized text: " + recognizedText);

    // Get response from Ernie Bot
    String ernieResponse = getErnieBotResponse("你好，讲个小故事，50字以内");
    Serial.println("Ernie Bot response: " + ernieResponse);

    // Perform text to speech
    char* synthesizedAudio = textToSpeech(ernieResponse);

    // Play audio via MAX98357A
    // Serial.printf("synthesizedAudio: %s %d", synthesizedAudio, strlen(synthesizedAudio));
    // playAudio((uint8_t *)synthesizedAudio, strlen(synthesizedAudio));
  }

  // 释放内存
  //free(pcm_data);

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

// Perform speech to text using Baidu STT API
String speechToText(int16_t* audioData, size_t audioDataSize) {
  if (baidu_access_token == "") {
    baidu_access_token = getAccessToken(baidu_api_key, baidu_secret_key);
  }

  HTTPClient http;
  http.begin(stt_api_url);
  http.addHeader("Content-Type", "application/json");

  // Base64 encode audio data
  String base64AudioData = base64::encode((uint8_t*)audioData, audioDataSize * 2);

  // Construct JSON request body
  DynamicJsonDocument doc(2048);
  doc["format"] = "pcm";
  doc["rate"] = SAMPLE_RATE;
  doc["channel"] = 1;
  doc["cuid"] = "ESP32-S3";
  doc["token"] = baidu_access_token;
  doc["lan"] = "zh";
  doc["speech"] = base64AudioData;
  doc["len"] = audioDataSize * 2;
  String requestBody;
  serializeJson(doc, requestBody);

  // Send POST request
  int httpCode = http.POST(requestBody);

  String recognizedText = "";

  if (httpCode == HTTP_CODE_OK) {
    String response = http.getString();
    DynamicJsonDocument responseDoc(2048);
    deserializeJson(responseDoc, response);

    if (responseDoc["err_no"] == 0) {
      recognizedText = responseDoc["result"][0].as<String>();
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

// Perform text to speech using Baidu TTS API
char* textToSpeech(String text) {
  if (baidu_access_token == "") {
    baidu_access_token = getAccessToken(baidu_api_key, baidu_secret_key);
  }

  HTTPClient http;
  http.begin(tts_api_url);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  //String requestBody = "cuid=ESP32-S3&lan=zh&ctp=1&tok=" + baidu_access_token + "&tex=" + text + "&vol=5&per=4";
  String requestBody = "tex=%E4%BD%A0%E6%98%AF%E8%B0%81%EF%BC%8C%E5%8F%AF%E4%BB%A5%E6%98%AF%E8%BF%99%E4%B8%AA%E6%95%85%E4%BA%8B%E5%90%97%EF%BC%8C%E6%98%AF%E7%9A%84%E5%90%97&tok=" + baidu_access_token + "&cuid=aZUku0ho5CwNzwU4XElF01zJOlQPjb8J&ctp=1&lan=zh&spd=5&pit=5&vol=5&per=1&aue=3";

  char* synthesizedAudio = NULL;
  int httpCode = http.POST(requestBody);
  if (httpCode == HTTP_CODE_OK) {
    String response = http.getString();

    DynamicJsonDocument responseDoc(51200);
    deserializeJson(responseDoc, response);

    // 获取音频数据长度
    size_t binarySize = responseDoc["binary"].as<String>().length();
    const char* binaryData = responseDoc["binary"].as<String>().c_str();
    // 将音频数据复制到数组中
    int decoded_length = Base64_Arturo.decode((char*)pcm_data, (char*)binaryData, binarySize);  // 从response中解码数据到chunk
    Serial.printf("[synthesizedAudio]%d %d %d\n", binarySize, decoded_length, pcm_data[0]);
  } else {
    Serial.printf("[HTTP] POST... failed, error: %s\n", http.errorToString(httpCode).c_str());
  }

  http.end();
  return (char*)synthesizedAudio;
}

// Play audio data using MAX98357A
void playAudio(uint8_t* audioData, size_t audioDataSize) {
  size_t bytesWritten;
  i2s_write(I2S_OUT_PORT, audioData, audioDataSize, &bytesWritten, portMAX_DELAY);
}