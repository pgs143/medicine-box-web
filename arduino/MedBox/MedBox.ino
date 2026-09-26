/* ═══════════════════════════════════════════════════════════
   MedBox — Smart Medicine Box 6 Slots
   Board: Heltec WiFi LoRa 32 V4
   ──────────────────────────────────────────────────────────
   Libraries:
     - Firebase ESP32 Client (Mobizt) v4.x
     - ArduinoJson v6.x
     - Adafruit SSD1306 + Adafruit GFX
   ═══════════════════════════════════════════════════════════ */

#include <WiFi.h>
#include <FirebaseESP32.h>
#include <time.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ═══════════════════════════════════════════════════════════
//  ① Configuration
// ═══════════════════════════════════════════════════════════

// ─── WiFi (2.4GHz only) ─────────────────────────────
#define WIFI_SSID     "chompunut"
#define WIFI_PASSWORD "Pgssupply14364"

// ─── Firebase ────────────────────────────────────────
#define FIREBASE_HOST    "basic-firebase-web-1d69a-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_API_KEY "AIzaSyABfYla0viy6ZBb_ULavp3G3A_4o4lk9qg"
#define USER_EMAIL       "esp32@test.com"
#define USER_PASS        "Esp32Pass123!"

// ─── Hardware Pins ───────────────────────────────────
#define BUTTON_PIN    4
#define BUZZER_PIN    5

// ─── OLED Pins (Heltec V4) ───────────────────────────
#define OLED_SDA      17
#define OLED_SCL      18
#define OLED_RST      21
#define OLED_ADDR     0x3C

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64

// ─── Constants ───────────────────────────────────────
#define NUM_SLOTS          6
#define MAX_SCHEDULES      8
#define CHECK_INTERVAL     1000UL       // ตรวจทุก 1 วินาที
#define BUTTON_TIMEOUT     30000UL      // รอสวิตช์ 30 วินาที
#define RELOAD_INTERVAL    30000UL      // โหลดข้อมูลใหม่ทุก 30 วิ

// ═══════════════════════════════════════════════════════════
//  ② Objects
// ═══════════════════════════════════════════════════════════

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST);

FirebaseData   fbdo;
FirebaseData   fbdoEvent;
FirebaseConfig config;
FirebaseAuth   auth;

// ═══════════════════════════════════════════════════════════
//  ③ Enums + Structs
// ═══════════════════════════════════════════════════════════

enum EventStatus {
  EVT_PENDING = 0,
  EVT_NOTIFIED,
  EVT_CONFIRMED,
  EVT_MISSED
};

struct Schedule {
  String time;
  String mealRelation;
};

struct Slot {
  String    medicineName;
  String    medicineNameTH;
  int       dosageMg;
  Schedule  schedules[MAX_SCHEDULES];
  int       scheduleCount;
  bool      enabled;
};

Slot slots[NUM_SLOTS + 1];

// ─── State ───────────────────────────────────────────
unsigned long lastCheck   = 0;
unsigned long lastReload  = 0;
String        lastDate    = "";
bool          isNotifying = false;

// ═══════════════════════════════════════════════════════════
//  ④ Helpers — enum ↔ string
// ═══════════════════════════════════════════════════════════

String statusToString(EventStatus s) {
  switch (s) {
    case EVT_PENDING:   return "pending";
    case EVT_NOTIFIED:  return "notified";
    case EVT_CONFIRMED: return "confirmed";
    case EVT_MISSED:    return "missed";
    default:            return "unknown";
  }
}

EventStatus stringToStatus(String s) {
  if (s == "pending")   return EVT_PENDING;
  if (s == "notified")  return EVT_NOTIFIED;
  if (s == "confirmed") return EVT_CONFIRMED;
  if (s == "missed")    return EVT_MISSED;
  return EVT_PENDING;
}

// ═══════════════════════════════════════════════════════════
//  ⑤ Helpers — Time
// ═══════════════════════════════════════════════════════════

String getDateString() {
  struct tm t;
  if (!getLocalTime(&t)) return "";
  char buf[11];
  strftime(buf, sizeof(buf), "%Y-%m-%d", &t);
  return String(buf);
}

String getTimeString() {
  struct tm t;
  if (!getLocalTime(&t)) return "";
  char buf[6];
  strftime(buf, sizeof(buf), "%H:%M", &t);
  return String(buf);
}

String makeEventKey(int slot, String date, String time) {
  return "slot" + String(slot) + "_" + date + "_" + time;
}

String mealRelationToEnglish(String rel) {
  if (rel == "before_meal") return "Before meal";
  if (rel == "after_meal")  return "After meal";
  if (rel == "immediate")   return "Immediate";
  return "";
}

// ═══════════════════════════════════════════════════════════
//  ⑥ OLED — Adafruit SSD1306 (English)
// ═══════════════════════════════════════════════════════════

void oledPrintCenter(String text, int y, int size) {
  display.setTextSize(size);
  display.setTextColor(SSD1306_WHITE);

  int textWidth = text.length() * 6 * size;
  int x = (SCREEN_WIDTH - textWidth) / 2;
  if (x < 0) x = 0;

  display.setCursor(x, y);
  display.println(text);
}

void oledSplash() {
  display.clearDisplay();
  oledPrintCenter("MedBox", 12, 2);
  oledPrintCenter("Smart Medicine Box", 38, 1);
  oledPrintCenter("6 Slots System", 52, 1);
  display.display();
}

void oledWaiting(String msg) {
  display.clearDisplay();
  oledPrintCenter(msg, 20, 1);
  oledPrintCenter("Please wait...", 44, 1);
  display.display();
}

void oledIdle() {
  display.clearDisplay();
  oledPrintCenter("MedBox", 0, 1);
  oledPrintCenter(getTimeString(), 18, 3);
  oledPrintCenter(getDateString(), 50, 1);
  display.display();
}

void oledNotify(int slot, int schedIdx) {
  Schedule &sc = slots[slot].schedules[schedIdx];

  display.clearDisplay();
  oledPrintCenter(">> MEDICINE TIME <<", 0, 1);
  oledPrintCenter(slots[slot].medicineName, 16, 2);
  oledPrintCenter(String(slots[slot].dosageMg) + " mg", 38, 2);
  oledPrintCenter(
    mealRelationToEnglish(sc.mealRelation) + " | Slot " + String(slot),
    56, 1
  );
  display.display();
}

void oledConfirmed() {
  display.clearDisplay();
  oledPrintCenter("OK", 12, 3);
  oledPrintCenter("Confirmed", 46, 1);
  display.display();
}

void oledMissed() {
  display.clearDisplay();
  oledPrintCenter("TIME", 12, 3);
  oledPrintCenter("Not Confirmed", 46, 1);
  display.display();
}

// ═══════════════════════════════════════════════════════════
//  ⑦ Load Slots from Firebase
// ═══════════════════════════════════════════════════════════

void loadSlots() {
  Serial.println("\n--- Loading medicine data ---");

  for (int i = 1; i <= NUM_SLOTS; i++) {
    String base = "/medicine_box/slots/slot" + String(i);

    // ─── ชื่อยา ──────────────────────────────────
    if (Firebase.getString(fbdo, base + "/medicine_name_en")) {
      slots[i].medicineName = fbdo.stringData();
    } else if (Firebase.getString(fbdo, base + "/medicine_name")) {
      slots[i].medicineName = fbdo.stringData();
    } else {
      slots[i].medicineName = "(empty)";
    }

    // ─── เก็บชื่อไทย ────────────────────────────
    if (Firebase.getString(fbdo, base + "/medicine_name")) {
      slots[i].medicineNameTH = fbdo.stringData();
    } else {
      slots[i].medicineNameTH = "";
    }

    // ─── ขนาด mg ────────────────────────────────
    if (Firebase.getInt(fbdo, base + "/dosage_mg")) {
      slots[i].dosageMg = fbdo.intData();
    } else {
      slots[i].dosageMg = 0;
    }

    // ─── enabled ────────────────────────────────
    if (Firebase.getBool(fbdo, base + "/enabled")) {
      slots[i].enabled = fbdo.boolData();
    } else {
      slots[i].enabled = false;
    }

    // ⭐ schedules[] — อ่านทีละ path ─────────────
    slots[i].scheduleCount = 0;

    for (int j = 0; j < MAX_SCHEDULES; j++) {
      String schedBase = base + "/schedules/" + String(j);

      String tTime = "";
      String tMeal = "";

      if (Firebase.getString(fbdo, schedBase + "/time")) {
        tTime = fbdo.stringData();
      }
      if (Firebase.getString(fbdo, schedBase + "/meal_relation")) {
        tMeal = fbdo.stringData();
      }

      // ถ้าไม่มีทั้ง time และ meal → จบ
      if (tTime == "" && tMeal == "") {
        break;
      }

      slots[i].schedules[slots[i].scheduleCount].time         = tTime;
      slots[i].schedules[slots[i].scheduleCount].mealRelation = tMeal;

      Serial.printf("    [%d] %s -> %s\n",
                    slots[i].scheduleCount,
                    tTime.c_str(),
                    tMeal.c_str());

      slots[i].scheduleCount++;
    }

    // ─── สรุป ───────────────────────────────────
    Serial.printf("slot%d: %s | %d mg | enabled=%d | schedules=%d\n",
                  i,
                  slots[i].medicineName.c_str(),
                  slots[i].dosageMg,
                  slots[i].enabled,
                  slots[i].scheduleCount);
  }

  Serial.println("--- Loading complete ---\n");
}

// ═══════════════════════════════════════════════════════════
//  ⑧ Notification Conditions
// ═══════════════════════════════════════════════════════════

bool isSlotEnabled(int slot) {
  return slots[slot].enabled;
}

int findMatchingSchedule(int slot, String nowTime) {
  for (int i = 0; i < slots[slot].scheduleCount; i++) {
    if (slots[slot].schedules[i].time == nowTime) return i;
  }
  return -1;
}

bool isAlreadyDone(String key) {
  if (!Firebase.getString(fbdo, "/events/" + key + "/status")) {
    return false;
  }
  EventStatus st = stringToStatus(fbdo.stringData());
  return (st == EVT_CONFIRMED || st == EVT_MISSED);
}

int shouldNotify(int slot, String today, String nowTime) {
  if (!isSlotEnabled(slot)) return -1;
  int idx = findMatchingSchedule(slot, nowTime);
  if (idx < 0) return -1;
  String key = makeEventKey(slot, today, nowTime);
  if (isAlreadyDone(key)) return -1;
  return idx;
}

// ═══════════════════════════════════════════════════════════
//  ⑨ Save Event + Notify User
// ═══════════════════════════════════════════════════════════

void saveEvent(String key, int slot, EventStatus status) {
  String base = "/events/" + key;
  Firebase.setInt   (fbdoEvent, base + "/slot",   slot);
  Firebase.setString(fbdoEvent, base + "/status", statusToString(status));
  Firebase.setInt   (fbdoEvent, base + "/ts",     (int)time(nullptr));

  if (status == EVT_CONFIRMED) {
    Firebase.setInt(fbdoEvent, base + "/confirmed_at", (int)time(nullptr));
  }

  Serial.printf("   [SAVE] %s -> %s\n",
                key.c_str(), statusToString(status).c_str());
}

void notifyUser(int slot, int schedIdx) {
  digitalWrite(BUZZER_PIN, HIGH);
  oledNotify(slot, schedIdx);

  Schedule &sc = slots[slot].schedules[schedIdx];
  String msg = slots[slot].medicineName + " "
             + String(slots[slot].dosageMg) + " mg "
             + mealRelationToEnglish(sc.mealRelation);

  Serial.println("[ALERT] slot" + String(slot)
                 + " time " + sc.time + ": " + msg);
}

void stopAlarm() {
  digitalWrite(BUZZER_PIN, LOW);
}

// ═══════════════════════════════════════════════════════════
//  ⑩ Button Wait
// ═══════════════════════════════════════════════════════════

bool waitButtonPressed(unsigned long timeoutMs) {
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    if (digitalRead(BUTTON_PIN) == LOW) {
      delay(50);
      if (digitalRead(BUTTON_PIN) == LOW) {
        while (digitalRead(BUTTON_PIN) == LOW) delay(10);
        return true;
      }
    }
    delay(10);
  }
  return false;
}

// ═══════════════════════════════════════════════════════════
//  ⑪ Auto-Reload
// ═══════════════════════════════════════════════════════════

void checkReload() {
  if (isNotifying) return;

  unsigned long nowMs = millis();

  if (nowMs - lastReload > RELOAD_INTERVAL) {
    lastReload = nowMs;
    Serial.println("[RELOAD] Refreshing slots from Firebase...");
    loadSlots();
  }

  String today = getDateString();
  if (today != "" && today != lastDate) {
    if (lastDate != "") {
      Serial.println("[RELOAD] New day: " + today);
      loadSlots();
    }
    lastDate = today;
  }
}

// ═══════════════════════════════════════════════════════════
//  ⑫ Setup
// ═══════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== MedBox — Heltec V4 ===");

  // ─── Pins ────────────────────────────────────
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // ⭐ เปิด Vext
  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, LOW);
  delay(100);

  // ⭐ Reset OLED
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW);
  delay(50);
  digitalWrite(OLED_RST, HIGH);
  delay(50);

  // ⭐ I2C
  Wire.begin(OLED_SDA, OLED_SCL);

  // ─── Scan I2C ────────────────────────────────
  Serial.println("Scan I2C...");
  byte found = 0;
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  Found 0x%02X\n", addr);
      found++;
    }
  }
  if (found == 0) Serial.println("  No I2C devices found");

  // ─── OLED Init ───────────────────────────────
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("[ERR] OLED init failed");
  } else {
    Serial.println("[OK] OLED ready");
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.display();

  // ─── Splash ──────────────────────────────────
  oledSplash();
  delay(1500);

  // ─── WiFi ────────────────────────────────────
  oledWaiting("Connecting WiFi...");
  Serial.print("[1/4] WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int try1 = 0;
  while (WiFi.status() != WL_CONNECTED && try1++ < 40) {
    Serial.print(".");
    delay(500);
  }
  if (WiFi.status() != WL_CONNECTED) {
    oledWaiting("WiFi Failed!");
    Serial.println("\n[ERROR] WiFi");
    delay(3000);
    ESP.restart();
  }
  Serial.println("\n[OK] WiFi: " + WiFi.localIP().toString());

  // ─── NTP ─────────────────────────────────────
  oledWaiting("Syncing time...");
  Serial.print("[2/4] NTP");
  configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");

  struct tm t;
  int try2 = 0;
  while (!getLocalTime(&t) && try2++ < 30) {
    Serial.print(".");
    delay(500);
  }
  if (try2 >= 30) {
    oledWaiting("NTP Failed!");
    Serial.println("\n[ERROR] NTP");
    delay(3000);
    ESP.restart();
  }
  Serial.println("\n[OK] Time: " + getDateString() + " " + getTimeString());

  // ─── Firebase ────────────────────────────────
  oledWaiting("Connecting Firebase...");
  Serial.println("[3/4] Firebase");
  Serial.print("Email: ");
  Serial.println(USER_EMAIL);

  config.host    = FIREBASE_HOST;
  config.api_key = FIREBASE_API_KEY;
  config.signer.tokens.legacy_token = "";

  auth.user.email    = USER_EMAIL;
  auth.user.password = USER_PASS;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  unsigned long t0 = millis();
  while (!Firebase.ready() && millis() - t0 < 15000) {
    Serial.print(".");
    delay(500);
  }

  if (Firebase.ready()) {
    Serial.println("\n[OK] Firebase login success");
  } else {
    Serial.println("\n[WARN] Firebase not ready");

    struct token_info_t info = Firebase.authTokenInfo();
    Serial.print("Token status: ");
    Serial.println(info.status);
    Serial.print("Error code: ");
    Serial.println(info.error.code);
    Serial.print("Error message: ");
    Serial.println(info.error.message.c_str());
  }

  // ─── Load Slots ──────────────────────────────
  oledWaiting("Loading medicine...");
  Serial.println("[4/4] Loading slots");
  loadSlots();

  // ─── Idle ────────────────────────────────────
  oledIdle();
  lastReload = millis();
  lastDate   = getDateString();
  Serial.println("=== Started ===\n");
}

// ═══════════════════════════════════════════════════════════
//  ⑬ Loop
// ═══════════════════════════════════════════════════════════

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    oledWaiting("WiFi Lost...");
    delay(2000);
    return;
  }

  if (!Firebase.ready()) {
    oledWaiting("Firebase Lost...");
    delay(2000);
    return;
  }

  checkReload();

  if (millis() - lastCheck < CHECK_INTERVAL) return;
  lastCheck = millis();

  String today = getDateString();
  String now   = getTimeString();
  if (today == "" || now == "") return;

  bool didNotify = false;

  for (int slot = 1; slot <= NUM_SLOTS; slot++) {
    int idx = shouldNotify(slot, today, now);
    if (idx < 0) continue;

    isNotifying = true;
    didNotify = true;
    String key = makeEventKey(slot, today, now);

    saveEvent(key, slot, EVT_NOTIFIED);
    notifyUser(slot, idx);

    if (waitButtonPressed(BUTTON_TIMEOUT)) {
      saveEvent(key, slot, EVT_CONFIRMED);
      Serial.println("   [OK] User confirmed");
      oledConfirmed();
      delay(2000);
    } else {
      saveEvent(key, slot, EVT_MISSED);
      Serial.println("   [TIMEOUT] missed");
      oledMissed();
      delay(2000);
    }

    stopAlarm();
    isNotifying = false;
  }

  if (!didNotify) {
    oledIdle();
  }
}