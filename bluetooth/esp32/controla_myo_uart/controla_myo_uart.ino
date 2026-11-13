#include <myo.h>

/**********************************************************************
 * MYO EMG ARMBAND EXAMPLE - IT HAS BEEN EDITED BY TOMAS JONSSON TO BE CONNECTED TO AN ESP32 for the pololu UART
 * Author: Davide Asnaghi
 * Date: April 3rd 2019
 * Requirements:
 *    - ESP32 arduino (https://github.com/espressif/arduino-esp32.git)
 *    - ESP32 Myo library (https://github.com/project-sparthan/sparthan-myo.git)
 * Notes:
 * This sketch is intended to be used with the Arduino IDE serial
 * plotter, it produces visualization for the data coming from the MYO
 * armband. Flash the example sketch onto a board, then open the
 * "Serial Plotter" tool to see the data in real time
 **********************************************************************/
#define ESP32_TX_PIN 17
#define ESP32_RX_PIN 16

static uint16_t sequence = 0;
static volatile uint32_t count = 0;

armband myo; // Myo BLE Armband

void send_emg_sample(int8_t *sample, size_t len)
{
  char line[64];                                    
  int chars_sofar = snprintf(line, sizeof(line), "%u", (unsigned)sequence); 
  for (size_t i = 0; i < len; i++)
  {
    chars_sofar += snprintf(line + chars_sofar, sizeof(line) - chars_sofar, ",%d", (int)sample[i]);  
  }
  line[chars_sofar++] = '\n';                                 

  Serial1.write((const uint8_t *)line, chars_sofar);         

  sequence++;                                           
  count++;                                        
}

void emg_callback(BLERemoteCharacteristic *pBLERemoteCharacteristic, uint8_t *pData, size_t length, bool isNotify)
{
  if (length != sizeof(myohw_emg_data_t)) {
    return;
  }
 
  myohw_emg_data_t *emg_data = (myohw_emg_data_t *)pData;
  send_emg_sample(emg_data->sample1, myohw_num_emg_sensors);
  send_emg_sample(emg_data->sample2, myohw_num_emg_sensors);
}

void myo_connect()
{
  Serial.println("Connecting");
  myo.connect();
  if (!myo.connected) {
    Serial.println(" Connection failed, trying again");
    return;
  }
  Serial.println(" Connected");
  delay(100);

  myo.set_sleep_mode(1); //1 corresponds to never sleeping - found in the myo_bluetooth.h file
  myo.set_myo_mode(myohw_emg_mode_send_emg_raw,         // EMG mode
                   myohw_imu_mode_none,             // IMU mode
                   myohw_classifier_mode_disabled); // Classifier mode

  myo.emg_notification(TURN_ON)->registerForNotify(emg_callback);

  BLERemoteService *emg_service = myo.pClient->getService(BLEUUID("d5060005-a904-deb9-4748-2c7f4a124842"));
  if (emg_service == nullptr)
  {
    Serial.println(" - ERROR: EMG service not found");
    return;
  }
  
  const char *other_emg_chars[3] = {
    "d5060205-a904-deb9-4748-2c7f4a124842",   // EMG characteristic 1
    "d5060305-a904-deb9-4748-2c7f4a124842",   // EMG characteristic 2
    "d5060405-a904-deb9-4748-2c7f4a124842",   // EMG characteristic 3
  };

  for (int i = 0; i < 3; i++)
  {
    BLERemoteCharacteristic *c = emg_service->getCharacteristic(BLEUUID(other_emg_chars[i]));
    if (c == nullptr)
    {
      Serial.printf(" - ERROR: EMG characteristic %d not found\n", i + 1);
      continue;
    }
    c->registerForNotify(emg_callback);
  }

  // notifies if connection to the Myo works
  myo.vibration(1);
  Serial.println(" Streaming RAW EMG");
}

/********************************************************************************************************
    ARDUINO SKETCH
 ********************************************************************************************************/

void setup()
{
  // Start the serial interface
  Serial.begin(115200);
  Serial1.begin(230400, SERIAL_8N1, ESP32_RX_PIN, ESP32_TX_PIN); //230400 is the baud rate across the ESTP32 and Pololu
  myo.debug = true;
}

void loop()
{
  static uint32_t last_ms = 0, last_count = 0;
if (millis() - last_ms >= 1000) {
  uint32_t c = count;                       // snapshot; callback runs in the BLE task
  Serial.printf("rate=%u/s\n", c - last_count);
  last_count = c;
  last_ms = millis();
}
  // Detect disconnection
  if (!myo.connected)
  {
    myo_connect();
  }

  delay(10);
}
