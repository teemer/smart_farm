#define BLYNK_TEMPLATE_ID "TMPL6vZZWiTyf"
#define BLYNK_TEMPLATE_NAME "test1"
#define BLYNK_AUTH_TOKEN "DpINAJ7Ra0jyyvEPSvCDlpFvLoua7JVy"

#define BLYNK_PRINT Serial

#define RX2_PIN 16
#define TX2_PIN 17

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RTClib.h>
#include <Preferences.h>

char auth[] = BLYNK_AUTH_TOKEN;
char ssid[] = "Panna 2.4G";
char pass[] = "panna_int";

// --- จอ OLED I2C 128x64 (SSD1306) ต่อขา SDA=21, SCL=22 (ขา I2C มาตรฐานของ ESP32) ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- RTC DS3231 (มีถ่านแบตในตัว จำเวลาได้แม้ไฟดับ) ---
RTC_DS3231 rtc;
bool rtcReady = false;

// --- เก็บค่าตั้งค่าลง Flash (NVS) กันไฟดับ ---
Preferences prefs;

// --- ส่วนที่เพิ่มสำหรับ EMA 30 ---
float emaValue = 0;       // ตัวแปรเก็บค่า EMA
float alpha = 2.0 / (100.0 + 1.0); // ค่า Alpha สำหรับ EMA 30 (ประมาณ 0.0645)
bool firstRun = true;     // เช็คการทำงานครั้งแรกเพื่อตั้งค่าเริ่มต้น
// -----------------------------

int moisture_1_sensor = 0;
// int moisture_2_sensor = 0;
int offset_moisture_1 = 0;
int offset_moisture_2 = 0;

BlynkTimer timer;

bool auto_status_1 = 0;
bool auto_status_2 = 0;
bool relay1_state = false;   // สถานะรีเลย์ตัวที่ 1 (ไว้โชว์จอ)
bool lastRelay2State = false;

float moisture_2_sensor    = 0;
float temperature = 0;
int   ec          = 0;
float ph          = 0;
int   nitrogen    = 0;
int   phosphorus  = 0;
int   potassium   = 0;

// สร้างตัวแปรเก็บข้อมูลที่เซนเซอร์ตอบกลับมา (ทั้งหมด 19 Bytes)
byte responseBuffer[19];
int byteCount = 0;

// ฟังก์ชันสำหรับอ่านค่าอนาล็อกแบบหาค่าเฉลี่ย
void sendAnalogValue() {
  moisture_1_sensor = map(emaValue, 3050, 2230, 0, 100);
  // Serial.print("Filtered Analog Value (Pin 35): ");
  Serial.println(moisture_1_sensor);
  Blynk.virtualWrite(V1, moisture_1_sensor); // ส่งค่าเฉลี่ยที่นิ่งแล้วไป Blynk
  Blynk.virtualWrite(V6, moisture_2_sensor);
  Blynk.virtualWrite(V7, temperature);
  Blynk.virtualWrite(V8, ec);
  Blynk.virtualWrite(V9, ph*10);
  Blynk.virtualWrite(V10, nitrogen);
  Blynk.virtualWrite(V11, phosphorus);
  Blynk.virtualWrite(V12, potassium);
}

void get_lola_sensor(){
  byte dataToSend[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x07, 0x04, 0x08};
  Serial2.write(dataToSend, sizeof(dataToSend));
}

// จอไม่พอเลยแบ่งเป็นหลายหน้า สลับกันแสดงอัตโนมัติ
const int DISPLAY_PAGE_COUNT = 2;
int displayPage = 0;

void cycleDisplayPage() {
  displayPage = (displayPage + 1) % DISPLAY_PAGE_COUNT;
}

// แถวหัวจอ (เวลา + สถานะ Blynk) ใช้ร่วมกันทุกหน้า
void drawStatusHeader() {
  display.setCursor(0, 0);
  if (rtcReady) {
    DateTime now = rtc.now();
    char buf[9];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
    display.print(buf);
  } else {
    display.print("--:--:--");
  }
  display.setCursor(80, 0);
  display.print(Blynk.connected() ? "Blynk:ON" : "Blynk:OFF");
  display.drawLine(0, 9, 127, 9, SSD1306_WHITE);
}

// หน้า 1: ความชื้น + Setpoint แต่ละจุด + Auto mode + Relay
void drawPageMoisture() {
  display.setCursor(0, 14);
  display.print("M1:"); display.print(moisture_1_sensor); display.print("%");
  display.setCursor(70, 14);
  display.print("SP:"); display.print(offset_moisture_1); display.print("%");

  display.setCursor(0, 26);
  display.print("M2:"); display.print(moisture_2_sensor, 0); display.print("%");
  display.setCursor(70, 26);
  display.print("SP:"); display.print(offset_moisture_2); display.print("%");

  display.drawLine(0, 37, 127, 37, SSD1306_WHITE);

  display.setCursor(0, 42);
  display.print("Auto1:"); display.print(auto_status_1 ? "ON " : "OFF");
  display.setCursor(70, 42);
  display.print("Auto2:"); display.print(auto_status_2 ? "ON" : "OFF");

  display.setCursor(0, 54);
  display.print("R1:"); display.print(relay1_state ? "OPEN " : "CLOSE");
  display.setCursor(70, 54);
  display.print("R2:"); display.print(lastRelay2State ? "OPEN" : "CLOSE");
}

// หน้า 2: อุณหภูมิ / EC / pH / NPK
void drawPageSoil() {
  display.setCursor(0, 14);
  display.print("Temp:"); display.print(temperature, 1); display.print(" C");

  display.setCursor(0, 24);
  display.print("EC  :"); display.print(ec); display.print(" uS");

  display.setCursor(0, 34);
  display.print("pH  :"); display.print(ph, 1);

  display.setCursor(0, 46);
  display.print("N:"); display.print(nitrogen);
  display.setCursor(45, 46);
  display.print("P:"); display.print(phosphorus);
  display.setCursor(90, 46);
  display.print("K:"); display.print(potassium);
}

void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  drawStatusHeader();

  switch (displayPage) {
    case 0: drawPageMoisture(); break;
    case 1: drawPageSoil();     break;
  }

  display.display();
}

BLYNK_WRITE(V0) {
  int pinValue = param.asInt();
  relay1_state = (pinValue == 1);
  if (relay1_state) {
    digitalWrite(23, HIGH);
    digitalWrite(19, HIGH);
  } else {
    digitalWrite(23, LOW);
    digitalWrite(19, LOW);
  }
}
BLYNK_WRITE(V4) { // offset
  int v = param.asInt();
  if (v != offset_moisture_1) {
    offset_moisture_1 = v;
    prefs.putInt("off1", offset_moisture_1); // เก็บลง flash กันไฟดับ
  }
}
BLYNK_WRITE(V14) { // offset
  int v = param.asInt();
  if (v != offset_moisture_2) {
    offset_moisture_2 = v;
    prefs.putInt("off2", offset_moisture_2); // เก็บลง flash กันไฟดับ
  }
}

BLYNK_WRITE(V2) { //Auto mode main moisture
  bool v = (param.asInt() == 1);
  if (v != auto_status_1) {
    auto_status_1 = v;
    prefs.putBool("auto1", auto_status_1); // เก็บลง flash กันไฟดับ
    Serial.println(auto_status_1 ? "auto 1" : "auto off");
  }
}

BLYNK_WRITE(V15) { //Auto mode main moisture
  bool v = (param.asInt() == 1);
  if (v != auto_status_2) {
    auto_status_2 = v;
    prefs.putBool("auto2", auto_status_2); // เก็บลง flash กันไฟดับ
    Serial.println(auto_status_2 ? "auto 2" : "auto off");
  }
}

BLYNK_WRITE(V5) { // offset
  int data = param.asInt();
  if (data == 1){
    // 1. สร้าง Array เก็บค่า Hex ไว้ (ใช้ 0x นำหน้าเพื่อบอกว่าเป็นเลขฐานสิบหก)
    byte dataToSend[] = {0x01, 0x05, 0x00, 0x00, 0xFF, 0x00, 0x8C, 0x3A};
    // 2. ใช้ Serial2.write(array, size) เพื่อส่งข้อมูลออกไปทั้งหมด
    Serial2.write(dataToSend, sizeof(dataToSend));
  }else{
    // 1. สร้าง Array เก็บค่า Hex ไว้ (ใช้ 0x นำหน้าเพื่อบอกว่าเป็นเลขฐานสิบหก)
    byte dataToSend[] = {0x01, 0x05, 0x00, 0x00, 0x00, 0x00, 0xCD, 0xCA};
    // 2. ใช้ Serial2.write(array, size) เพื่อส่งข้อมูลออกไปทั้งหมด
    Serial2.write(dataToSend, sizeof(dataToSend));
  }
}

// ปุ่ม Refresh บนแอป (Virtual Pin V16, Push Button) กดแล้วส่งค่าล่าสุดขึ้น Blynk ทันที ไม่ต้องรอ 15 นาที
BLYNK_WRITE(V16) {
  if (param.asInt() == 1) {
    sendAnalogValue();
  }
}

// เรียกอัตโนมัติทันทีที่ ESP32 เชื่อมต่อกับ Blynk server สำเร็จ (ตอนบูต หรือหลุด WiFi แล้วต่อกลับ)
BLYNK_CONNECTED() {
  sendAnalogValue();
}


void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RX2_PIN, TX2_PIN);

  pinMode(23, OUTPUT);
  pinMode(19, OUTPUT);
  pinMode(4, OUTPUT);
  pinMode(35, INPUT);

  Wire.begin(); // SDA=21, SCL=22 (ขา I2C มาตรฐานของ ESP32)

  // --- โหลดการตั้งค่าที่เคยบันทึกไว้กลับมา (รอด แม้ไฟดับ) ---
  prefs.begin("farm", false);
  offset_moisture_1 = prefs.getInt("off1", 0);
  offset_moisture_2 = prefs.getInt("off2", 0);
  auto_status_1 = prefs.getBool("auto1", false);
  auto_status_2 = prefs.getBool("auto2", false);

  // --- จอ OLED ---
  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Smart Farm booting...");
    display.display();
  } else {
    Serial.println("SSD1306 not found");
  }

  // --- RTC DS3231 ---
  if (rtc.begin()) {
    rtcReady = true;
    if (rtc.lostPower()) {
      // ตั้งเวลาจากตอน compile ครั้งแรกที่เจอ (เช่น แบตหมด/ต่อใหม่ครั้งแรก)
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
  } else {
    Serial.println("RTC DS3231 not found");
  }

  Blynk.begin(auth, ssid, pass);
  // อ่านค่าเซนเซอร์ + คุม auto ทุก 5 วิ (ต้องไวเพื่อความแม่นยำของการรดน้ำอัตโนมัติ)
  timer.setInterval(5000L, get_lola_sensor);
  // ส่งค่าขึ้นแอป Blynk ทุก 15 นาที (ประหยัด data ของ Blynk)
  // ค่าแรกจะถูกส่งทันทีผ่าน BLYNK_CONNECTED() ด้านบน ไม่ต้องรอ 15 นาทีแรก
  timer.setInterval(15UL * 60UL * 1000UL, sendAnalogValue);
  timer.setInterval(1000L, updateDisplay);
  timer.setInterval(4000L, cycleDisplayPage); // สลับหน้าจอทุก 4 วิ
}

void loop() {
  Blynk.run();
  timer.run(); 
  int sensorValue = analogRead(35);
  if (firstRun) {
    emaValue = sensorValue;
    firstRun = false;
  } else {
    emaValue = (sensorValue * alpha) + (emaValue * (1.0 - alpha));
  }

  if (auto_status_1){
    relay1_state = (moisture_1_sensor <= offset_moisture_1);
    if (relay1_state){
      digitalWrite(23, HIGH);
      digitalWrite(19, HIGH);
    }else {
      digitalWrite(23, LOW);
      digitalWrite(19, LOW);
    }
  }

  if (auto_status_2) {
      bool shouldOpen = (moisture_2_sensor <= offset_moisture_2);

      if (shouldOpen != lastRelay2State) {
          if (shouldOpen) {
              byte dataToSend[] = {0x01, 0x05, 0x00, 0x00, 0xFF, 0x00, 0x8C, 0x3A};
              Serial2.write(dataToSend, sizeof(dataToSend));
          } else {
              byte dataToSend[] = {0x01, 0x05, 0x00, 0x00, 0x00, 0x00, 0xCD, 0xCA};
              Serial2.write(dataToSend, sizeof(dataToSend));
          }
          lastRelay2State = shouldOpen;
      }
  }

  while (Serial2.available() > 0) {
    byte incomingByte = Serial2.read();

    // ชั้นที่ 1: กรองหัว Packet (ถ้า byteCount เป็น 0 ตัวที่เข้ามาต้องเป็น Address 0x01 เท่านั้น)
    if (byteCount == 0 && incomingByte != 0x01) {
      continue; // ถ้าไม่ใช่ 0x01 ให้ข้ามไปเลย ไม่เก็บ
    }

    // ชั้นที่ 2: กรองฟังก์ชัน (ถ้า byteCount เป็น 1 ตัวที่เข้ามาต้องเป็น Function 0x03 เท่านั้น)
    if (byteCount == 1 && incomingByte != 0x03) {
      byteCount = 0; // ถ้าไม่ใช่ แสดงว่าไม่ใช่หัวขบวนจริง ให้รีเซ็ตเริ่มหาใหม่
      continue;
    }

    // ชั้นที่ 3: กรองความยาวข้อมูล (ถ้า byteCount เป็น 2 ตัวที่เข้ามาต้องบอกว่ายาว 14 bytes หรือ 0x0E)
    if (byteCount == 2 && incomingByte != 0x0E) {
      byteCount = 0; // ถ้าไม่ใช่ ให้รีเซ็ตเริ่มหาใหม่
      continue;
    }

    // ถ้าผ่านเงื่อนไขกรองด้านบนมาได้ จึงนำมาเก็บลง Buffer
    if (byteCount < 19) {
      responseBuffer[byteCount] = incomingByte;
      byteCount++;
    }

    // หน่วงเวลาเล็กน้อยเพื่อให้ตัวถัดไปมาทัน
    delay(5); 
  }

  // เมื่อกรองจนเก็บข้อมูลครบหัวท้าย 19 Bytes แน่ๆ แล้ว ค่อยประมวลผล
  if (byteCount == 19) {
    
    // ดึงค่าต่างๆ ออกมาคำนวณอย่างปลอดภัย
    moisture_2_sensor    = ((responseBuffer[3] << 8) | responseBuffer[4]) / 10.0;
    temperature = ((responseBuffer[5] << 8) | responseBuffer[6]) / 10.0;
    ec          = (responseBuffer[7] << 8) | responseBuffer[8];
    ph          = ((responseBuffer[9] << 8) | responseBuffer[10]) / 100.0;
    nitrogen    = (responseBuffer[11] << 8) | responseBuffer[12];
    phosphorus  = (responseBuffer[13] << 8) | responseBuffer[14];
    potassium   = (responseBuffer[15] << 8) | responseBuffer[16];

    // แสดงผลออก Serial Monitor
    Serial.println("--- [ผ่านการกรอง] ข้อมูลสภาพดิน ---");
    Serial.print("ความชื้น: "); Serial.print(moisture_2_sensor); Serial.println(" %");
    Serial.print("อุณหภูมิ: "); Serial.print(temperature); Serial.println(" °C");
    Serial.print("ค่า EC: "); Serial.print(ec); Serial.println(" uS/cm");
    Serial.print("ค่า pH: "); Serial.print(ph); Serial.println();
    Serial.print("N: "); Serial.print(nitrogen); Serial.print(" | P: "); Serial.print(phosphorus); Serial.print(" | K: "); Serial.println(potassium);
    Serial.println("--------------------------------\n");

    // ประมวลผลเสร็จแล้วเคลียร์ตัวนับเพื่อเริ่มสแกนหา Packet ถัดไป
    byteCount = 0;
  }
}