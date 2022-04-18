#include "nrf.h"

void nfcAsGpio() {
  if ((NRF_UICR->NFCPINS & UICR_NFCPINS_PROTECT_Msk) == (UICR_NFCPINS_PROTECT_NFC << UICR_NFCPINS_PROTECT_Pos)){
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen << NVMC_CONFIG_WEN_Pos;
    while (NRF_NVMC->READY == NVMC_READY_READY_Busy){}
    NRF_UICR->NFCPINS &= ~UICR_NFCPINS_PROTECT_Msk;
    while (NRF_NVMC->READY == NVMC_READY_READY_Busy){}
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren << NVMC_CONFIG_WEN_Pos;
    while (NRF_NVMC->READY == NVMC_READY_READY_Busy){}
    NVIC_SystemReset();
  }
}

int pinNums[21] = {
  PIN_A0, PIN_A1, PIN_A2, PIN_A3, PIN_A4, PIN_A5,
  PIN_NFC2,
  PIN_SPI_MOSI, PIN_SPI_MISO, PIN_SPI_SCK, PIN_WIRE_SCL, PIN_WIRE_SDA,
  13, 12, 11, 10, 9, 6, 5,
  PIN_SERIAL1_RX, PIN_SERIAL1_TX
  
};
char* pinNames[21] = {
  "PIN_A0", "PIN_A1", "PIN_A2", "PIN_A3", "PIN_A4", "PIN_A5",
  "PIN_NFC2",
  "PIN_SPI_MOSI", "PIN_SPI_MISO", "PIN_SPI_SCK", "PIN_WIRE_SCL", "PIN_WIRE_SDA",
  "13", "12", "11", "10", "9", "6", "5",
  "PIN_SERIAL1_RX", "PIN_SERIAL1_TX"
};

void setup() {
  nfcAsGpio();
  for (int i = 0; i < 21; ++i) {
    pinMode(pinNums[i], INPUT_PULLUP);
  }
  Serial.begin(9600);
}

void loop() {
  for (int i = 0; i < 21; ++i) {
    if (digitalRead(pinNums[i]) == 0) {
      Serial.println(pinNames[i]); 
    }
  }
}
