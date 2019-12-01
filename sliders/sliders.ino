#include "MIDIUSB.h"

class Slider {
 public:
  Slider(int pin, int cc) : cc_num_(cc), pin_(pin) {
    pinMode(pin_, INPUT);
  }
  void Update() {
    uint32_t sense = analogRead(pin_);
    uint32_t cc = sense >> 3;
  
    if (cc != last_cc_ && abs((int)last_sense_ - (int)sense) > 7) {
      last_sense_ = sense;
      last_cc_ = cc;
      Serial.println(cc);
      MidiUSB.sendMIDI({0x0B, 0xB0 | 0, cc_num_, cc});
      MidiUSB.flush();
    }
  }
 private:
  uint8_t pin_;
  uint8_t cc_num_;
  uint32_t last_sense_ = 0;
  uint32_t last_cc_ = 0;
};

auto mod = Slider(A2, 1);
auto vib = Slider(A1, 21);

void setup() {
  Serial.begin(115200);
}

void loop() {
  mod.Update();
  vib.Update();
  delay(5);
}
