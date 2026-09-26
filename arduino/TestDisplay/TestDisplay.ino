#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64

// ⭐ ใช้ขา Reset จริงของ Heltec V4
#define OLED_RESET    21 

// สร้าง object display บน I2C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

void setup() {
  Serial.begin(115200);
  delay(1000);

  // ⭐ 1. เปิด Vext (จ่ายไฟให้จอ) — ต้องทำก่อนทุกอย่าง
  pinMode(Vext, OUTPUT);      // Vext ถูก define ไว้ใน board package แล้ว
  digitalWrite(Vext, LOW);    // LOW = เปิด (Active LOW)
  delay(100);

  // ⭐ 2. เริ่ม I2C ด้วยขาที่ถูกต้องของ Heltec V4
  Wire.begin(17, 18);         // SDA=17, SCL=18

  // ⭐ 3. Init OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 allocation failed");
    for(;;);
  }

  // ⭐ 4. ทดสอบวาดข้อความ
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println("Heltec V4 + Adafruit");
  display.display();

  Serial.println("OLED ทำงานแล้ว");
}

void loop() {
  // โค้ดหลักของคุณ
}