#include <Wire.h>
#include "LoRa_APP.h"
#include "Arduino.h"
#include <LoRaWan_APP.h>

#ifndef LoraWan_RGB
#define LoraWan_RGB 0
#endif

#define RF_FREQUENCY 915000000  // Hz
#define TX_OUTPUT_POWER 14      // dBm

#define LORA_BANDWIDTH 0         // [0: 125 kHz]
#define LORA_SPREADING_FACTOR 7  // [SF7..SF12]
#define LORA_CODINGRATE 1        // [1: 4/5]
#define LORA_PREAMBLE_LENGTH 8
#define LORA_SYMBOL_TIMEOUT 0
#define LORA_FIX_LENGTH_PAYLOAD_ON false
#define LORA_IQ_INVERSION_ON false
#define RX_TIMEOUT_VALUE 1000

#define BUFFER_SIZE 33  // Define the payload size here

#define BATTERY_PIN ADC1    // ADC pin for the battery
#define MOVEMENT_PIN GPIO8  // Pin connected to the PIR sensor

#define LOW_BATTERY_THRESHOLD 3.01  // Threshold for low battery voltage

char txpacket[BUFFER_SIZE] = "WARNING!! Movement is recognized";
char rxpacket[BUFFER_SIZE];
char txbatterypacket[BUFFER_SIZE] = "Low Battery";

static RadioEvents_t RadioEvents;
bool lora_idle = true;

// Wakes us from sleep when motion occurs
volatile bool movement_triggered = false;

// ---- Forward declarations ----
void MovementISR();
void checkBatteryVoltage();
void sendLoRaMessage();
void sendLowBatteryMessage();
void enterDeepSleep();
void OnTxDone(void);
void OnTxTimeout(void);

void setup() {
  Serial.begin(115200);
  while (!Serial)
    ;  // Wait for serial port to connect (USB)

  // --- LoRa radio init (P2P) ---
  RadioEvents.TxDone    = OnTxDone;
  RadioEvents.TxTimeout = OnTxTimeout;

  Radio.Init(&RadioEvents);
  Radio.SetChannel(RF_FREQUENCY);
  Radio.SetTxConfig(MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH,
                    LORA_SPREADING_FACTOR, LORA_CODINGRATE,
                    LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                    true, 0, 0, LORA_IQ_INVERSION_ON, 3000);
  Radio.Sleep();  // Start with radio in low-power

  // --- GPIO / ADC setup ---
  pinMode(MOVEMENT_PIN, INPUT);
  pinMode(BATTERY_PIN, INPUT);   // ADC input
  attachInterrupt(digitalPinToInterrupt(MOVEMENT_PIN), MovementISR, RISING);

  Serial.println("Initializing LoRa....");
  Serial.println("LoRa initialized.");

  // Go directly to low power. Device will wake on:
  // - GPIO8 interrupt (motion)
  // - Radio IRQ (TX done / timeout)
  enterDeepSleep();
}

void loop() {
  // Handle any pending radio IRQs (TX done, timeout, RX…)
  Radio.IrqProcess();

  // If motion woke us
  if (movement_triggered) {
    movement_triggered = false;

    // Optional: confirm pin is still high (simple debounce)
    int pinState = digitalRead(MOVEMENT_PIN);
    Serial.print("Movement detected, GPIO8 state = ");
    Serial.println(pinState);

    // Check battery once per motion event (optional)
    checkBatteryVoltage();

    // Send LoRa message about movement
    sendLoRaMessage();
  }

  // Nothing else to do? Go back to low power
  enterDeepSleep();
}

// ---- Interrupt: motion on GPIO8 ----
void MovementISR() {
  movement_triggered = true;
}

// ---- Battery check ----
void checkBatteryVoltage() {
  float raw = analogRead(BATTERY_PIN);
  float batteryVoltage = raw * (3.3 / 4095.0);  // adjust with divider if needed

  Serial.print("Battery Voltage (ADC pin): ");
  Serial.println(batteryVoltage);

  if (batteryVoltage < LOW_BATTERY_THRESHOLD) {
    Serial.println("Battery is low!");
    sendLowBatteryMessage();
  }
}

// ---- Send main motion message ----
void sendLoRaMessage() {
  Serial.println("Sending motion packet...");
  delay(200);  // small delay for debug prints
  Serial.printf("sending packet \"%s\"\n", txpacket);
  lora_idle = false;
  Radio.Send((uint8_t *)txpacket, strlen(txpacket));  // send the packet
}

// ---- Send low battery message ----
void sendLowBatteryMessage() {
  Serial.println("Sending low battery message...");
  delay(200);
  Serial.printf("sending packet \"%s\"\n", txbatterypacket);
  lora_idle = false;
  Radio.Send((uint8_t *)txbatterypacket, strlen(txbatterypacket));
}

// ---- Low power handler wrapper ----
void enterDeepSleep() {
  Serial.println("Entering low power...");
  delay(50);          // Give Serial a moment
  lowPowerHandler();  // CubeCell's low-power manager
}

// ---- Radio callbacks ----
void OnTxDone(void) {
  turnOffRGB();
  Serial.println("TX Done");
  lora_idle = true;
  Radio.Sleep();  // IMPORTANT: radio back to sleep after TX
}

void OnTxTimeout(void) {
  turnOffRGB();
  Radio.Sleep();
  Serial.println("TX Timeout");
  lora_idle = true;
}