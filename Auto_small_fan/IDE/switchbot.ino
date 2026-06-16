/*
 * 自動スタンドライト  (Arduino UNO R4 WiFi)
 * --------------------------------------------------
 * 超音波センサー HC-SR04 で人を検知し、
 * Wi-Fi 経由で SwitchBot プラグ(API v1.1)に
 * HTTP POST を送って ON / OFF する。
 *
 * 必要ライブラリ（Arduino IDE のライブラリマネージャからインストール）:
 *   - ArduinoHttpClient
 *   - Crypto            （作者: Rhys Weatherley。HMAC-SHA256 用）
 *   ※ WiFiS3 は UNO R4 WiFi のボードパッケージに同梱（追加インストール不要）
 *
 * 配線:
 *   HC-SR04 VCC  -> 5V
 *   HC-SR04 GND  -> GND
 *   HC-SR04 Trig -> D9
 *   HC-SR04 Echo -> D10
 */

#include <WiFiS3.h>
#include <ArduinoHttpClient.h>
#include <SHA256.h>

// ======== ユーザー設定 ここから ========

// Wi-Fi
const char* WIFI_SSID = "使っているWiFi";
const char* WIFI_PASS = "WiFiのパスワード";

// SwitchBot（アプリの 開発者向けオプション で取得）
const char* SB_TOKEN  = "取得したトークン";
const char* SB_SECRET = "取得したシークレット";
String      SB_DEVICE = "取得したデバイスID";   // /v1.1/devices で確認（下の説明参照）

// センサー / 判定パラメータ
//使いやすい値に変える
const int   TRIG_PIN          = 9;
const int   ECHO_PIN          = 10;
const long  DETECT_CM         = 50;    // この距離(cm)以内なら「人がいる」
const long  MIN_VALID_CM      = 2;      // これ未満は誤読として無視
const long  MAX_VALID_CM      = 150;    // これより遠い/測定不能は「いない」扱い
const unsigned long ABSENCE_TIMEOUT_MS = 5000;  // 何も検知しない状態がこの時間続いたらOFF
const unsigned long SAMPLE_INTERVAL_MS = 200;   // 測定間隔

// ======== ユーザー設定 ここまで ========

WiFiSSLClient sslClient;
HttpClient    http = HttpClient(sslClient, "api.switch-bot.com", 443);

bool fanIsOn = false;                 // プラグの現在状態（こちらが把握している値）
unsigned long lastSampleMs = 0;
unsigned long lastSeenMs = 0;         // 最後に人を検知した時刻

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);

  randomSeed(analogRead(A0) ^ micros());  // nonce 生成用

  connectWiFi();
  Serial.println("準備完了。検知を開始します。");
}

void loop() {
  // Wi-Fi が切れたら再接続
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  unsigned long now = millis();
  if (now - lastSampleMs < SAMPLE_INTERVAL_MS) return;
  lastSampleMs = now;

  long cm = readDistanceCm();
  bool present = (cm >= MIN_VALID_CM && cm <= DETECT_CM);

  if (present) {
    lastSeenMs = now;
  }

  // 望ましい状態を決める（ヒステリシス: 一度検知したらタイムアウトまでON維持）
  bool wantOn = (now - lastSeenMs) <= ABSENCE_TIMEOUT_MS && lastSeenMs != 0;

  // 状態が変化したときだけ HTTP を送る（API 回数の節約 & チャタリング防止）
  if (wantOn && !fanIsOn) {
    Serial.println("人を検知 -> ON 要求");
    if (sendCommand("turnOn")) fanIsOn = true;
  } else if (!wantOn && fanIsOn) {
    Serial.println("不在 -> OFF 要求");
    if (sendCommand("turnOff")) fanIsOn = false;
  }
}

// ---- 超音波測定。失敗時は -1 ----
long readDistanceCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // 最大 ~400cm 相当の往復時間でタイムアウト
  unsigned long dur = pulseIn(ECHO_PIN, HIGH, 25000UL);
  if (dur == 0) return MAX_VALID_CM + 1;   // 反射なし=遠い/不在
  long cm = (long)(dur / 58);              // 音速から距離(cm)へ
  return cm;
}

// ---- Wi-Fi 接続 ----
void connectWiFi() {
  Serial.print("Wi-Fi 接続中");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" OK");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

// ---- SwitchBot にコマンド送信（turnOn / turnOff）----
bool sendCommand(const char* command) {
  // 1) タイムスタンプ（13桁ミリ秒）。epoch秒 * 1000 なので末尾は "000"
  unsigned long epoch = WiFi.getTime();
  if (epoch == 0) {
    Serial.println("時刻取得に失敗。少し待って再試行します。");
    return false;
  }
  String t = String(epoch) + "000";

  // 2) nonce（任意のユニーク文字列）
  String nonce = makeNonce();

  // 3) sign = Base64( HMAC-SHA256( token + t + nonce, secret ) ).toUpperCase()
  String toSign = String(SB_TOKEN) + t + nonce;
  uint8_t mac[32];
  hmacSha256(SB_SECRET, toSign, mac);
  String sign = base64Encode(mac, 32);
  sign.toUpperCase();

  // 4) リクエスト本文
  String body = String("{\"command\":\"") + command +
                "\",\"parameter\":\"default\",\"commandType\":\"command\"}";

  // 5) POST 送信
  String path = "/v1.1/devices/" + SB_DEVICE + "/commands";
  http.beginRequest();
  http.post(path);
  http.sendHeader("Authorization", SB_TOKEN);
  http.sendHeader("sign", sign);
  http.sendHeader("t", t);
  http.sendHeader("nonce", nonce);
  http.sendHeader("Content-Type", "application/json; charset=utf8");
  http.sendHeader("Content-Length", body.length());
  http.beginBody();
  http.print(body);
  http.endRequest();

  int status = http.responseStatusCode();
  String resp = http.responseBody();
  Serial.print("HTTP ");
  Serial.print(status);
  Serial.print("  ");
  Serial.println(resp);

  return (status == 200);
}

// ---- HMAC-SHA256（出力は 32バイト）----
void hmacSha256(const char* key, const String& msg, uint8_t* out) {
  SHA256 sha;
  size_t keyLen = strlen(key);
  sha.resetHMAC(key, keyLen);
  sha.update((const uint8_t*)msg.c_str(), msg.length());
  sha.finalizeHMAC(key, keyLen, out, 32);
}

// ---- 16文字のランダム16進 nonce ----
String makeNonce() {
  const char* hex = "0123456789abcdef";
  String s = "";
  for (int i = 0; i < 16; i++) s += hex[random(16)];
  return s;
}

// ---- 標準 Base64 エンコード ----
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
