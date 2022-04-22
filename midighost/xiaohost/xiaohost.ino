#include "Wire.h"

// https://github.com/mikeitz/USB_Host_Library_SAMD (based on fork with interrupts)
#include <usbh_midi.h>
#include <usbhub.h>

USBHost UsbH;
USBHub Hub(&UsbH);
USBH_MIDI Midi(&UsbH);

void setup() {
    Wire.begin();
    if (UsbH.Init()) {
        while (true) {
            delay(1000);
        }
    }
}

void loop() {
    UsbH.Task();
    if (!Midi) {
        return;
    }

    uint8_t buffer[1000];
    uint16_t count = 0;
    memset(buffer, 0, 1000);
    if (Midi.RecvData(&count, buffer) != 0 || count == 0) {
        return;
    }
    bool allzeros = true;
    for (int i = 0; i < count; ++i) {
        if (buffer[i] != 0) {
            allzeros = false;
            break;
        }
    }
    if (allzeros) {
        return;
    }
    Wire.beginTransmission(0);
    Wire.write(buffer, count);
    Wire.endTransmission();

    /*
        while (true) {
            uint16_t count;
            if (!Midi.RecvData(&count, buffer)) {
                break;
            }
            if (count == 0) {
                break;
            }
        }*/




        /*
        uint16_t count = 0;
        while (count = Midi.RecvData(buffer)) {
            Wire.beginTransmission(0);
            Wire.write(buffer, count);
            Wire.endTransmission();
        }
        */
}
