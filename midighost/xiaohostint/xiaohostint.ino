// https://github.com/mikeitz/USB_Host_Library_SAMD (based on fork with interrupts)
#include <usbh_midi.h>
#include <usbhub.h>
#include <Wire.h>

#define Is_uhd_in_received0(p)                    ((USB->HOST.HostPipe[p].PINTFLAG.reg&USB_HOST_PINTFLAG_TRCPT0) == USB_HOST_PINTFLAG_TRCPT0)
#define Is_uhd_in_received1(p)                    ((USB->HOST.HostPipe[p].PINTFLAG.reg&USB_HOST_PINTFLAG_TRCPT1) == USB_HOST_PINTFLAG_TRCPT1)
#define uhd_ack_in_received0(p)                   USB->HOST.HostPipe[p].PINTFLAG.reg = USB_HOST_PINTFLAG_TRCPT0
#define uhd_ack_in_received1(p)                   USB->HOST.HostPipe[p].PINTFLAG.reg = USB_HOST_PINTFLAG_TRCPT1
#define uhd_byte_count0(p)                        usb_pipe_table[p].HostDescBank[0].PCKSIZE.bit.BYTE_COUNT
#define uhd_byte_count1(p)                        usb_pipe_table[p].HostDescBank[1].PCKSIZE.bit.BYTE_COUNT
#define Is_uhd_in_ready0(p)                       ((USB->HOST.HostPipe[p].PSTATUS.reg&USB_HOST_PSTATUS_BK0RDY) == USB_HOST_PSTATUS_BK0RDY)  
#define uhd_ack_in_ready0(p)                       USB->HOST.HostPipe[p].PSTATUSCLR.reg = USB_HOST_PSTATUSCLR_BK0RDY
#define Is_uhd_in_ready1(p)                       ((USB->HOST.HostPipe[p].PSTATUS.reg&USB_HOST_PSTATUS_BK1RDY) == USB_HOST_PSTATUS_BK1RDY)  
#define uhd_ack_in_ready1(p)                       USB->HOST.HostPipe[p].PSTATUSCLR.reg = USB_HOST_PSTATUSCLR_BK1RDY
#define uhd_current_bank(p)                       ((USB->HOST.HostPipe[p].PSTATUS.reg&USB_HOST_PSTATUS_CURBK) == USB_HOST_PSTATUS_CURBK)  
#define Is_uhd_toggle(p)                          ((USB->HOST.HostPipe[p].PSTATUS.reg&USB_HOST_PSTATUS_DTGL) == USB_HOST_PSTATUS_DTGL)  
#define Is_uhd_toggle_error0(p)                   usb_pipe_table[p].HostDescBank[0].STATUS_PIPE.bit.DTGLER
#define Is_uhd_toggle_error1(p)                   usb_pipe_table[p].HostDescBank[1].STATUS_PIPE.bit.DTGLER

USBHost UsbH;
USBHub Hub(&UsbH);
USBH_MIDI  Midi(&UsbH);

bool doPipeConfig = false;
bool usbConnected = false;

//SAMD21 datasheet pg 836. ADDR location needs to be aligned. 
uint8_t bufBk0[64] __attribute__((aligned(4))); //Bank0
uint8_t bufBk1[64] __attribute__((aligned(4))); //Bank1

void WirePrint(String c) {
    Wire.beginTransmission(0);
    Wire.write(0);
    Wire.write(c.c_str());
    Wire.endTransmission();
};

void WirePrintln(String c) {
    Wire.beginTransmission(0);
    Wire.write(0);
    Wire.write(c.c_str());
    Wire.write("\n");
    Wire.endTransmission();
};

void setup() {
    Wire.begin();
    if (UsbH.Init()) {
        WirePrintln("FAILURE");
        while (1); //halt
    }
    USB_SetHandler(&CUSTOM_UHD_Handler);
    delay(200);
}

void loop() {
    //Note that Task() polls a hub if present, and we want to avoid polling.
    //So these conditions carry out enumeration only, and then stop running.
    //The idea is that except for enumeration (and release) this loop should 
    //be quiescent. 
    if (doPipeConfig || (!usbConnected && (UsbH.getUsbTaskState() != USB_DETACHED_SUBSTATE_WAIT_FOR_DEVICE))) {
        UsbH.Task();
    } else if (usbConnected && (UsbH.getUsbTaskState() != USB_STATE_RUNNING)) {
        UsbH.Task();
    }

    if (usbConnected && (UsbH.getUsbTaskState() == USB_STATE_RUNNING)) {
        if (Midi && (Midi.GetAddress() != Hub.GetAddress()) && (Midi.GetAddress() != 0)) {
            if (doPipeConfig) {
                //There is a chance that a disconnect interrupt may happen in the middle of this
                //and result in instability. Various tests here on usbConnected to hopefully
                //reduce the chance of it.
                uint32_t epAddr = Midi.GetEpAddress();
                doPipeConfig = false;
                uint16_t rcvd;
                while (usbConnected && (USB->HOST.HostPipe[Midi.GetEpAddress()].PCFG.bit.PTYPE != 0x03)) {
                    UsbH.Task();
                    Midi.RecvData(&rcvd, bufBk0);
                }
                USB->HOST.HostPipe[epAddr].BINTERVAL.reg = 0x01;//Zero here caused bus resets.
                usb_pipe_table[epAddr].HostDescBank[0].ADDR.reg = (uint32_t)bufBk0;
                usb_pipe_table[epAddr].HostDescBank[1].ADDR.reg = (uint32_t)bufBk1;
                USB->HOST.HostPipe[epAddr].PCFG.bit.PTOKEN = tokIN;
                USB->HOST.HostPipe[epAddr].PSTATUSCLR.reg = USB_HOST_PSTATUSCLR_BK0RDY;
                uhd_unfreeze_pipe(epAddr); //launch the transfer
                USB->HOST.HostPipe[epAddr].PINTENSET.reg = 0x3; //Enable pipe interrupts
            }
        }
    } else {
        USB_SetHandler(&CUSTOM_UHD_Handler);
        USB->HOST.HostPipe[Midi.GetEpAddress()].PINTENCLR.reg = 0xFF; //Disable pipe interrupts
    }
}


void CUSTOM_UHD_Handler(void) {
    uint32_t epAddr = Midi.GetEpAddress();
    if (USB->HOST.INTFLAG.reg == USB_HOST_INTFLAG_DCONN) {
        WirePrintln("CONNECT");
        doPipeConfig = true;
        usbConnected = true;
    } else if (USB->HOST.INTFLAG.reg == USB_HOST_INTFLAG_DDISC) {
        WirePrintln("DISCONNECT");
        usbConnected = false;
        USB->HOST.HostPipe[epAddr].PINTENCLR.reg = 0xFF; //Disable pipe interrupts
    }
    UHD_Handler();
    uhd_freeze_pipe(epAddr);

    //Both banks full and bank1 is oldest, so process first. 
    if (Is_uhd_in_received0(epAddr) && Is_uhd_in_received1(epAddr) && uhd_current_bank(epAddr)) {
        handleBank1(epAddr);
    }
    if (Is_uhd_in_received0(epAddr)) {
        handleBank0(epAddr);
    }
    if (Is_uhd_in_received1(epAddr)) {
        handleBank1(epAddr);
    }
    uhd_unfreeze_pipe(epAddr);
}

void handleBank0(uint32_t epAddr) {
    int rcvd = uhd_byte_count0(epAddr);
    uint8_t data[4];
    for (int i = 0; i < rcvd; i++) {
        //for regular MIDI searching for nonzero in the data and then
        //reading in chunks of four bytes seems to work well. 
        //Sysex would require a different strategy though.
        if (bufBk0[i] > 0) {
            data[0] = bufBk0[i++];
            data[1] = bufBk0[i++];
            data[2] = bufBk0[i++];
            data[3] = bufBk0[i];
            Wire.beginTransmission(0);
            Wire.write(data, 4);
            Wire.endTransmission();
        }
    }
    uhd_ack_in_received0(epAddr);
    uhd_ack_in_ready0(epAddr);
}

void handleBank1(uint32_t epAddr) {
    int rcvd = uhd_byte_count1(epAddr);
    uint8_t data[4];
    for (int i = 0; i < rcvd; i++) {
        if (bufBk1[i] > 0) {
            data[0] = bufBk1[i++];
            data[1] = bufBk1[i++];
            data[2] = bufBk1[i++];
            data[3] = bufBk1[i];
            Wire.beginTransmission(0);
            Wire.write(data, 4);
            Wire.endTransmission();
        }
    }
    uhd_ack_in_received1(epAddr);
    uhd_ack_in_ready1(epAddr);
}
