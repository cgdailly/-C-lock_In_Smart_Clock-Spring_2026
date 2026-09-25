#include <stdlib.h>
#include <TimeLib.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <IRremote.hpp>

uint8_t brightness = 0;

uint8_t alarm_hour;
uint8_t alarm_min;
bool alarm_armed = false;

uint8_t pomodoro_cycles = 0;
bool pomodoro_break = false;
uint8_t pomodoro_toggle = false;
uint8_t pomodoro_min = 0;

uint8_t disarm_challenge; // 0 == button press, 1 == shake, 2 == base, 3 == game

typedef struct pack {
  uint8_t start;
  uint8_t min;
  uint8_t hour;
  char function;
} screen_data;

screen_data display;

#define ALARM_PIN 4
#define SDA_PIN 6
#define SCL_PIN 7
#define LED_STRIP 15
#define RX_PIN 16
#define TX_PIN 17
#define IR_PIN 23

#define NUMPIXELS 30

Adafruit_NeoPixel strip(NUMPIXELS, LED_STRIP, NEO_GRB + NEO_KHZ800);
HardwareSerial MegaSerial(1);

/////////////////////////////////////////////////////////////////////////////////

void setup() {
  Serial.begin(115200);

  display.start = 0xAA;

  // Screen SETUP

  MegaSerial.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);

  Serial.println("Screen Setup Complete <3");

  // LED SETUP

  strip.begin();
  strip.show();

  Serial.println("LED Setup Complete <3");

  // IR SETUP

  IrReceiver.begin(IR_PIN, DISABLE_LED_FEEDBACK);

  Serial.println("IR Setup Complete <3");

  // ALARM SETUP

  pinMode(ALARM_PIN, OUTPUT);

}

////////////////////////////////////////////////////////////////////////////////

void loop() {

  if (alarm_armed && hour() == alarm_hour && minute() == alarm_min) {
    alarmTriggered();
  }

  if (pomodoro_toggle == true && minute() == pomodoro_min) {
    pomodoroTriggered();
  }

  static int lastMinute = -1;
  if (minute() != lastMinute) {
    Serial.println("Start of loop");

    lastMinute = minute();
    displayCurrentTime();

    if ((alarm_armed && alarm_min >= 25 && hour() == alarm_hour && alarm_min - minute() < 25) || (alarm_armed && alarm_min < 25 && hour() == alarm_hour - 1 && 60 + alarm_min - minute() < 25)) {
      brightness += 10;
    } else if (alarm_armed && alarm_min == minute() && alarm_hour == hour()) {
      brightness = 255;
    } else {
      brightness = 0;
    }

    strip.setBrightness(brightness);
    setAll(255, 50, 0);
    Serial.println("End of loop");
  }

  if (IrReceiver.decode()) {
    if (!(IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT)) {
      uint8_t cmd = IrReceiver.decodedIRData.command;

      if (cmd == 0x45) {
        setClockIR();
      }

      if (cmd == 0x47) {
        setAlarm();
      }

      if (cmd == 0x46) {
        setPomodoro();
      }
    }

    IrReceiver.resume();
  }
}

//////////////////////////////////////////////////////////////////////////////////

// CLOCK HELPERS

void setTimeFromInput(String timeStr) {
  int h, m, s;

  if (sscanf(timeStr.c_str(), "%d:%d", &h, &m) == 2) {
    if (h >= 0 && h < 24 && m >= 0 && m < 60) {
      setTime(h, m, 0, 1, 1, 2024);
    } else {
      Serial.println("Use HH:MM");
    }
  } else {
    Serial.println("Use HH:MM");
  }
}

// SCREEN HELPERS

void displayCurrentTime() {
  Serial.println("Sending Time...");
  display.min = minute();
  display.hour = hour();
  display.function = 'A';
  
  MegaSerial.write((uint8_t*)&display, sizeof(display));
}


// LED HELPERS

void setAll(uint8_t r, uint8_t g, uint8_t b) {
  for (int i = 0; i < NUMPIXELS; i++) {
    strip.setPixelColor(i, strip.Color(r, g, b));
  }
  strip.show();
}

// IR Helpers

const char* getButtonName(uint8_t cmd) {
  switch (cmd) {
    case 0x45: return "POWER";
    case 0x46: return "VOL+";
    case 0x47: return "FUNC/STOP";
    case 0x44: return "<<";
    case 0x40: return ">||";
    case 0x43: return ">>";
    case 0x07: return "DOWN";
    case 0x15: return "VOL-";
    case 0x09: return "UP";
    case 0x16: return "0";
    case 0x19: return "EQ";
    case 0x0D: return "ST/REPT";
    case 0x0C: return "1";
    case 0x18: return "2";
    case 0x5E: return "3";
    case 0x08: return "4";
    case 0x1C: return "5";
    case 0x5A: return "6";
    case 0x42: return "7";
    case 0x52: return "8";
    case 0x4A: return "9";
    default:   return "UNKNOWN";
  }
}

void setClockIR() {
  Serial.println("Setting Clock...");
  int inputs = 0;
  int minute = 0;
  int hour = 0;
  while (inputs < 4) {
    if (IrReceiver.decode()) {
      if (!(IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT)) {
        uint8_t cmd = IrReceiver.decodedIRData.command;

        if (cmd == 0x16 || cmd == 0x0C || cmd == 0x18 || cmd == 0x5E|| cmd == 0x08 || cmd == 0x1C || cmd == 0x5A || cmd == 0x42 || cmd == 0x52 || cmd == 0x4A) {
          
          int in = atoi(getButtonName(cmd));
          
          if (inputs == 0) {
            hour = in * 10;
            Serial.println(hour);
          } else if (inputs == 1) {
            hour += in;
            Serial.println(hour);
          } else if (inputs == 2) {
            minute = in * 10;
            Serial.println(minute);
          } else if (inputs == 3) {
            minute += in;
            Serial.println(minute);
          }

          inputs++;
        }
      }

      IrReceiver.resume();
    }
  }
  setTime(hour, minute, 0, 1, 1, 2024);
}

void setAlarm() {
  if (alarm_armed == true) {
    Serial.println("Alarm disarmed!");
    alarm_armed = false;
    return;
  }

  Serial.println("Setting alarm...");
  int inputs = 0;
  int minute = 0;
  int hour = 0;
  while (inputs < 4) {
    if (IrReceiver.decode()) {
      if (!(IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT)) {
        uint8_t cmd = IrReceiver.decodedIRData.command;

        if (cmd == 0x16 || cmd == 0x0C || cmd == 0x18 || cmd == 0x5E|| cmd == 0x08 || cmd == 0x1C || cmd == 0x5A || cmd == 0x42 || cmd == 0x52 || cmd == 0x4A) {
          
          int in = atoi(getButtonName(cmd));
          
          if (inputs == 0) {
            hour = in * 10;
            Serial.println(hour);
          } else if (inputs == 1) {
            hour += in;
            Serial.println(hour);
          } else if (inputs == 2) {
            minute = in * 10;
            Serial.println(minute);
          } else if (inputs == 3) {
            minute += in;
            Serial.println(minute);
          }

          inputs++;
        }
      }

      IrReceiver.resume();
    }
  }

  alarm_hour = hour;
  alarm_min = minute;
  alarm_armed = true;
  Serial.println("Alarm armed!");

}

// ALARM HELPERS

void alarmTriggered() {
  while (true) {
    if (display.min != minute()) displayCurrentTime();
    digitalWrite(ALARM_PIN, HIGH);
    delay(1000);
    digitalWrite(ALARM_PIN, LOW);
    delay(1000);
  }
}

// POMODORO HELPERS

void setPomodoro() {
  pomodoro_cycles++;
  Serial.print(pomodoro_cycles);
  Serial.println(" Pomodoro Cycles Scheduled!");
  if (pomodoro_toggle == false) {
    pomodoro_toggle = true;
    pomodoro_min = (uint8_t)((int)minute() + 20) % 60;
    Serial.println(pomodoro_min);
  }
}

void pomodoroTriggered() {
    if (pomodoro_break == false) {
      pomodoro_cycles--;
      if (pomodoro_cycles == 0) {
        // OPEN LOCK, TRIGGER
        pomodoro_toggle = false;
        return;
      }
      pomodoro_break = true;
      pomodoro_min = (minute() + 5) % 60;
      // OPEN LOCK, TRIGGER
    } else {
      pomodoro_break = false;
      pomodoro_min = (minute() + 20) % 60;
      // CLOSE LOCK, TRIGGER
    }
}

