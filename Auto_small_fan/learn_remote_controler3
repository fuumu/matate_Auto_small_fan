/*
  学習リモコン ── スロット名を日本語表示（U8g2版）
  ============================================================
  ボード      : Arduino UNO R4 WiFi
  ライブラリ  : IRremote 4.x / U8g2（ライブラリマネージャで「U8g2」を検索）
                ※この版では Adafruit SSD1306 は使いません

  ■ ポイント
    ・OLEDに「電源」「風量 強/中/弱」などスロット名を日本語で表示
    ・スロット名は slotNames[] で自由に変更可（下記）
    ・名前は単なるラベル。例：スロット0=「電源」に電源信号を記録する

  ■ OLED配線（I2C・4本。既存の回路はそのまま）
    OLED VCC → 5V / GND → GND / SDA → SDA(A4) / SCL → SCL(A5)

  ■ 既存の配線
    D3:赤外線LED送信  D2:赤外線受信  D4:選択  D5:記録  D6:送信
  ============================================================
*/

#define IR_SEND_PIN        3
#define IR_RECEIVE_PIN     2
#define BUTTON_SELECT_PIN  4
#define BUTTON_RECORD_PIN  5
#define BUTTON_SEND_PIN    6

#define RAW_BUFFER_LENGTH  750
//#define RECORD_GAP_MICROS 12000

#include <IRremote.hpp>

// ---- OLED (U8g2) ----
#include <U8g2lib.h>
#include <Wire.h>
// SSD1306 128x64 をハードウェアI2Cで使う（フルバッファ "F"）
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

#define NUM_SLOTS 5

// ★ここを書き換えればスロット名を自由に変更できます（UTF-8でそのまま日本語OK）
const char *slotNames[NUM_SLOTS] = {
  "電源",
  "風量 強",
  "風量 中",
  "風量 弱",
  "光調節",
};

struct StoredCode {
  IRData   data;
  uint8_t  rawCode[RAW_BUFFER_LENGTH];
  uint16_t rawLength;
  bool     used;
};
StoredCode slots[NUM_SLOTS];

uint8_t activeSlot   = 0;
bool    armRecording = false;
String  statusMsg    = "READY";   // 画面上部に出す状態（英数字）

// ---- ボタン ----
struct Button { uint8_t pin; bool lastReading; bool stableState; unsigned long lastChange; };
Button btnSelect = { BUTTON_SELECT_PIN, HIGH, HIGH, 0 };
Button btnRecord = { BUTTON_RECORD_PIN, HIGH, HIGH, 0 };
Button btnSend   = { BUTTON_SEND_PIN,   HIGH, HIGH, 0 };

bool wasPressed(Button &b) {
  bool reading = digitalRead(b.pin);
  if (reading != b.lastReading) { b.lastChange = millis(); b.lastReading = reading; }
  if (millis() - b.lastChange > 30) {
    if (reading != b.stableState) {
      b.stableState = reading;
      if (b.stableState == LOW) return true;
    }
  }
  return false;
}

// ---- OLED表示 ----
void updateDisplay() {
  u8g2.clearBuffer();

  // 上部：状態メッセージ（小さい英数字フォント）
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.setDrawColor(1);
  u8g2.setCursor(0, 8);
  u8g2.print(statusMsg);

  // スロット一覧（日本語フォント）。選択中は反転表示
  u8g2.setFont(u8g2_font_b10_t_japanese1);
  for (uint8_t i = 0; i < NUM_SLOTS; i++) {
    int baseline = 18 + i * 11;                 // 各行のベースライン位置
    if (i == activeSlot) {
      u8g2.setDrawColor(1);
      u8g2.drawBox(0, baseline - 9, 128, 11);   // 白い帯
      u8g2.setDrawColor(0);                     // 黒文字
    } else {
      u8g2.setDrawColor(1);                     // 白文字
    }
    // 左：スロット名
    u8g2.setCursor(2, baseline);
    u8g2.print(slotNames[i]);
    // 右：記録状態
    u8g2.setCursor(96, baseline);
    if (!slots[i].used)                          u8g2.print("--");
    else if (slots[i].data.protocol == UNKNOWN)  u8g2.print("RAW");
    else                                         u8g2.print("OK");
  }
  u8g2.setDrawColor(1);                          // 念のため戻す
  u8g2.sendBuffer();
}

// ---- 記録 ----
bool storeCode(StoredCode *dst) {
  if (IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT) return false;

  if (IrReceiver.decodedIRData.protocol == UNKNOWN) {
    if (IrReceiver.decodedIRData.flags & IRDATA_FLAGS_WAS_OVERFLOW) {
      Serial.println(F("  ※バッファ不足！ RAW_BUFFER_LENGTH を大きくしてください"));
      statusMsg = "OVERFLOW!";
      return false;
    }
    IrReceiver.compensateAndStoreIRResultInArray(dst->rawCode);
    dst->rawLength     = IrReceiver.decodedIRData.rawlen - 1;
    dst->data.protocol = UNKNOWN;
    Serial.print(F("  RAWで記録: "));
    Serial.print(dst->rawLength);
    Serial.println(F(" 個の時間データ"));
  } else {
    IrReceiver.decodedIRData.flags = 0;
    dst->data = IrReceiver.decodedIRData;
    Serial.print(F("  記録: "));
    IrReceiver.printIRResultShort(&Serial);
  }
  dst->used = true;
  return true;
}

// ---- 送信 ----
void sendCode(StoredCode *src) {
  if (!src->used) { Serial.println(F("  このスロットは空です")); statusMsg = "EMPTY"; return; }

  if (src->data.protocol == UNKNOWN) {
    IrSender.sendRaw(src->rawCode, src->rawLength, 38);
    Serial.println(F("  RAW送信"));
  } else {
    IrSender.write(&src->data);
    Serial.print(F("  送信: "));
    printIRResultShort(&Serial, &src->data);
  }
  IrReceiver.restartAfterSend();
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) { ; }

  pinMode(BUTTON_SELECT_PIN, INPUT_PULLUP);
  pinMode(BUTTON_RECORD_PIN, INPUT_PULLUP);
  pinMode(BUTTON_SEND_PIN,   INPUT_PULLUP);

  IrReceiver.begin(IR_RECEIVE_PIN, ENABLE_LED_FEEDBACK);
  IrSender.begin(IR_SEND_PIN);

  u8g2.begin();
  u8g2.enableUTF8Print();   // 日本語(UTF-8)をprintで出せるようにする

  Serial.println(F("学習リモコン（日本語OLED版）準備完了"));
  updateDisplay();
}

void loop() {
  // 選択
  if (wasPressed(btnSelect)) {
    activeSlot = (activeSlot + 1) % NUM_SLOTS;
    statusMsg = "SELECT";
    updateDisplay();
  }

  // 記録待ち
  if (wasPressed(btnRecord)) {
    armRecording = true;
    statusMsg = "REC: push remote";
    Serial.print(F("[記録待ち] スロット"));
    Serial.println(activeSlot);
    updateDisplay();
  }

  // 受信処理
  if (IrReceiver.decode()) {
    if (armRecording) {
      if (storeCode(&slots[activeSlot])) {
        armRecording = false;
        statusMsg = "SAVED";
        Serial.print(F("→ スロット"));
        Serial.print(activeSlot);
        Serial.println(F(" に保存"));
      }
      updateDisplay();
    }
    IrReceiver.resume();
  }

  // 送信
  if (wasPressed(btnSend)) {
    statusMsg = "SENT";
    Serial.print(F("[送信] スロット"));
    Serial.println(activeSlot);
    sendCode(&slots[activeSlot]);
    updateDisplay();
  }
}
