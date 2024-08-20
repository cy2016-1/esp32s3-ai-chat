#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <base64.h>
#include <driver/i2s.h>

// I2S config for MAX98357A
#define I2S_OUT_PORT I2S_NUM_0
#define I2S_OUT_BCLK 16
#define I2S_OUT_LRC  17
#define I2S_OUT_DOUT 15 

// INMP441 config
#define I2S_IN_PORT I2S_NUM_1
#define I2S_IN_BCLK  4
#define I2S_IN_LRC   5
#define I2S_IN_DIN   6 

// WiFi credentials
const char* ssid = "chging";
const char* password = "1993@Chg";

// Baidu API credentials
const char* baidu_api_key = "mnCVxgi8S8fLfIldPeNUewAq";
const char* baidu_secret_key = "x0xcBGyUA7NTxzw7wEJyEz9uqmXMbxTd";
const char* stt_api_url = "https://vop.baidu.com/server_api"; // 语音识别API
const char* ernie_api_url = "https://aip.baidubce.com/rpc/2.0/ai_custom/v1/wenxinworkshop/chat/completions?access_token=24.65653adcb3499e58af63696c38942878.2592000.1726763505.282335-95595183"; // 千帆大模型API
const char* tts_api_url = "https://tts.baidu.com/restapi2/v1/tts"; // 语音合成API

// Audio recording settings
const int sampleRate = 16000;
const int bufferSize = 1024;
uint8_t audioBuffer[bufferSize];

// Function prototypes
String getAccessToken();
String speechToText(uint8_t* audioData, size_t audioDataSize);
String getErnieBotResponse(String prompt);
String textToSpeech(String text);
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
    .sample_rate = sampleRate,
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
    .sample_rate = sampleRate,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT, // 注意：INMP441 输出 32 位数据
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
}

void loop() {
  // Record audio from INMP441
  size_t bytesRead;
  i2s_read(I2S_IN_PORT, (char*)audioBuffer, bufferSize, &bytesRead, portMAX_DELAY);
  
  // Convert 32-bit audio data to 16-bit
  int16_t audioBuffer16[bufferSize / 2];
  for (int i = 0; i < bufferSize / 4; i++) {
    audioBuffer16[i] = (int16_t)audioBuffer[i * 4 + 2] << 8 | audioBuffer[i * 4 + 3]; // 取高 16 位
  }

  // Perform speech to text
  String recognizedText = speechToText((uint8_t*)audioBuffer16, bufferSize / 2);
  Serial.println("Recognized text: " + recognizedText);

  // Get response from Ernie Bot
  String ernieResponse = getErnieBotResponse(recognizedText);
  Serial.println("Ernie Bot response: " + ernieResponse);

  // Perform text to speech
  String synthesizedAudio = textToSpeech(ernieResponse);

  // Play audio via MAX98357A
  playAudio((uint8_t*)synthesizedAudio.c_str(), synthesizedAudio.length());

  delay(100);
}

// Baidu API access token (全局变量)
String access_token = "";

// Get Baidu API access token
String getAccessToken() {
  HTTPClient http;
  http.begin("https://aip.baidubce.com/oauth/2.0/token?grant_type=client_credentials&client_id=" + String(baidu_api_key) + "&client_secret=" + String(baidu_secret_key));
  int httpCode = http.POST("");

  if (httpCode == HTTP_CODE_OK) {
    String response = http.getString();
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, response);
    access_token = doc["access_token"].as<String>();
  } else {
    Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();

  return access_token;
}

// Perform speech to text using Baidu STT API
String speechToText(uint8_t* audioData, size_t audioDataSize) {
  if (access_token == "") {
    getAccessToken();
  }

  HTTPClient http;
  http.begin(stt_api_url);
  http.addHeader("Content-Type", "application/json");

  // Base64 encode audio data
  String base64AudioData = base64::encode(audioData, audioDataSize);

  // Construct JSON request body
  DynamicJsonDocument doc(2048);
  doc["format"] = "pcm";
  doc["rate"] = sampleRate;
  doc["channel"] = 1;
  doc["cuid"] = "ESP32-S3";
  doc["token"] = access_token;
  doc["lan"] = "zh";
  doc["speech"] = base64AudioData;
  doc["len"] = audioDataSize;
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
  if (access_token == "") {
    getAccessToken();
  }

  HTTPClient http;
  http.begin(ernie_api_url);
  http.addHeader("Content-Type", "application/json");

  DynamicJsonDocument doc(1024);
  doc["prompt"] = prompt;
  String requestBody;
  serializeJson(doc, requestBody);

  int httpCode = http.POST(requestBody);
  String ernieResponse = "";

  if (httpCode == HTTP_CODE_OK) {
    String response = http.getString();
    DynamicJsonDocument responseDoc(2048);
    deserializeJson(responseDoc, response);

    ernieResponse = responseDoc["result"].as<String>();
  } else {
    Serial.printf("[HTTP] POST... failed, error: %s\n", http.errorToString(httpCode).c_str());
  }

  http.end();
  return ernieResponse;
}

// Perform text to speech using Baidu TTS API
String textToSpeech(String text) {
  if (access_token == "") {
    getAccessToken();
  }

  HTTPClient http;
  http.begin(tts_api_url);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String requestBody = "cuid=ESP32-S3&lan=zh&ctp=1&tok=" + access_token + "&tex=" + text + "&vol=5&per=4";

  int httpCode = http.POST(requestBody);
  String synthesizedAudio = "";

  if (httpCode == HTTP_CODE_OK) {
    synthesizedAudio = http.getString();
  } else {
    Serial.printf("[HTTP] POST... failed, error: %s\n", http.errorToString(httpCode).c_str());
  }

  http.end();
  return synthesizedAudio;
}

// Play audio data using MAX98357A
void playAudio(uint8_t* audioData, size_t audioDataSize) {
  size_t bytesWritten;
  i2s_write(I2S_OUT_PORT, audioData, audioDataSize, &bytesWritten, portMAX_DELAY);
}