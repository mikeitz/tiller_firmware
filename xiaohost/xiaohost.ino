#include "Wire.h"

// https://github.com/gdsports/USB_Host_Library_SAMD
#include <usbh_midi.h>
#include <usbhub.h>

USBHost UsbH;
USBH_MIDI MIDIUSBH(&UsbH);

void SendMidi(uint8_t* data, uint8_t count) {
    Wire.beginTransmission(0);
    Wire.write(data, count);
    Wire.endTransmission();
}

void setup() {
    Wire.begin();
    if (UsbH.Init()) {
        while (true) {
          delay(500);
          Wire.beginTransmission(0);
          Wire.write(0xff);
          Wire.endTransmission();
        }
    }
}


int n = 0;

void loop() {
    delay(1);
    if (n++ == 3000) {
      n = 0;
      Wire.beginTransmission(0);
      Wire.write(0);
      Wire.endTransmission();
    }
       
    UsbH.Task();
    if (!MIDIUSBH) {
      return;
    }

    uint8_t recvBuf[256];
    uint8_t rcode = 0;     //return code
    uint16_t rcvd = 0;
    uint8_t readCount = 0;

    rcode = MIDIUSBH.RecvData(&rcvd, recvBuf);

    if (rcode != 0 || rcvd == 0) return;
    if (recvBuf[0] == 0 && recvBuf[1] == 0 && recvBuf[2] == 0 && recvBuf[3] == 0) {
        return;
    }

    uint8_t* p = recvBuf;
    while (readCount < rcvd) {
        if (*p == 0 && *(p + 1) == 0) break; //data end
        uint8_t header = *p & 0x0F;
        p++;
        switch (header) {
        case 0x00:  // Misc. Reserved for future extensions.
            break;
        case 0x01:  // Cable events. Reserved for future expansion.
            break;
        case 0x02:  // Two-byte System Common messages
        case 0x0C:  // Program Change
        case 0x0D:  // Channel Pressure
            SendMidi(p, 2);
            break;
        case 0x03:  // Three-byte System Common messages
        case 0x08:  // Note-off
        case 0x09:  // Note-on
        case 0x0A:  // Poly-KeyPress
        case 0x0B:  // Control Change
        case 0x0E:  // PitchBend Change
            SendMidi(p, 3);
            break;

        case 0x04:  // SysEx starts or continues
            SendMidi(p, 3);
            break;
        case 0x05:  // Single-byte System Common Message or SysEx ends with the following single byte
            SendMidi(p, 1);
            break;
        case 0x06:  // SysEx ends with the following two bytes
            SendMidi(p, 2);
            break;
        case 0x07:  // SysEx ends with the following three bytes
            SendMidi(p, 3);
            break;
        case 0x0F:  // Single Byte, TuneRequest, Clock, Start, Continue, Stop, etc.
            SendMidi(p, 1);
            break;
        }
        p += 3;
        readCount += 4;
    }
}
