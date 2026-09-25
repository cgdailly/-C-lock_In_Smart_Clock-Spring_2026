#include <stdlib.h>
#include <TimeLib.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <IRremote.hpp>
#include <SPI.h>
#include <MFRC522.h>
#include <CountDown.h>

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define IR_INPUT_PIN 4

#define SDA_RFID_PIN 42
#define SCK_RFID_PIN 41
#define MOSI_RFID_PIN 40
#define MISO_RFID_PIN 39
#define RST_RFID_PIN 38

#define RX_PIN 44
#define TX_PIN 43

#define RELAY_PIN 5

#define LED_PIN 6
#define NUMPIXELS 30

#define SPEAKER_PIN 15

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint8_t brightness = 0;

uint8_t alarm_hour;
uint8_t alarm_min;
uint8_t LED_hour;
uint8_t LED_min;
bool LED_rise = false;
bool alarm_armed = false;
bool alarm_sound = false;

uint8_t pomodoro_cycles = 0;
bool pomodoro_break = false;
uint8_t pomodoro_toggle = false;

uint8_t disarm_challenge = 0; // 0 == button press, 1 == base, 2 == game

typedef struct pack {
  uint8_t start;
  uint8_t min;
  uint8_t hour;
  uint8_t challengeType;
  uint8_t x;
  uint8_t func;
  uint8_t y;
  char function;
} screen_data;

screen_data display;
screen_data incoming;

uint8_t curr_hour = 0;
uint8_t curr_min = 0;

CountDown lock(CountDown::MINUTES);
bool unlockable = false;
bool countdown = false;

uint8_t x;
uint8_t function;
uint8_t y;

Adafruit_NeoPixel strip(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);
HardwareSerial MegaSerial(1);
MFRC522 rfid(SDA_RFID_PIN, RST_RFID_PIN);

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// THREADING

TaskHandle_t clockTask;
TaskHandle_t computation;

SemaphoreHandle_t shared;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// CLOCK HELPERS

/*

  Take an input value and send it to the screen to be displayed

*/

void setTimeFromInput(String timeStr) {
  int h, m, s;

  if (sscanf(timeStr.c_str(), "%d:%d", &h, &m) == 2) {
    if (h >= 0 && h < 24 && m >= 0 && m < 60) {
      sendToMega(m, h, 0, 0, 0, 'A');
    } else {
      Serial.println("Use HH:MM");
    }
  } else {
    Serial.println("Use HH:MM");
  }
}

// SCREEN HELPERS

/*

  Updates display and sends it to the screen

*/

void sendToMega(uint8_t min, uint8_t hour, uint8_t xSend, uint8_t funcSend, uint8_t ySend, char func) {
  display.start = 0xAA;
  display.min = min;
  display.hour = hour;
  display.challengeType = disarm_challenge;
  display.x = xSend;
  display.func = funcSend;
  display.y = ySend;
  display.function = func;

  MegaSerial.write((uint8_t*)&display, sizeof(display));
}

/*

  Take an input from the screen and updates incoming, reacts according to the function

  Z - Time Updates
  X - Reset

*/

void readTimeFromMega() {
  while (MegaSerial.available() > 0) {
    uint8_t b = MegaSerial.read();

    if (b == 0xAA) {
      while (MegaSerial.available() < 7) {
        vTaskDelay(pdMS_TO_TICKS(1));
      }

      uint8_t incoming_min = MegaSerial.read();
      uint8_t incoming_hour = MegaSerial.read();
      uint8_t challenge = MegaSerial.read();
      uint8_t xx = MegaSerial.read();
      uint8_t funcc = MegaSerial.read();
      uint8_t yy = MegaSerial.read();
      char incoming_function = MegaSerial.read();

      if (incoming_function == 'Z') {
        curr_min = incoming_min;
        curr_hour = incoming_hour;

        Serial.print("Time received from Mega: ");
        Serial.print(curr_hour);
        Serial.print(":");
        Serial.println(curr_min);
      }
    
      if (incoming_function == 'Y') {
        pomodoroTriggered();
      }

      if (incoming_function == 'X') {
        statusX();
      }
    
    }
  }
}

void statusX() {
  brightness = 0;

  alarm_hour = NULL;
  alarm_min = NULL;
  alarm_armed = false;
  alarm_sound = false;

  pomodoro_cycles = 0;
  pomodoro_break = false;
  pomodoro_toggle = false;

  ledcWriteTone(SPEAKER_PIN, 0);
  strip.setBrightness(0);
  setAll(0, 0, 0);
}

// LED HELPERS

/*

  Takes an RGB code and updates all the LEDs accordingly, showing the new updates

*/

void setAll(uint8_t r, uint8_t g, uint8_t b) {
  for (int i = 0; i < NUMPIXELS; i++) {
    strip.setPixelColor(i, strip.Color(r, g, b));
  }
  strip.show();
}

// IR Helpers

uint8_t readIR() {
  if (IrReceiver.decode()) {
    if (!(IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT)) {
      uint8_t command = IrReceiver.decodedIRData.command;
      IrReceiver.resume();
      return command;
    }
    IrReceiver.resume();
  }
  return 0x00;
}

/*

  Names the button based on the address received

*/

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

/*

  Sets the new time by taking in four number values, updates the time on the screen

*/

void setClockIR() {
  Serial.println("Setting Clock...");
  int inputs = 0;
  int minute = 0;
  int hour = 0;
  unsigned long start = millis();
  while (inputs < 4 && millis() - start < 10000) {
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

  if (hour >= 24 || minute >= 60) {
    Serial.println("Invalid time entered. Use HHMM, where HH < 24 and MM < 60.");
    return;
  }

  sendToMega(minute, hour, 0, 0, 0, 'A');

}

/*

  Updates the alarm variables and takes four number inputs from the remote to set the alarm trigger

*/

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
  unsigned long start = millis();
  while (inputs < 4 && millis() - start < 10000) {
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

  if (hour >= 24 || minute >= 60) {
    Serial.println("Invalid time entered. Use HHMM, where HH < 24 and MM < 60.");
    return;
  }

  alarm_hour = hour;
  alarm_min = minute;
  alarm_armed = true;

  if (alarm_min >= 25) {
    LED_hour = alarm_hour;
    LED_min = alarm_min - 25;
  } else {
    LED_hour = alarm_hour - 1;
    LED_min = 60 - (-1)*(alarm_min - 25);
  }

  Serial.print("LED hour and minute: ");
  Serial.println(LED_hour);
  Serial.println(LED_min);

  Serial.println("Alarm armed!");

}

// ALARM HELPERS

/*

  Updates alarm variables and sets off noise and reaction, also starts 30 minute timer for phone unlocking (1 minute for testing)

*/

void alarmTriggered() {
  sendToMega(0,0,0,0,0,'C');
  lockBox();
  while (true) {
    alarm_sound = true;
    if (alarmDisarm()) break;
  }
  alarm_sound = false;
  if (alarm_armed) {
    alarm_armed = false;
    lock.start(0,0,1);
    countdown = true;
    unlockable = false;
  } else {
    unlockBox();
    unlockable = false;
    sendToMega(0,0,0,0,0,'C');
  }
}

/*

  Function to check if the base has been connected to turn the alarm off

*/

bool checkBase() {
  Serial.println("Checking RFID...");

  if (!rfid.PICC_IsNewCardPresent()) {
    return false;
  }

  if (!rfid.PICC_ReadCardSerial()) {
    return false;
  }

  Serial.println("RFID Present <3");

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  return true;
}

bool alarmDisarm() {
  if (disarm_challenge == 0) {  // Simple Button Press on Remote
    return (readIR() == 0x45);
  } else if (disarm_challenge == 1) {
    return checkBase();
  } else if (disarm_challenge == 2) {
    return mathGame();
  }
  return false;
}

bool mathGame() {
  int wins = 0;
  while (wins < 4) {
    x = random(0, 25);
    y = random(0, 25);
    function = random(0, 3);
    int ans;

    sendToMega(0, 0, x, function, y, 'G');

    switch (function) {
      case 0:
        ans = x + y;
        break;
      case 1:
        if (x >= y) {
          ans = x - y;
        } else {
          ans = y - x;
        }
        break;
      default:
        ans = x * y;
        
    }

    int input = 0;

    while (true) {
      uint8_t cmd = readIR();

      if (cmd == 0x19) {
        break;
      }

      const char* button = getButtonName(cmd);

      if (button[0] >= '0' && button[0] <= '9') {
        input = input * 10 + atoi(button);
        delay(200);
      }
    }

    if (input == ans) {
      wins++;
    }

  }
  if (pomodoro_toggle) {
    sendToMega(0,0,0,0,0,'B');
  } else {
    sendToMega(0,0,0,0,0,'A');
  }
  return true;
}

// POMODORO HELPERS

/*

  Increments the pomodoro cycles and updates them on the Mega, turns the pomodoro variable on if it hasn't been activated already

*/

void setPomodoro() {
  pomodoro_cycles++;
  if (!pomodoro_break) {
    lockBox();
  }
  if (!pomodoro_toggle) {
    pomodoro_toggle = true;
    sendToMega(0, 0, 0, 0, 0, 'B');
  }
  Serial.print(pomodoro_cycles);
  Serial.println(" Pomodoro Cycles Scheduled!");
  sendToMega(0,0,0,0,0,'C');
}

/*

  Reacts according to different triggers, whether the box is coming from a break or work session

*/

void pomodoroTriggered() {
    if (!pomodoro_break) {
      pomodoro_cycles--;
      unlockable = true;
      sendToMega(0,0,0,0,0,'D');
      if (pomodoro_cycles == 0) {
        alarm_sound = false;
        pomodoro_toggle = false;
        pomodoro_break = false;
        sendToMega(0, 0, 0, 0, 0, 'E');
        return;
      }
      pomodoro_break = true;
      sendToMega(0, 0, 0, 0, 0, 'F');
    } else {
      pomodoro_break = false;
      lockBox();
      sendToMega(0,0,0,0,0,'B');
      // TRIGGER ALARM UNTIL RELOCKED
    }
}

// SOLENOID HELPERS

/*

  Locks the box by disconnecting the solenoid

*/

void lockBox() {
  sendToMega(0,0,0,0,0,'C');
  digitalWrite(RELAY_PIN, LOW);
  unlockable = false;
}

/*

  Unlocks solenoid by connecting power

*/

void unlockBox() {
  sendToMega(0,0,0,0,0,'D');
  digitalWrite(RELAY_PIN, HIGH);
  delay(10000);
  digitalWrite(RELAY_PIN, LOW);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void clockBasics(void *pvParameters) {
  Serial.print("Clock basics running on core ");
  Serial.println(xPortGetCoreID());

  for (;;) {
    
    /*

      Reads time and updates variables, checks for alarm trigger, pomodoro trigger, and LED timing

    */

    readTimeFromMega();

    if (alarm_sound) {
      ledcWriteTone(SPEAKER_PIN, 98);
      delay(200);

      ledcWriteTone(SPEAKER_PIN, 0);
      delay(200);
    }

    static int lastMinute = -1;
    if (curr_min != lastMinute) {
      lastMinute = curr_min;

      // Update LEDs for Wake Up

      if (alarm_armed && curr_hour == LED_hour && curr_min == LED_min) {
        LED_rise = true;
        brightness += 10;
        Serial.println("Light activated!");
      } else if (alarm_armed && LED_rise == true) {
        brightness += 10;
      } else {
        brightness = 0;
        LED_rise = false;
      }

      strip.setBrightness(brightness);
      setAll(255, 50, 0);
    }

  vTaskDelay(pdMS_TO_TICKS(10));
  }
  
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void clockComps(void *pvParameters) {
  Serial.print("Clock computations running on core ");
  Serial.println(xPortGetCoreID());

  for (;;) {

    // Alarm Check

  // if (rfid.PICC_IsNewCardPresent()) {
  //   Serial.println("RFID Present <3");
  // } else {
  //   Serial.println("No card reading");  
  // }

    if (alarm_armed && curr_hour == alarm_hour && curr_min == alarm_min) {
      alarmTriggered();
    }

    if (countdown && lock.remaining() == 0) {
      countdown = false;
      unlockable = true;
      sendToMega(0,0,0,0,0,'D');
    }

    /*

      Takes in user interaction, reacts internally to all ESP jobs (setting clock, setting alarm, setting pomodoro, turning off alarms, opening lock)

    */

    if (IrReceiver.decode()) {
      if (!(IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT)) {
        uint8_t cmd = IrReceiver.decodedIRData.command;

        if (cmd == 0x0D) {
          setClockIR();
        }

        if (cmd == 0x47) {
          setAlarm();
        }

        if (cmd == 0x46) {
          setPomodoro();
        }

        if (cmd == 0x43) {
          disarm_challenge = (disarm_challenge + 1) % 3;
          Serial.print("Disarm challenge: ");
          Serial.println(disarm_challenge);

          sendToMega(curr_min, curr_hour, 0, 0, 0, 'A');
        }

        if (cmd == 0x40) {
          Serial.println("Unlocking box <3");
          if (unlockable) {
            unlockBox();
          } else {
            alarmTriggered();
          }
        }
      }
      IrReceiver.resume();
    }

  vTaskDelay(pdMS_TO_TICKS(10));
  }

}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void setup() {

  Serial.begin(115200);

  display.start = 0xAA;

  randomSeed(analogRead(0));

  // IR SETUP

  IrReceiver.begin(IR_INPUT_PIN, DISABLE_LED_FEEDBACK);

  Serial.println("IR Setup Complete <3");

  // RFID SETUP

  SPI.begin(SCK_RFID_PIN, MISO_RFID_PIN, MOSI_RFID_PIN, SDA_RFID_PIN);
  rfid.PCD_Init();

  Serial.println("RFID Setup Complete <3");

  // SCREEN SETUP

  MegaSerial.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);

  Serial.println("Screen Setup Complete <3");

  // RELAY SETUP

  pinMode(RELAY_PIN, OUTPUT);

  Serial.println("Relay Setup Complete <3");

  // LED SETUP

  strip.begin();
  strip.show();

  Serial.println("LED Setup Complete <3");  

  // THREADING SETUP

  shared = xSemaphoreCreateMutex();

  xTaskCreatePinnedToCore(clockBasics, "Basics", 8192, NULL, 1, &clockTask, 0);
  xTaskCreatePinnedToCore(clockComps, "Computations", 4096, NULL, 1, &computation, 1);

  // SPEAKER SETUP

  ledcAttach(SPEAKER_PIN, 2000, 8);

  sendToMega(0,0,0,0,0,'H');

}

void loop() {
}
