#include "nrf.h"
#include "nrf_gzll.h"

#define num_cols 7
#define num_rows 8

const uint8_t rows[num_rows] = {5, 4, 3, 2, 25, 26, 27, 30};
const uint8_t cols[num_cols] = {29, 16, 15, 7, 11, 13, 14};

uint8_t matrix[num_rows + 1];
uint8_t debounceMatrix[num_rows];
uint8_t lastMatrix[num_rows];
int scanTimeMs = 1;
int debounceTicks = ms2tick(1);
int debounceCount = 2;

int lastActivity = 0;
int sleepTimeMs = 2000;
int powerOffSeconds = 600;
volatile int sleeping = 0;
volatile int wakeUp = 0;
TimerHandle_t powerOffTimer;

int maxTransmissionGap = 1000;
int lastTransmission = maxTransmissionGap + 1;
const uint8_t CHECK_BYTE = 0x55;

static uint8_t ack_payload[NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH];
static uint8_t data_buffer[NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH];
extern nrf_gzll_error_code_t nrf_gzll_error_code;
static uint8_t channel_table[3] = {4, 42, 77};

///////////////////////////////////////////////////// MATRIX

inline void clearMatrix(uint8_t *m) {
  memset(m, 0, num_rows);
}

inline void copyMatrixTo(uint8_t *other) {
  memcpy(other, matrix, num_rows);
}

inline bool different(uint8_t *a, uint8_t *b) {
  return memcmp(a, b, num_rows);
}

inline void setKey(uint8_t r, uint8_t c, bool set) {
  matrix[r] = matrix[r] | (set << c);
}

inline bool getKey(uint8_t r, uint8_t c) {
  return (matrix[r] & (1 << c)) != 0;
}

bool anyKeyDown() {
  for (int r = 0; r < num_rows; ++r) {
    if (matrix[r]) {
      return true;
    }
  }
  return false;
}

void printMatrix() {
  if (Serial) {
    return;
  }
  Serial.println("");
  for (uint8_t r = 0; r < num_rows; r++) {
    for (uint8_t c = 0; c < num_cols; c++) {
      Serial.print(getKey(r, c));
    }
    Serial.println("");
  }
  Serial.println("");
}

void scanMatrix() {
  clearMatrix(matrix);
  for (uint8_t c = 0; c < num_cols; ++c) {
    digitalWrite(cols[c], HIGH);
    for (uint8_t r = 0; r < num_rows; ++r) {
      bool bit = digitalRead(rows[r]);
      setKey(r, c, bit ? 1 : 0);
    }
    digitalWrite(cols[c], LOW);
  }
}

bool scanWithDebounce() {
  scanMatrix();
  if (!different(matrix, lastMatrix)) {
    return false;
  }
  copyMatrixTo(debounceMatrix);
  uint8_t debounces = 0;
  while (debounces < debounceCount) {
    vTaskDelay(debounceTicks);
    scanMatrix();
    if (different(matrix, debounceMatrix)) {
      debounces = 0;
      copyMatrixTo(debounceMatrix);
    } else {
      debounces++;
    }
  }
  bool result = different(matrix, lastMatrix);
  copyMatrixTo(lastMatrix);
  return result;
}

void initMatrix() {
  for (uint8_t c = 0; c < num_cols; ++c) {
    pinMode(cols[c], OUTPUT);
    digitalWrite(cols[c], LOW);
  }
  for (uint8_t r = 0; r < num_rows; ++r) {
    pinMode(rows[r], INPUT_PULLDOWN);
  }
}

///////////////////////////////////////////////////// RADIO

void transmit() {
  matrix[num_rows] = CHECK_BYTE;
  nrf_gzll_add_packet_to_tx_fifo(0, matrix, num_rows + 1);
  // Serial.print(".");
}

void nrf_gzll_device_tx_failed(uint32_t pipe, nrf_gzll_device_tx_info_t tx_info) {}
void nrf_gzll_host_rx_data_ready(uint32_t pipe, nrf_gzll_host_rx_info_t rx_info) {}
void nrf_gzll_disabled() {}
void  nrf_gzll_device_tx_success(uint32_t pipe, nrf_gzll_device_tx_info_t tx_info)
{
  uint32_t ack_payload_length = NRF_GZLL_CONST_MAX_PAYLOAD_LENGTH;
  if (tx_info.payload_received_in_ack)
  {
    nrf_gzll_fetch_packet_from_rx_fifo(pipe, ack_payload, &ack_payload_length);
  }
}

void initRadio() {
  nrf_gzll_init(NRF_GZLL_MODE_DEVICE);
  nrf_gzll_set_max_tx_attempts(100);
  nrf_gzll_set_timeslots_per_channel(4);
  nrf_gzll_set_channel_table(channel_table, 3);
  nrf_gzll_set_datarate(NRF_GZLL_DATARATE_1MBIT);
  nrf_gzll_set_timeslot_period(900);
  nrf_gzll_set_base_address_0(0x01020304);
  nrf_gzll_set_base_address_1(0x05060708);
  nrf_gzll_enable();
}

//////////////////////////////////////////////////// BATTERY

// From:
// https://learn.adafruit.com/bluefruit-nrf52-feather-learning-guide/nrf52-adc

#define VBAT_MV_PER_LSB   (0.73242188F)   // 3.0V ADC range and 12-bit ADC resolution = 3000mV/4096
#ifdef NRF52840_XXAA
#define VBAT_DIVIDER      (0.5F)               // 150K + 150K voltage divider on VBAT
#define VBAT_DIVIDER_COMP (2.0F)          // Compensation factor for the VBAT divider
#else
#define VBAT_DIVIDER      (0.71275837F)   // 2M + 0.806M voltage divider on VBAT = (2M / (0.806M + 2M))
#define VBAT_DIVIDER_COMP (1.403F)        // Compensation factor for the VBAT divider
#endif
#define REAL_VBAT_MV_PER_LSB (VBAT_DIVIDER_COMP * VBAT_MV_PER_LSB)

float readVBAT(void) {
  float raw;
  // Set the analog reference to 3.0V (default = 3.6V)
  analogReference(AR_INTERNAL_3_0);
  // Set the resolution to 12-bit (0..4095)
  analogReadResolution(12); // Can be 8, 10, 12 or 14
  // Let the ADC settle
  delay(1);
  // Get the raw 12-bit, 0..3000mV ADC value
  raw = analogRead(PIN_VBAT);
  // Set the ADC back to the default settings
  analogReference(AR_DEFAULT);
  analogReadResolution(10);
  // Convert the raw value to compensated mv, taking the resistor-
  // divider into account (providing the actual LIPO voltage)
  // ADC range is 0..3000mV and resolution is 12-bit (0..4095)
  return raw * REAL_VBAT_MV_PER_LSB;
}
 
uint8_t mvToPercent(float mvolts) {
  if(mvolts<3300)
    return 0;
  if(mvolts <3600) {
    mvolts -= 3300;
    return mvolts/30;
  }
  mvolts -= 3600;
  return 10 + (mvolts * 0.15F );  // thats mvolts /6.66666666
}

//////////////////////////////////////////////////// POWER

void interruptHandler() {
  NRF_GPIOTE->EVENTS_PORT = 0;
  wake();
}

inline void pinModeDetect(uint32_t pin) {
  pin = g_ADigitalPinMap[pin];
  NRF_GPIO_Type * port = nrf_gpio_pin_port_decode(&pin);
  port->PIN_CNF[pin] = ((uint32_t)GPIO_PIN_CNF_DIR_Input << GPIO_PIN_CNF_DIR_Pos)
                       | ((uint32_t)GPIO_PIN_CNF_INPUT_Connect << GPIO_PIN_CNF_INPUT_Pos)
                       | ((uint32_t)GPIO_PIN_CNF_PULL_Pulldown << GPIO_PIN_CNF_PULL_Pos)
                       | ((uint32_t)GPIO_PIN_CNF_DRIVE_S0S1 << GPIO_PIN_CNF_DRIVE_Pos)
                       | ((uint32_t)GPIO_PIN_CNF_SENSE_High << GPIO_PIN_CNF_SENSE_Pos);
}

void powerOffHandler(TimerHandle_t handle) {
  sleep(true);
}

void initPower() {
  NVIC_DisableIRQ(GPIOTE_IRQn);
  NVIC_ClearPendingIRQ(GPIOTE_IRQn);
  NVIC_SetPriority(GPIOTE_IRQn, 3);
  NVIC_EnableIRQ(GPIOTE_IRQn);
  attachCustomInterruptHandler(interruptHandler);
  powerOffTimer = xTimerCreate(NULL, ms2tick(powerOffSeconds * 1000), false, NULL, powerOffHandler);
  xTimerStart(powerOffTimer, 0);
}

#define debug(x) if (Serial) { Serial.println(x); }

void sleep(bool deep) {
  debug(deep ? "poweroff" : "sleep");
  if (sleeping && !deep) {
    return;
  }
  NRF_GPIOTE->EVENTS_PORT = 0;
  NRF_GPIOTE->INTENSET |= GPIOTE_INTENSET_PORT_Msk;
  for (uint8_t r = 0; r < num_rows; ++r) {
    pinModeDetect(rows[r]);
  }
  for (uint8_t c = 0; c < num_cols; ++c) {
    pinMode(cols[c], OUTPUT);
    digitalWrite(cols[c], HIGH);
  }
  sleeping = 1;
  if (deep) {
    NRF_POWER->SYSTEMOFF = 1;
  } else {
    suspendLoop();
  }
}

void wake() {
  if (!sleeping) {
    return;
  }
  sleeping = 0;
  wakeUp = 1;
  NRF_GPIOTE->EVENTS_PORT = 0;
  NRF_GPIOTE->INTENCLR |= GPIOTE_INTENSET_PORT_Msk;
  initMatrix();
  xTimerReset(powerOffTimer, 0);
  resumeLoop();
}

//////////////////////////////////////////////////// MAIN

void setup() {
  Serial.begin(115200);
  if (Serial) {
    debug("online");
    float vbat_mv = readVBAT();
    uint8_t vbat_per = mvToPercent(vbat_mv);
    debug("battery level:");
    debug(vbat_mv);
    debug(vbat_per);
  }
  initMatrix();
  initRadio();
  initPower();
  sleep(false);
}

void loop() {
  if (wakeUp) {
    debug("wake");
    wakeUp = 0;
    lastActivity = 0;
  }
  if (scanWithDebounce() || lastTransmission > maxTransmissionGap) {
    transmit();
    //printMatrix();
    lastTransmission = 0;
  } else {
    lastTransmission += scanTimeMs;
  }
  if (anyKeyDown()) {
    lastActivity = 0;
    xTimerReset(powerOffTimer, 0);
  } else {
    lastActivity += scanTimeMs;
  }
  if (lastActivity > sleepTimeMs) {
    sleep(false);
  } else {
    delay(scanTimeMs);
  }
}
