/* ═══════════════════════════════════════════════════════════
   MedBox — Smart Medicine Box 6 Slots
   Board: Heltec WiFi LoRa 32 V4
   ──────────────────────────────────────────────────────────
   Logic การแจ้งเตือน:
     - before_meal / after_meal → window ±15 นาที
     - immediate               → window ±5 นาที
     - กดใน window → confirmed
     - หมด window → missed อัตโนมัติ
   ═══════════════════════════════════════════════════════════ */

#include <WiFi.h>
#include <FirebaseESP32.h>
#include <time.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ═══════════════════════════════════════════════════════════
//  ENTER / EXIT MACROS
// ═══════════════════════════════════════════════════════════

#define ENTER(fn)   Serial.printf(">>> [ENTER] %s\n", fn)
#define EXIT(fn)    Serial.printf("<<< [EXIT]  %s\n", fn)

// ═══════════════════════════════════════════════════════════
//  ① Configuration
// ═══════════════════════════════════════════════════════════

#define WIFI_SSID     "chompunut"
#define WIFI_PASSWORD "Pgssupply14364"

#define FIREBASE_HOST    "basic-firebase-web-1d69a-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_API_KEY "AIzaSyABfYla0viy6ZBb_ULavp3G3A_4o4lk9qg"
#define USER_EMAIL       "esp32@test.com"
#define USER_PASS        "Esp32Pass123!"

// ─── Hardware ────────────────────────────────────────
#define BUTTON_PIN    4
#define BUZZER_PIN    5

// ─── OLED (Heltec V4) ───────────────────────────────
#define OLED_SDA      17
#define OLED_SCL      18
#define OLED_RST      21
#define OLED_ADDR     0x3C
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64

// ─── Constants ───────────────────────────────────────
#define NUM_SLOTS          6
#define MAX_SCHEDULES      10
#define CHECK_INTERVAL     1000UL
#define BUTTON_TIMEOUT     30000UL      // รอกด 30 วิต่อรอบ
#define RELOAD_INTERVAL    30000UL
#define HEARTBEAT_INTERVAL 30000UL

// ⭐ Window (±นาที)
#define WINDOW_NORMAL      15          // ±15 นาที (before/after meal)
#define WINDOW_IMMEDIATE   5           // ±5 นาที (immediate)

// ═══════════════════════════════════════════════════════════
//  ② Objects
// ═══════════════════════════════════════════════════════════

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST);

FirebaseData   fbdo;
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
unsigned long lastCheck       = 0;
unsigned long lastReload      = 0;
unsigned long lastHeartbeat   = 0;

String activityStatus   = "idle";
String activityDate     = "";
int    activitySlot     = 0;
String activityMedicine = "";
int    activityDosageMg = 0;

bool isNotifying = false;

// ═══════════════════════════════════════════════════════════
//  ④ Helpers — ไม่มี ENTER/EXIT
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

String makeFlagKey(int slot, String time) {
  return "slot" + String(slot) + "_" + time;
}

String mealRelationToEnglish(String rel) {
  if (rel == "before_meal") return "Before meal";
  if (rel == "after_meal")  return "After meal";
  if (rel == "immediate")   return "Immediate";
  return "";
}

// ─── เวลา ───
int timeToMinutes(String hhmm) {
  if (hhmm.length() != 5) return -1;
  int hh = hhmm.substring(0, 2).toInt();
  int mm = hhmm.substring(3, 5).toInt();
  if (hh < 0 || hh > 23 || mm < 0 || mm > 59) return -1;
  return hh * 60 + mm;
}

// ⭐ ตรวจว่าอยู่ใน window ±windowMin ไหม (รองรับข้ามเที่ยงคืน)
bool isInWindow(int nowMin, int targetMin, int windowMin) {
  int diff = nowMin - targetMin;
  if (diff < -720) diff += 1440;
  if (diff >  720) diff -= 1440;
  return (diff >= -windowMin && diff <= windowMin);
}

// ⭐ หา window ตาม meal_relation
int getWindowMinutes(String mealRelation) {
  if (mealRelation == "immediate") return WINDOW_IMMEDIATE;   // ±5
  return WINDOW_NORMAL;                                        // ±15
}

// ⭐ เช็คว่าเลย window ไปแล้วไหม (สำหรับ missed)
bool isPastWindow(int nowMin, int targetMin, int windowMin) {
  int diff = nowMin - targetMin;
  if (diff < -720) diff += 1440;
  if (diff >  720) diff -= 1440;
  return (diff > windowMin);
}

// ═══════════════════════════════════════════════════════════
//  ⑤ OLED
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
//  ⑥ Load Slots
// ═══════════════════════════════════════════════════════════

void loadSlots() {
  ENTER("loadSlots");
  Serial.printf("[loadSlots] heap=%d\n", ESP.getFreeHeap());

  Serial.println("\n--- Loading medicine data ---");

  for (int i = 1; i <= NUM_SLOTS; i++) {
    String base = "/medicine_box/slots/slot" + String(i);

    if (Firebase.getString(fbdo, base + "/medicine_name_en")) {
      slots[i].medicineName = fbdo.stringData();
    } else if (Firebase.getString(fbdo, base + "/medicine_name")) {
      slots[i].medicineName = fbdo.stringData();
    } else {
      slots[i].medicineName = "(empty)";
    }

    if (Firebase.getString(fbdo, base + "/medicine_name")) {
      slots[i].medicineNameTH = fbdo.stringData();
    } else {
      slots[i].medicineNameTH = "";
    }

    if (Firebase.getInt(fbdo, base + "/dosage_mg")) {
      slots[i].dosageMg = fbdo.intData();
    } else {
      slots[i].dosageMg = 0;
    }

    if (Firebase.getBool(fbdo, base + "/enabled")) {
      slots[i].enabled = fbdo.boolData();
    } else {
      slots[i].enabled = false;
    }

    slots[i].scheduleCount = 0;

    for (int j = 0; j < MAX_SCHEDULES; j++) {
      if (slots[i].scheduleCount >= MAX_SCHEDULES) break;

      String schedBase = base + "/schedules/" + String(j);
      String tTime = "";
      String tMeal = "";

      if (Firebase.getString(fbdo, schedBase + "/time")) {
        String s = fbdo.stringData();
        if (s.length() > 0 && s.length() < 10) tTime = s;
      }
      if (Firebase.getString(fbdo, schedBase + "/meal_relation")) {
        String s = fbdo.stringData();
        if (s.length() > 0 && s.length() < 20) tMeal = s;
      }

      if (tTime == "" && tMeal == "") break;

      int idx = slots[i].scheduleCount;
      if (idx >= 0 && idx < MAX_SCHEDULES) {
        slots[i].schedules[idx].time         = tTime;
        slots[i].schedules[idx].mealRelation = tMeal;
        slots[i].scheduleCount++;
      }
    }

    Serial.printf("slot%d: %s | %d mg | enabled=%d | schedules=%d\n",
                  i, slots[i].medicineName.c_str(), slots[i].dosageMg,
                  slots[i].enabled, slots[i].scheduleCount);

    for (int j = 0; j < slots[i].scheduleCount; j++) {
      Serial.printf("    [%d] %s -> %s\n", j,
                    slots[i].schedules[j].time.c_str(),
                    slots[i].schedules[j].mealRelation.c_str());
    }
  }

  Serial.println("--- Loading complete ---\n");

  if (Firebase.ready()) {
    Firebase.setInt(fbdo, "/activity/last_check/slots_load", (int)time(nullptr));
  }

  Serial.printf("[loadSlots] heap after=%d\n", ESP.getFreeHeap());
  EXIT("loadSlots");
}

// ═══════════════════════════════════════════════════════════
//  ⑦ Event History
// ═══════════════════════════════════════════════════════════

void saveEvent(String key, int slot, EventStatus status) {
  ENTER("saveEvent");

  String base = "/events/" + key;

  Firebase.setInt   (fbdo, base + "/slot",   slot);
  Firebase.setString(fbdo, base + "/status", statusToString(status));
  Firebase.setInt   (fbdo, base + "/ts",     (int)time(nullptr));

  if (status == EVT_CONFIRMED) {
    Firebase.setInt(fbdo, base + "/confirmed_at", (int)time(nullptr));
  }

  Serial.printf("   [HISTORY] %s -> %s\n",
                key.c_str(), statusToString(status).c_str());

  EXIT("saveEvent");
}

// ═══════════════════════════════════════════════════════════
//  ⑧ Activity + Flags
// ═══════════════════════════════════════════════════════════

bool flagExists(int slot, String time) {
  ENTER("flagExists");

  String flagKey = makeFlagKey(slot, time);
  String path    = "/activity/current/flags/" + flagKey;

  bool exists = false;
  if (Firebase.getString(fbdo, path)) {
    exists = true;
  }

  EXIT("flagExists");
  return exists;
}

String getFlag(int slot, String time) {
  String flagKey = makeFlagKey(slot, time);
  String path    = "/activity/current/flags/" + flagKey;

  if (Firebase.getString(fbdo, path)) {
    return fbdo.stringData();
  }
  return "";
}

void writeFlagAndEvent(int slot, String time, String status) {
  ENTER("writeFlagAndEvent");

  String flagKey  = makeFlagKey(slot, time);
  String flagPath = "/activity/current/flags/" + flagKey;

  if (status.length() > 0) {
    Firebase.setString(fbdo, flagPath, status);
  }

  delay(20);

  String today    = getDateString();
  String eventKey = makeEventKey(slot, today, time);

  EventStatus evtStatus;
  if (status == "confirmed")     evtStatus = EVT_CONFIRMED;
  else if (status == "missed")   evtStatus = EVT_MISSED;
  else                            evtStatus = EVT_NOTIFIED;

  saveEvent(eventKey, slot, evtStatus);

  Serial.printf("[FLAG] %s -> %s\n", flagKey.c_str(), status.c_str());

  EXIT("writeFlagAndEvent");
}

void writeActivity(String status) {
  ENTER("writeActivity");

  if (status.length() > 0) {
    activityStatus = status;
  }

  if (activityDate.length() == 0) {
    activityDate = getDateString();
  }

  String base = "/activity/current";

  Firebase.setString(fbdo, base + "/status", activityStatus);

  if (activityDate.length() > 0) {
    Firebase.setString(fbdo, base + "/date", activityDate);
  }

  Firebase.setInt(fbdo, base + "/slot", activitySlot);

  if (activityMedicine.length() > 0) {
    Firebase.setString(fbdo, base + "/medicine", activityMedicine);
  }

  Firebase.setInt(fbdo, base + "/dosage_mg", activityDosageMg);
  Firebase.setInt(fbdo, base + "/updated_at", (int)time(nullptr));
  Firebase.setString(fbdo, base + "/source", "esp32");

  Serial.printf("[ACTIVITY] %s | slot%d\n", activityStatus.c_str(), activitySlot);

  EXIT("writeActivity");
}

void activityNotify(int slot, int schedIdx) {
  ENTER("activityNotify");

  activitySlot     = slot;
  activityMedicine = slots[slot].medicineName;
  activityDosageMg = slots[slot].dosageMg;

  writeActivity("notified");

  EXIT("activityNotify");
}

void activityConfirm() {
  ENTER("activityConfirm");
  writeActivity("confirmed");
  EXIT("activityConfirm");
}

void activityMissed() {
  ENTER("activityMissed");
  writeActivity("missed");
  EXIT("activityMissed");
}

void activityIdle() {
  ENTER("activityIdle");

  activitySlot     = 0;
  activityMedicine = "";
  activityDosageMg = 0;

  writeActivity("idle");

  EXIT("activityIdle");
}

// ═══════════════════════════════════════════════════════════
//  ⑨ Heartbeat
// ═══════════════════════════════════════════════════════════

void writeHeartbeat() {
  ENTER("writeHeartbeat");

  String base = "/activity/last_check";

  Firebase.setInt   (fbdo, base + "/esp32_seen", (int)time(nullptr));
  Firebase.setString(fbdo, base + "/date",       activityDate);
  Firebase.setString(fbdo, base + "/time",       getTimeString());
  Firebase.setInt   (fbdo, base + "/free_heap",  ESP.getFreeHeap());

  Serial.printf("[HEARTBEAT] heap=%d\n", ESP.getFreeHeap());

  EXIT("writeHeartbeat");
}

// ═══════════════════════════════════════════════════════════
//  ⑩ Notification Conditions — ใช้ Window ±15
// ═══════════════════════════════════════════════════════════

int shouldNotify(int slot, String today, String nowTime) {
  ENTER("shouldNotify");

  if (isNotifying) {
    EXIT("shouldNotify (busy)");
    return -1;
  }
  if (!slots[slot].enabled) {
    EXIT("shouldNotify (disabled)");
    return -1;
  }

  int nowMin = timeToMinutes(nowTime);
  if (nowMin < 0) {
    EXIT("shouldNotify (bad time)");
    return -1;
  }

  for (int i = 0; i < slots[slot].scheduleCount; i++) {
    String schedTime = slots[slot].schedules[i].time;
    int    targetMin = timeToMinutes(schedTime);
    if (targetMin < 0) continue;

    int windowMin = getWindowMinutes(slots[slot].schedules[i].mealRelation);

    // ⭐ ต้องอยู่ใน window
    if (!isInWindow(nowMin, targetMin, windowMin)) continue;

    // ⭐ มี flag แล้วหรือยัง?
    String flag = getFlag(slot, schedTime);
    if (flag.length() > 0) {
      // มี flag (notified/confirmed/missed) → ข้าม
      continue;
    }

    // ✅ ต้องแจ้ง!
    EXIT("shouldNotify (MATCH)");
    return i;
  }

  EXIT("shouldNotify (no match)");
  return -1;
}

// ⭐ ตรวจ expiry + เขียน missed อัตโนมัติ
void checkExpiredSchedules() {
  ENTER("checkExpiredSchedules");

  String now    = getTimeString();
  int    nowMin = timeToMinutes(now);
  if (nowMin < 0) {
    EXIT("checkExpired (bad time)");
    return;
  }

  for (int slot = 1; slot <= NUM_SLOTS; slot++) {
    if (!slots[slot].enabled) continue;

    for (int j = 0; j < slots[slot].scheduleCount; j++) {
      String schedTime = slots[slot].schedules[j].time;
      int    targetMin = timeToMinutes(schedTime);
      if (targetMin < 0) continue;

      int windowMin = getWindowMinutes(slots[slot].schedules[j].mealRelation);

      String flag = getFlag(slot, schedTime);

      // ⭐ เงื่อนไข: flag = "notified" + หมด window → missed
      if (flag == "notified") {
        if (isPastWindow(nowMin, targetMin, windowMin)) {
          Serial.printf("[EXPIRED] slot%d %s → missed\n",
                        slot, schedTime.c_str());

          writeFlagAndEvent(slot, schedTime, "missed");
          activityMissed();
          delay(500);
          activityIdle();
        }
      }
    }
  }

  EXIT("checkExpiredSchedules");
}

// ═══════════════════════════════════════════════════════════
//  ⑪ Notify User
// ═══════════════════════════════════════════════════════════

void notifyUser(int slot, int schedIdx, String time) {
  ENTER("notifyUser");

  digitalWrite(BUZZER_PIN, HIGH);
  oledNotify(slot, schedIdx);

  writeFlagAndEvent(slot, time, "notified");
  activityNotify(slot, schedIdx);

  Schedule &sc = slots[slot].schedules[schedIdx];
  Serial.println("[ALERT] slot" + String(slot)
                 + " time " + sc.time
                 + " | " + slots[slot].medicineName
                 + " | " + String(slots[slot].dosageMg) + "mg");

  EXIT("notifyUser");
}

void stopAlarm() {
  digitalWrite(BUZZER_PIN, LOW);
}

// ═══════════════════════════════════════════════════════════
//  ⑫ Button
// ═══════════════════════════════════════════════════════════

bool waitButtonPressed(unsigned long timeoutMs) {
  ENTER("waitButtonPressed");

  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    if (digitalRead(BUTTON_PIN) == LOW) {
      delay(50);
      if (digitalRead(BUTTON_PIN) == LOW) {
        while (digitalRead(BUTTON_PIN) == LOW) delay(10);
        EXIT("waitButtonPressed (pressed)");
        return true;
      }
    }
    delay(10);
  }

  EXIT("waitButtonPressed (timeout)");
  return false;
}

// ═══════════════════════════════════════════════════════════
//  ⑬ Auto-Reload + Day Change
// ═══════════════════════════════════════════════════════════

void checkReload() {
  if (isNotifying) return;

  unsigned long nowMs = millis();
  if (nowMs - lastReload > RELOAD_INTERVAL) {
    ENTER("checkReload");
    lastReload = nowMs;
    Serial.println("[RELOAD] Refreshing slots...");
    loadSlots();
    EXIT("checkReload");
  }
}

void checkDayChange() {
  ENTER("checkDayChange");

  String today = getDateString();

  if (today == "") {
    EXIT("checkDayChange (no date)");
    return;
  }

  if (activityDate == "") {
    activityDate = today;
    EXIT("checkDayChange (init)");
    return;
  }

  if (activityDate == today) {
    EXIT("checkDayChange (same)");
    return;
  }

  Serial.printf("[DAY CHANGE] %s -> %s\n",
                activityDate.c_str(), today.c_str());

  Firebase.deleteNode(fbdo, "/activity/current/flags");
  Serial.println("   Cleared all flags");

  activitySlot     = 0;
  activityMedicine = "";
  activityDosageMg = 0;
  activityDate     = today;
  writeActivity("idle");

  loadSlots();

  EXIT("checkDayChange (changed)");
}

// ═══════════════════════════════════════════════════════════
//  ⑭ Setup
// ═══════════════════════════════════════════════════════════

void setup() {
  ENTER("setup");

  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== MedBox — Heltec V4 ===");

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // ─── Vext ───
  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, LOW);
  delay(100);

  // ─── OLED RST ───
  pinMode(OLED_RST, OUTPUT);
  digitalWrite(OLED_RST, LOW);
  delay(50);
  digitalWrite(OLED_RST, HIGH);
  delay(50);

  // ─── I2C ───
  Wire.begin(OLED_SDA, OLED_SCL);

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

  // ─── OLED Init ───
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("[ERR] OLED init failed");
  } else {
    Serial.println("[OK] OLED ready");
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.display();

  oledSplash();
  delay(1500);

  // ─── WiFi ───
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

  // ─── NTP ───
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

  // ─── Firebase ───
  oledWaiting("Connecting Firebase...");
  Serial.println("[3/4] Firebase");

  config.host    = FIREBASE_HOST;
  config.api_key = FIREBASE_API_KEY;
  config.signer.tokens.legacy_token = "";

  config.timeout.socketConnection = 30 * 1000;
  config.timeout.serverResponse   = 10 * 1000;

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
    Serial.print("Error: ");
    Serial.println(info.error.message.c_str());
  }

  delay(2000);
  Serial.printf("[setup] heap before loadSlots: %d\n", ESP.getFreeHeap());

  // ─── Load Slots ───
  if (Firebase.ready()) {
    oledWaiting("Loading medicine...");
    Serial.println("[4/4] Loading slots");
    loadSlots();
  } else {
    Serial.println("[SKIP] Firebase not ready");
  }

  delay(500);
  Serial.printf("[setup] heap after loadSlots: %d\n", ESP.getFreeHeap());

  // ─── Init Activity ───
  if (Firebase.ready()) {
    activityDate = getDateString();
    writeActivity("idle");
    writeHeartbeat();
  }

  oledIdle();
  lastReload    = millis();
  lastHeartbeat = millis();

  Serial.println("=== Started ===\n");

  EXIT("setup");
}

// ═══════════════════════════════════════════════════════════
//  ⑮ Loop
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

  // Heartbeat
  if (millis() - lastHeartbeat > HEARTBEAT_INTERVAL) {
    lastHeartbeat = millis();
    writeHeartbeat();
  }

  checkDayChange();
  checkReload();

  // ⭐ เช็คหมดเวลา → เขียน missed
  checkExpiredSchedules();

  // ─── เช็คทุก 1 วินาที ───
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

    String schedTime = slots[slot].schedules[idx].time;

    notifyUser(slot, idx, schedTime);

    if (waitButtonPressed(BUTTON_TIMEOUT)) {
      // ✅ กดทันเวลา → confirmed
      Serial.println("   [OK] User confirmed");
      writeFlagAndEvent(slot, schedTime, "confirmed");
      activityConfirm();
      oledConfirmed();
      delay(2000);
    } else {
      // ⏰ หมดรอบ 30 วิ — ยังอยู่ใน window → รอรอบถัดไป
      // ไม่เขียน missed ที่นี่ — ให้ checkExpiredSchedules() จัดการ
      Serial.println("   [WAIT] 30s passed — still in window, waiting");
    }

    stopAlarm();
    activityIdle();
    isNotifying = false;
  }

  if (!didNotify) {
    oledIdle();
  }
}