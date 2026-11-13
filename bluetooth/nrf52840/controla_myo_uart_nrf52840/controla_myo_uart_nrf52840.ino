/**********************************************************************
 * controla_myo_uart_nrf52840.ino
 *
 * Myo armband -> BLE -> SparkFun Pro nRF52840 Mini -> UART -> Pololu 3pi+ 2040
 *
 * Port of controla_myo_uart.ino (ESP32 + sparthan-myo) to the nRF52840,
 * using the Adafruit nRF52 core's Bluefruit library instead of sparthan-myo
 * (sparthan-myo only builds on ESP32). The Myo commands and the UART output
 * are the same as the ESP32 version:
 *   - Myo: never sleep, raw EMG on, IMU off, classifier off, short vibrate
 *   - UART: one line per EMG sample, "seq,e0,e1,e2,e3,e4,e5,e6,e7\n",
 *           230400 baud 8N1
 *
 * Board:  Tools > Board > SparkFun Pro nRF52840 Mini
 *         (Adafruit nRF52 core + SparkFun board definition)
 *
 * Wiring (both boards are 3.3 V logic):
 *   Mini pin 17 (Serial1 TX) -> 3pi+ UART RX
 *   Mini pin 15 (Serial1 RX) <- 3pi+ UART TX   (only if the 3pi+ talks back)
 *   Mini GND                 -- 3pi+ GND        (required)
 *
 * USB "Serial" is debug output only (any baud in the Serial Monitor).
 **********************************************************************/
#include <bluefruit.h>

/* ---------------- Myo BLE protocol (values from myohw.h) ----------------
 * Myo UUIDs are d506XXXX-a904-deb9-4748-2c7f4a124842. Bluefruit takes
 * 128-bit UUIDs least-significant byte first, and keeps a pointer to the
 * array, so these must stay global/static. */
#define MYO_UUID(lo, hi) { 0x42, 0x48, 0x12, 0x4a, 0x7f, 0x2c, 0x48, 0x47, \
                           0xb9, 0xde, 0x04, 0xa9, (lo), (hi), 0x06, 0xd5 }

static const uint8_t UUID_CONTROL_SVC[16] = MYO_UUID(0x01, 0x00); // d5060001 control service
static const uint8_t UUID_COMMAND_CHR[16] = MYO_UUID(0x01, 0x04); // d5060401 command (write)
static const uint8_t UUID_EMG_SVC[16]     = MYO_UUID(0x05, 0x00); // d5060005 EMG service
static const uint8_t UUID_EMG0_CHR[16]    = MYO_UUID(0x05, 0x01); // d5060105 EMG data 0 (notify)
static const uint8_t UUID_EMG1_CHR[16]    = MYO_UUID(0x05, 0x02); // d5060205 EMG data 1
static const uint8_t UUID_EMG2_CHR[16]    = MYO_UUID(0x05, 0x03); // d5060305 EMG data 2
static const uint8_t UUID_EMG3_CHR[16]    = MYO_UUID(0x05, 0x04); // d5060405 EMG data 3

// Command bytes: {command, payload_size, payload...}, same as sparthan-myo
static const uint8_t CMD_NEVER_SLEEP[]   = { 0x09, 0x01, 0x01 };             // set_sleep_mode(never_sleep)
static const uint8_t CMD_EMG_RAW_ONLY[]  = { 0x01, 0x03, 0x03, 0x00, 0x00 }; // set_mode(emg raw, imu none, classifier off)
static const uint8_t CMD_VIBRATE_SHORT[] = { 0x03, 0x01, 0x01 };             // vibrate(short)

#define MYO_NUM_EMG_SENSORS   8
#define MYO_EMG_PACKET_LEN    16   // two samples of 8 x int8 per notification

/* ---------------- UART to the 3pi+ ---------------- */
#define POLOLU_BAUD 230400         // Serial1: TX = pin 17, RX = pin 15 (board variant)

/* ---------------- BLE client objects ---------------- */
BLEClientService        myoCtrlSvc(UUID_CONTROL_SVC);
BLEClientCharacteristic myoCmdChr(UUID_COMMAND_CHR);

BLEClientService        myoEmgSvc(UUID_EMG_SVC);
BLEClientCharacteristic myoEmg0(UUID_EMG0_CHR);
BLEClientCharacteristic myoEmg1(UUID_EMG1_CHR);
BLEClientCharacteristic myoEmg2(UUID_EMG2_CHR);
BLEClientCharacteristic myoEmg3(UUID_EMG3_CHR);
BLEClientCharacteristic* const emgChrs[4] = { &myoEmg0, &myoEmg1, &myoEmg2, &myoEmg3 };

static uint16_t sequence = 0;
static volatile uint32_t count = 0;       // EMG samples sent to the 3pi+
static volatile bool streaming = false;   // true once EMG notifications are on
static uint32_t last_scan_kick_ms = 0;

/* ---------------- EMG -> UART (same format as the ESP32 version) ---------------- */
void send_emg_sample(const int8_t *sample, size_t len)
{
  char line[64];
  int chars_sofar = snprintf(line, sizeof(line), "%u", (unsigned)sequence);
  for (size_t i = 0; i < len; i++)
  {
    chars_sofar += snprintf(line + chars_sofar, sizeof(line) - chars_sofar, ",%d", (int)sample[i]);
  }
  line[chars_sofar++] = '\n';            // max line is 46 chars, fits in 64

  Serial1.write((const uint8_t *)line, chars_sofar);

  sequence++;
  count++;
}

void emg_notify_cb(BLEClientCharacteristic *chr, uint8_t *data, uint16_t len)
{
  (void)chr;
  if (len != MYO_EMG_PACKET_LEN) {
    return;
  }
  send_emg_sample((const int8_t *)data, MYO_NUM_EMG_SENSORS);
  send_emg_sample((const int8_t *)data + MYO_NUM_EMG_SENSORS, MYO_NUM_EMG_SENSORS);
}

/* ---------------- Myo helpers ---------------- */
static bool myo_command(const uint8_t *cmd, uint16_t len, const char *what)
{
  // write-without-response, the same write type sparthan-myo uses on ESP32
  uint16_t n = myoCmdChr.write(cmd, len);
  Serial.printf("  %-22s %s\n", what, (n == len) ? "sent" : "FAILED");
  return n == len;
}

static void print_addr(const uint8_t addr[6])
{
  for (int i = 5; i >= 0; i--) {
    Serial.printf("%02X%s", addr[i], i ? ":" : "");
  }
}

/* ---------------- BLE callbacks ---------------- */
void scan_cb(ble_gap_evt_adv_report_t *report)
{
  // Scanner.filterUuid() only passes devices advertising the Myo control service
  Serial.print("Myo found: ");
  print_addr(report->peer_addr.addr);
  Serial.printf("  RSSI %d dBm, connecting...\n", report->rssi);
  last_scan_kick_ms = millis();
  Bluefruit.Central.connect(report);
}

void connect_cb(uint16_t conn_handle)
{
  Serial.println("Connected. Discovering Myo services...");

  if (!myoCtrlSvc.discover(conn_handle) || !myoCmdChr.discover()) {
    Serial.println(" - ERROR: control service / command characteristic not found");
    Bluefruit.disconnect(conn_handle);
    return;
  }
  if (!myoEmgSvc.discover(conn_handle)) {
    Serial.println(" - ERROR: EMG service not found");
    Bluefruit.disconnect(conn_handle);
    return;
  }
  int found = 0;
  for (int i = 0; i < 4; i++) {
    if (emgChrs[i]->discover()) {
      found++;
    } else {
      Serial.printf(" - ERROR: EMG characteristic %d not found\n", i);
    }
  }
  if (found == 0) {
    Bluefruit.disconnect(conn_handle);
    return;
  }

  delay(100);   // same settle time as the ESP32 version

  Serial.println("Configuring Myo:");
  myo_command(CMD_NEVER_SLEEP, sizeof(CMD_NEVER_SLEEP), "sleep mode = never");
  myo_command(CMD_EMG_RAW_ONLY, sizeof(CMD_EMG_RAW_ONLY), "mode = raw EMG only");

  int enabled = 0;
  for (int i = 0; i < 4; i++) {
    if (emgChrs[i]->discovered() && emgChrs[i]->enableNotify()) {
      enabled++;
    } else {
      Serial.printf(" - ERROR: could not enable notify on EMG characteristic %d\n", i);
    }
  }
  Serial.printf("  EMG notifications on: %d/4\n", enabled);

  myo_command(CMD_VIBRATE_SHORT, sizeof(CMD_VIBRATE_SHORT), "vibrate (short)");

  streaming = (enabled > 0);
  Serial.println(streaming ? " Streaming RAW EMG" : " - ERROR: no EMG streams enabled");
  if (!streaming) {
    Bluefruit.disconnect(conn_handle);
  }
}

void disconnect_cb(uint16_t conn_handle, uint8_t reason)
{
  (void)conn_handle;
  streaming = false;
  last_scan_kick_ms = millis();
  Serial.printf("Disconnected (reason 0x%02X). Scanning for Myo again...\n", reason);
  // Scanner.restartOnDisconnect(true) restarts the scan automatically
}

/********************************************************************************************************
    ARDUINO SKETCH
 ********************************************************************************************************/

void setup()
{
  Serial.begin(115200);          // USB CDC debug; does not wait for a PC to be attached
  Serial1.begin(POLOLU_BAUD);    // to the 3pi+ (TX pin 17, RX pin 15)

  // One central link, with large buffers so 100 EMG notifications/s keep up
  Bluefruit.configCentralBandwidth(BANDWIDTH_MAX);
  Bluefruit.begin(0, 1);         // 0 peripheral links, 1 central link
  Bluefruit.setName("Myo-nRF52840");

  // Client characteristics attach to the service begin()'d just before them
  myoCtrlSvc.begin();
  myoCmdChr.begin();

  myoEmgSvc.begin();
  for (int i = 0; i < 4; i++) {
    emgChrs[i]->setNotifyCallback(emg_notify_cb);
    emgChrs[i]->begin();
  }

  Bluefruit.Central.setConnInterval(6, 12);   // 7.5-15 ms (units of 1.25 ms)
  Bluefruit.Central.setConnectCallback(connect_cb);
  Bluefruit.Central.setDisconnectCallback(disconnect_cb);

  Bluefruit.Scanner.setRxCallback(scan_cb);
  Bluefruit.Scanner.restartOnDisconnect(true);
  Bluefruit.Scanner.filterUuid(myoCtrlSvc.uuid);
  Bluefruit.Scanner.useActiveScan(true);       // sparthan-myo also scans actively
  Bluefruit.Scanner.setInterval(160, 80);      // 100 ms interval, 50 ms window
  Bluefruit.Scanner.start(0);                  // scan until a Myo is found
  last_scan_kick_ms = millis();

  Serial.println("Scanning for Myo...");
}

void loop()
{
  static uint32_t last_ms = 0, last_count = 0;
  if (millis() - last_ms >= 1000) {
    uint32_t c = count;                        // snapshot; callback runs in the BLE task
    Serial.printf("rate=%lu/s%s\n", (unsigned long)(c - last_count), streaming ? "" : " (not streaming)");
    last_count = c;
    last_ms = millis();
  }

  // If a connection attempt timed out, nothing restarts the scanner; kick it.
  if (!Bluefruit.Central.connected() && !Bluefruit.Scanner.isRunning()
      && millis() - last_scan_kick_ms > 5000) {
    Serial.println("Scanner idle, restarting scan...");
    Bluefruit.Scanner.start(0);
    last_scan_kick_ms = millis();
  }

  delay(10);
}
