#include <Arduino.h>
#include "BluetoothSerial.h"
#include <IRremote.h>

BluetoothSerial SerialBT;

// ==== Pins ====
#define LIN_TX_PIN 17
#define CS_PIN 16
#define IR_PIN 4

#define LIN_BAUD 19200

// ==== Modes ====
enum Mode { OFF, STATIC, JUMP3, FADE3, JUMP7 };
Mode currentMode = OFF;

// ==== Globals ====
uint8_t colorIndex = 0;
uint16_t brightness = 1023;
bool ledEnabled = false;

// ==== Timing ====
unsigned long lastSend = 0;
unsigned long lastEffect = 0;

// ==== Colors ====
const uint8_t colors[][3] = {
  {255,0,0}, {0,255,0}, {0,0,255}, {255,255,255}, 
  {255,255,0}, {255,165,0}, {211,130,198}, {93,180,255}
};
#define NUM_COLORS 8

// ==== JUMP7 ====
uint16_t j7Bright = 0;
bool rampUp = true;

// ==== FUNCTION DECLARATIONS ====
void sendLIN(uint8_t id,uint8_t* data,uint8_t len);
void sendColor(const uint8_t* rgb, uint16_t b, uint8_t modeByte);
uint8_t calcPID(uint8_t id);
uint8_t checksum(uint8_t pid,uint8_t* data,uint8_t len);
void sendBreak();

// ================= SETUP =================
void setup() {
  Serial.begin(LIN_BAUD, SERIAL_8N1, -1, LIN_TX_PIN);
  SerialBT.begin("RGB light engine controller");

  pinMode(LIN_TX_PIN, OUTPUT);
  pinMode(CS_PIN, OUTPUT);
  digitalWrite(CS_PIN, HIGH);

  IrReceiver.begin(IR_PIN, ENABLE_LED_FEEDBACK);

  delay(500);

  uint8_t wake[8]={0};
  sendLIN(0x49,wake,8);
}

// ================= LOOP =================
void loop() {

  // ===== BLE INPUT =====
  if (SerialBT.available()) {
    String cmd = SerialBT.readStringUntil('\n');
    cmd.trim();
    cmd.toUpperCase();

    if(cmd=="ON"){ ledEnabled=true; currentMode=STATIC; }
    else if(cmd=="OFF"){ ledEnabled=false; currentMode=OFF; }

    else if(cmd=="RED"){colorIndex=0; currentMode=STATIC; ledEnabled=true;}
    else if(cmd=="GREEN"){colorIndex=1; currentMode=STATIC; ledEnabled=true;}
    else if(cmd=="BLUE"){colorIndex=2; currentMode=STATIC; ledEnabled=true;}
    else if(cmd=="WHITE"){colorIndex=3; currentMode=STATIC; ledEnabled=true;}
    else if(cmd=="YELLOW"){colorIndex=4; currentMode=STATIC; ledEnabled=true;}
    else if(cmd=="ORANGE"){colorIndex=5; currentMode=STATIC; ledEnabled=true;}
    else if(cmd=="PURPLE"){colorIndex=6; currentMode=STATIC; ledEnabled=true;}
    else if(cmd=="CYAN"){colorIndex=7; currentMode=STATIC; ledEnabled=true;}
    
    else if(cmd=="JUMP3"){currentMode=JUMP3; ledEnabled=true;}
    else if(cmd=="FADE3"){currentMode=FADE3; ledEnabled=true;}
    else if(cmd=="JUMP7"){currentMode=JUMP7; ledEnabled=true; j7Bright = 0; 
    rampUp = true; colorIndex = 0;}

    else if(cmd=="B+"){ brightness = min(1023, brightness+100); }
    else if(cmd=="B-"){ brightness = max(0, brightness-100); }

    else if(cmd.startsWith("B_")) {
      int val = cmd.substring(2).toInt();
      brightness = constrain(val,0,1023);
    }
  }

  // ===== IR INPUT =====
  if (IrReceiver.decode()) {
    uint32_t code = IrReceiver.decodedIRData.decodedRawData;

    switch(code) {
      case 0xFC0310: ledEnabled=false; currentMode=OFF; break;
      case 0xFD0210: ledEnabled=true; currentMode=STATIC; break;

      case 0xFB0410: colorIndex=0; currentMode=STATIC; break;
      case 0xFA0510: colorIndex=1; currentMode=STATIC; break;
      case 0xF90610: colorIndex=2; currentMode=STATIC; break;
      case 0xF80710: colorIndex=3; currentMode=STATIC; break;

      case 0xF30C10: currentMode=JUMP3; break;
      case 0xF10E10: currentMode=FADE3; break;

      case 0xF20D10: 
        currentMode=JUMP7; 
        j7Bright = 0;
        rampUp = true;
        colorIndex = 0;   // FIX
        break;

      case 0xFF0010: brightness=min(1023,brightness+100); break;
      case 0xFE0110: brightness=max(0,brightness-100); break;
    }

    IrReceiver.resume();
  }

  if (!ledEnabled) return;

  // ===== STATIC =====
  if (currentMode == STATIC) {
    if (millis()-lastSend > 1500) {
      sendColor(colors[colorIndex], brightness, 0x2A);
      lastSend = millis();
    }
  }

  // ===== JUMP3 =====
  else if (currentMode == JUMP3) {
    if (millis()-lastEffect > 800) {
      lastEffect = millis();
      colorIndex = (colorIndex+1)%3;
      sendColor(colors[colorIndex], brightness, 0xF3);
    }
  }

  // ===== FADE3 =====
  else if (currentMode == FADE3) {
    if (millis()-lastEffect > 1500) {
      lastEffect = millis();
      colorIndex = (colorIndex+1)%3;
      sendColor(colors[colorIndex], brightness, 0x2A);
    }
  }

  // ===== JUMP7 ===== //
  else if (currentMode == JUMP7) {

    if (millis()-lastEffect > 40) {
      lastEffect = millis();

      if (rampUp) { j7Bright += 10;
        if (j7Bright >= 1013) { j7Bright = 1023; rampUp = false; }
      } 
      else {j7Bright -= 10;
        if (j7Bright <= 10) { j7Bright = 0; rampUp = true;
          colorIndex = (colorIndex+1)%NUM_COLORS;}
      }

      sendColor(colors[colorIndex], j7Bright, 0xF3); 
    }
  }
  //j7Bright = 0;
}

// ================= SEND COLOR =================
void sendColor(const uint8_t* rgb, uint16_t b, uint8_t modeByte) {

  uint8_t b0 = 0xA3 | ((b & 0x3F) << 2);
  uint8_t b1 = (b >> 6) & 0x0F;

  uint8_t frame[8] = {b0,b1,0xFF,0xFF,rgb[0],rgb[1],rgb[2],modeByte};

  sendLIN(0x1A, frame, 8);
}

// ================= LIN =================
void sendBreak() {
  Serial.flush();
  Serial.end();

  digitalWrite(LIN_TX_PIN, LOW);
  delayMicroseconds(900);
  digitalWrite(LIN_TX_PIN, HIGH);
  delayMicroseconds(200);

  Serial.begin(LIN_BAUD, SERIAL_8N1, -1, LIN_TX_PIN);
}

uint8_t calcPID(uint8_t id) {
  uint8_t p0=((id>>0)^(id>>1)^(id>>2)^(id>>4))&1;
  uint8_t p1=~((id>>1)^(id>>3)^(id>>4)^(id>>5))&1;
  return (id&0x3F)|(p0<<6)|(p1<<7);
}

uint8_t checksum(uint8_t pid,uint8_t* data,uint8_t len){
  uint16_t sum=pid;
  for(int i=0;i<len;i++){
    sum+=data[i];
    if(sum>255) sum=(sum&255)+1;
  }
  return ~sum;
}

void sendLIN(uint8_t id,uint8_t* data,uint8_t len){
  sendBreak();
  Serial.write(0x55);
  uint8_t pid=calcPID(id);
  Serial.write(pid);
  for(int i=0;i<len;i++) Serial.write(data[i]);
  Serial.write(checksum(pid,data,len));
}