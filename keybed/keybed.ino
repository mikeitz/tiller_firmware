#include "MIDIUSB.h"

#define VMIN 3000
#define VMAX 50000

const uint8_t rowScan[8] = {2, 4, 8, 16, 32, 64, 128, 1};

uint8_t last1[8], last2[8];
unsigned long time1[64], time2[64];

void show(uint8_t b) {
  for (int i = 0; i < 8; ++i) {
    Serial.print((int)(b >> i) & 1);
  }
}

uint8_t note(uint8_t i, uint8_t j) {
  return j * 8 + i + 32;
}

uint8_t vel(long t) {
  t = (t - VMIN) * 127 / VMAX;
  t = 127 - t;
  return min(127, max(1, t));
}

void noteOn(uint8_t i, uint8_t j, unsigned long t) {
  uint8_t n = note(i, j);
  uint8_t v = vel(t);
  MidiUSB.sendMIDI({0x09, 0x90, n, v});
  MidiUSB.flush();
  /*Serial.print("ON ");
  Serial.print(n);
  Serial.print(" ");
  Serial.println(vel(t));
  Serial.flush();*/
}

void noteOff(uint8_t i, uint8_t j, unsigned long t) {
  uint8_t n = note(i, j);
  uint8_t v = vel(t);
  MidiUSB.sendMIDI({0x08, 0x80, n, v});
  MidiUSB.flush();
  /*Serial.print("OFF ");
  Serial.print(n);
  Serial.print(" ");
  Serial.println(vel(t));
  Serial.flush();*/
}

void updateScan(unsigned long t, uint8_t i, uint8_t scan1, uint8_t scan2) {
  if (scan1 != last1[i]) {
    for (uint8_t j = 0; j < 7; ++j) {
      uint8_t b = 1 << j;
      uint8_t k = 8 * i + j;
      if ((scan1 & b) && !(last1[i] & b)) { // released
        if (time2[k] != 0 && time1[k] == 0) {
          noteOff(i, j, t - time2[k]);
        }
        time1[k] = time2[k] = 0;
      }
      if (!(scan1 & b) && (last1[i] & b)) { // pressed
        time1[k] = t;
      }
    }
    last1[i] = scan1;
  }
  if (scan2 != last2[i]) {
    for (uint8_t j = 0; j < 7; ++j) {
      uint8_t b = 1 << j;
      uint8_t k = 8 * i + j;
      if ((scan2 & b) && !(last2[i] & b)) { // released
        time2[k] = t;
      }
      if (!(scan2 & b) && (last2[i] & b)) { // pressed
        if (time1[k] != 0 && time2[k] == 0) {
          noteOn(i, j, t - time1[k]);
        }
        time1[k] = time2[k] = 0;
      }
    }
    last2[i] = scan2;
  }
}

void setup() {
  DDRD = 255;
  PORTD = 254;
  
  DDRC = 0;
  PORTC = 255;

  DDRB = 0;
  PORTB = 255;
  
  Serial.begin(115200);
  memset(last1, 255, 8);
  memset(last2, 255, 8);
  memset(time1, 0, 64 * 4);
  memset(time2, 0, 64 * 4);
}

void loop() {
  for(uint8_t i = 0; i < 8; ++i) {
    unsigned long t = micros();
    uint8_t scan1 = PINB, scan2 = PINC;
    PORTD = ~rowScan[i];
    updateScan(t, i, scan1, scan2);
  }
}
