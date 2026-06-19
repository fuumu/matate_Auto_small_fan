#include <WiFiS3.h>
#include <ArduinoHttpClient.h>
#include <SHA256.h>
#include "secrets.h"

// ======== 設定 ========
String      SB_DEVICE = SB_DEVICE_STR;

// Discord Webhook
const char* DISCORD_HOST = "discord.com";
const String DISCORD_PATH = DISCORD_PATH_STR;
const String MSG_DETECTED = "人を検知しました（机に誰か近づきました）";

// 離れたときも通知したいなら true
const bool   NOTIFY_ON_LEAVE = false;
const String MSG_LEFT = "いなくなりました";

const int TRIG_PIN = 9;
const int ECHO_PIN = 10;
const long  DETECT_CM   = 20;
const long  MIN_VALID_CM = 2;
const unsigned long ABSENCE_TIMEOUT_MS = 5000;
const unsigned long SAMPLE_INTERVAL_MS = 200;
const unsigned long NOTIFY_COOLDOWN_MS = 60000;
// ======== 設定ここまで ========

WiFiSSLClient sbSsl;   HttpClient sbHttp = HttpClient(sbSsl, "api.switch-bot.com", 443);
WiFiSSLClient dcSsl;   HttpClient dcHttp = HttpClient(dcSsl, DISCORD_HOST, 443);

bool present = false;
bool wasConnected = false;
unsigned long lastSampleMs = 0;
unsigned long lastSeenMs   = 0;
unsigned long lastNotifyMs = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  Serial.println();
  Serial.println("==================================================");
  Serial.println(" 起動 / リセットしました（setup 実行）");
  Serial.println(" ★この行が動作中に再び出たら、ボードがリセットしています");
  Serial.println("==================================================");

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);
  randomSeed(analogRead(A0) ^ micros());
  connectWiFi();
  Serial.println("検知を開始します。");
}

void loop() {
  // Wi-Fi状態の変化を表示
  bool nowConnected = (WiFi.status() == WL_CONNECTED);
  if (nowConnected != wasConnected) {
    if (nowConnected) {
      Serial.print("[WiFi] 接続OK  IP: "); Serial.print(WiFi.localIP());
      Serial.print("  RSSI: "); Serial.print(WiFi.RSSI()); Serial.println(" dBm");
    } else {
      Serial.println("[WiFi] 切断 -> 再接続します");
    }
    wasConnected = nowConnected;
  }
  if (!nowConnected) connectWiFi();

  unsigned long now = millis();
  if (now - lastSampleMs < SAMPLE_INTERVAL_MS) return;
  lastSampleMs = now;

  long cm = readDistanceCm();
  bool detectedNow = (cm >= MIN_VALID_CM && cm <= DETECT_CM);
  if (detectedNow) lastSeenMs = now;
  bool wantPresent = (lastSeenMs != 0) && (now - lastSeenMs <= ABSENCE_TIMEOUT_MS);

  if (wantPresent && !present) {
    present = true;
    Serial.println("--------------------------------------------------");
    Serial.println("[検知] 人を検知 -> 処理開始");

    bool a = sendCommand("turnOn");
    Serial.print("[検知] SwitchBot結果: "); Serial.println(a ? "成功" : "失敗");

    if (lastNotifyMs == 0 || now - lastNotifyMs >= NOTIFY_COOLDOWN_MS) {
      bool d = sendDiscord(MSG_DETECTED);
      Serial.print("[検知] Discord結果: "); Serial.println(d ? "成功" : "失敗");
      if (d) lastNotifyMs = now;
    } else {
      Serial.println("[検知] 通知はクールダウン中でスキップ");
    }
    Serial.println("[検知] ★処理完了・ループ継続中（この行が出れば落ちていない）");
    Serial.println("--------------------------------------------------");
  } else if (!wantPresent && present) {
    present = false;
    Serial.println("[不在] ライトOFF");
    sendCommand("turnOff");
    if (NOTIFY_ON_LEAVE) sendDiscord(MSG_LEFT);
  }
}

long readDistanceCm() {
  digitalWrite(TRIG_PIN, LOW);  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  unsigned long dur = pulseIn(ECHO_PIN, HIGH, 25000UL);
  if (dur == 0) return 99999;
  return (long)(dur / 58);
}

void connectWiFi() {
  Serial.print("[WiFi] 接続中");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println(" OK");
}

bool sendDiscord(const String& message) {
  Serial.println("  [Discord] 送信開始");
  String body = String("{\"content\":\"") + message + "\"}";
  dcHttp.beginRequest();
  dcHttp.post(DISCORD_PATH);
  dcHttp.sendHeader("Content-Type", "application/json");
  dcHttp.sendHeader("Content-Length", body.length());
  dcHttp.beginBody();
  dcHttp.print(body);
  dcHttp.endRequest();
  int status = dcHttp.responseStatusCode();
  dcHttp.responseBody();
  dcHttp.stop();                       // ★接続を閉じてリソースを解放
  Serial.print("  [Discord] 応答 HTTP "); Serial.println(status);
  Serial.println("  [Discord] 接続クローズ完了");
  return (status == 204 || status == 200);
}

bool sendCommand(const char* command) {
  Serial.println("  [SwitchBot] 送信開始");
  unsigned long epoch = WiFi.getTime();
  if (epoch == 0) { Serial.println("  [SwitchBot] 時刻取得失敗"); return false; }
  String t = String(epoch) + "000";
  String nonce = makeNonce();

  String toSign = String(SB_TOKEN) + t + nonce;
  uint8_t mac[32];
  hmacSha256(SB_SECRET, toSign, mac);
  String sign = base64Encode(mac, 32);
  sign.toUpperCase();

  String body = String("{\"command\":\"") + command +
                "\",\"parameter\":\"default\",\"commandType\":\"command\"}";

  String path = "/v1.1/devices/" + SB_DEVICE + "/commands";
  sbHttp.beginRequest();
  sbHttp.post(path);
  sbHttp.sendHeader("Authorization", SB_TOKEN);
  sbHttp.sendHeader("sign", sign);
  sbHttp.sendHeader("t", t);
  sbHttp.sendHeader("nonce", nonce);
  sbHttp.sendHeader("Content-Type", "application/json; charset=utf8");
  sbHttp.sendHeader("Content-Length", body.length());
  sbHttp.beginBody();
  sbHttp.print(body);
  sbHttp.endRequest();
  int status = sbHttp.responseStatusCode();
  sbHttp.responseBody();
  sbHttp.stop();                       // ★接続を閉じてリソースを解放
  Serial.print("  [SwitchBot] 応答 HTTP "); Serial.println(status);
  return (status == 200);
}

void hmacSha256(const char* key, const String& msg, uint8_t* out) {
  SHA256 sha;
  size_t keyLen = strlen(key);
  sha.resetHMAC(key, keyLen);
  sha.update((const uint8_t*)msg.c_str(), msg.length());
  sha.finalizeHMAC(key, keyLen, out, 32);
}

String makeNonce() {
  const char* hex = "0123456789abcdef";
  String s = "";
  for (int i = 0; i < 16; i++) s += hex[random(16)];
  return s;
}

String base64Encode(const uint8_t* data, size_t len) {
  static const char* tbl =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  String out = "";
  size_t i = 0;
  while (i < len) {
    uint32_t n = data[i++] << 16;
    int valid = 1;
    if (i < len) { n |= data[i++] << 8; valid++; }
    if (i < len) { n |= data[i++];      valid++; }
    out += tbl[(n >> 18) & 0x3F];
    out += tbl[(n >> 12) & 0x3F];
    out += (valid > 1) ? tbl[(n >> 6) & 0x3F] : '=';
    out += (valid > 2) ? tbl[n & 0x3F]        : '=';
  }
  return out;
}
