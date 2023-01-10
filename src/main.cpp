#include <Arduino.h>
#include <time.h>

#include <WiFi.h>
#include <WebSerialLite.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <AsyncElegantOTA.h>

#include <MQ135.h>

#include <Firebase.h>
#include <FirebaseJson.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>

#define LED_BUILTIN 33
#define GAS_SENSOR 15
#define OTA_MODE_EN 13

const char *host = "esp32";
const char *ssid = "HUAWEI-V4XU";
const char *password = "Tataycruz";

const char *firebaseApiKey = "AIzaSyC9ptugG-7U8EcTXQk-5nCje3OvAvrXHMw";
const char *firebaseDbUrl = "https://airquality-6d70f-default-rtdb.asia-southeast1.firebasedatabase.app/";

FirebaseData fb_dataObject;
FirebaseAuth fb_auth;
FirebaseConfig fb_config;
bool signupOk = false;

AsyncWebServer server2(80); // HTTP server descriptor assigned to port 80, used by the OTA and webserial server

int otaFlashMode;

struct sensorValue_t {
  int rawAnalogRead;
  float ppmReading;
  tm readAt;
};

MQ135 sensor(GAS_SENSOR);
float r0Calibration;
// int sensorValues[3];
sensorValue_t sensorValues[3];

void readGasSensor() {
  digitalWrite(LED_BUILTIN, 1);
  // The ADC of the GPIO pins are shared with the ADC used by the
  // WiFi antenna. To use analogRead, we first need to disconnect
  // WiFi and then reconnect after
  WiFi.disconnect(true, false);

  for (int i = 0; i < 3; ++i) {
    sensorValues[i].rawAnalogRead = analogRead(GAS_SENSOR);
    sensorValues[i].ppmReading = sensor.getPPM();
    getLocalTime(&sensorValues[i].readAt);
    delay(1000);
  }

  WiFi.begin(ssid, password);
  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(LED_BUILTIN, 0);
    delay(250);
    digitalWrite(LED_BUILTIN, 1);
    delay(250);
  }
  digitalWrite(LED_BUILTIN, 0);
}

void receiveWebSerial(uint8_t* data, size_t len) {
  String d = "";
  for(int i = 0; i < len; i++){
    d += char(data[i]);
  }

  WebSerial.println(d);

  if (d.equals("reboot")) {
    WebSerial.print("Device restarting ...");
    WebSerial.print("Refresh this page if it does not automatically reconnect");

    digitalWrite(LED_BUILTIN, HIGH);
    delay(1000);
    esp_restart();
  }

  if (d.equals("mode")) {
    // Implement Serial command that shows current OTA mode for debugging purposes
    if (otaFlashMode == 1) {
      WebSerial.print("MODE: OTA flash mode");
    } else {
      WebSerial.print("MODE: Run mode");
    }
    WebSerial.println();
  }

  if (d.equals("sensor read")) {
    // Implement serial command that forces sensor read. This will
    // momentarily turn off WiFi
    readGasSensor();
  }

  if (d.equals("sensor show")) {
    // Implement serial command that shows contents of sensorValues array

    for (int i = 0; i < 3; ++i) {
      WebSerial.print("RAW: ");
      WebSerial.println(sensorValues[i].rawAnalogRead);
      WebSerial.print("PPM: ");
      WebSerial.println(sensorValues[i].ppmReading);
      WebSerial.print("TIME: ");
      tm tempTime = sensorValues[i].readAt;
      WebSerial.println(asctime(&tempTime));
    }
  }

  if (d.equals("sensor r0")) {
    WebSerial.print("R0: ");
    WebSerial.println(r0Calibration);
  }

  if (d.equals("time")) {
    struct tm tempTime;
    getLocalTime(&tempTime);
    WebSerial.println(asctime(&tempTime));
  }

  if (d.equals("firebase status")) {
    WebSerial.print("Ready: ");
    WebSerial.println(Firebase.ready());
    WebSerial.print("SignupOK: ");
    WebSerial.println(signupOk);
  }
}

void initializeWifiAndOTA() {
  // Connect to WiFi network
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("");

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Basic index page for HTTP server, HTTP server will be
  // used for OTA and WebSerial
  server2.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "/update for OTA upload, /webserial for serial monitor");
  });

  // Elegant Async OTA
  // Start OTA server so we don't need USB cable to upload code
  AsyncElegantOTA.begin(&server2);
  server2.begin();

  // Start webserial server so we don't need USB cable to view serial monitor
  WebSerial.onMessage(receiveWebSerial);
  WebSerial.begin(&server2);
}

void setup(void) {
  pinMode(OTA_MODE_EN, INPUT);
  pinMode(GAS_SENSOR, INPUT);
  pinMode(LED_BUILTIN, OUTPUT);

  if (digitalRead(OTA_MODE_EN) == 0) {
    // Pull GPIO 13 low if you want to enable to ota flash mode
    otaFlashMode = 1;
  } else {
    otaFlashMode = 0;
  }

  // Calibrate and test the sensor before WiFi is turned on
  // because we can no longer use analogRead once WiFi radio is on
  r0Calibration = sensor.getRZero();
  // for (int i = 0; i < 3; ++i) {
  //   sensorValues[i].rawAnalogRead = analogRead(GAS_SENSOR);
  //   sensorValues[i].ppmReading = sensor.getPPM();
  //   delay(1000);
  // }

  Serial.begin(115200);

  // Since our ESP32-CAM does not have a micro USB connector, there are 2 options:
  // 1.) Use a separate programmer board that plugs in to the TX and RX pins 
  //     of the ESP32, to upload code and view the serial monitor
  // or,
  // 2.) Utilize the wifi capabilities of the ESP32 to upload code over-the-air (OTA)
  //     We have chosen this option for convenience and cost savings
  initializeWifiAndOTA();

  if (otaFlashMode == 0) {
    // OTA flash mode is a failsafe. If it is on, only OTA will work, everything
    // will be disabled. This ensures that we can reupload code to the device even
    // if we upload broken firmware to it. If OTA flash mode is off, OTA and everything else
    // will still work, but OTA might not be fast or reliable
    const long GMT_OFFSET_PH = 8;
    const char* NTP_SERVER1 = "pool.ntp.org";
    const char* NTP_SERVER2 = "time-a-g.nist.gov";
    const char* NTP_SERVER3 = "time.google.com";
    configTime(GMT_OFFSET_PH, 0, NTP_SERVER1, NTP_SERVER2, NTP_SERVER3);

    fb_config.api_key = firebaseApiKey;
    fb_config.database_url = firebaseDbUrl;
    fb_config.token_status_callback = tokenStatusCallback;
    fb_auth.user.email = ""; // Anonymous login
    fb_auth.user.password = "";

    // Login to fb
    if (Firebase.signUp(&fb_config, &fb_auth, fb_auth.user.email, fb_auth.user.password)) {
      signupOk = true;
    }

    Firebase.begin(&fb_config, &fb_auth);  // Connect to db
    // Don't let firebase library control wifi auto connect,
    // we want to control this on our own
    Firebase.reconnectWiFi(false); 
    Firebase.setDoubleDigits(5);
  }

  // If all goes well, turn on the buil-in red LED of the ESP32
  // Note that LED_BUILTIN has reverse logic, writing LOW to it turns it on
  digitalWrite(LED_BUILTIN, LOW);
}

void writeToFirebase() {
  // Since sensor reading is done three times at a time, we can get the average
  // of the last sensor readings to mitigate errors in the data
  float averagePpmSample = (sensorValues[0].ppmReading +
                           sensorValues[1].ppmReading +
                           sensorValues[2].ppmReading) / 3;

  int averageAnalogValueSample = (sensorValues[0].rawAnalogRead +
                           sensorValues[1].rawAnalogRead +
                           sensorValues[2].rawAnalogRead) / 3;

  // const char* path =  "devices/james-esp32/";
  tm tempTime = sensorValues[0].readAt;

  FirebaseJson obj;
  obj.add("ppm", averagePpmSample);
  obj.add("rawAnalog", averageAnalogValueSample);
  obj.add("readAt", mktime(&tempTime));

  if (Firebase.RTDB.pushJSON(&fb_dataObject, "devices/james-esp32", &obj)) {
    WebSerial.println(fb_dataObject.dataPath());
  } else {
    WebSerial.println(fb_dataObject.errorReason());
  }

  // if (Firebase.RTDB.setFloat(&fb_dataObject, fullPath, averagePpmSample)) {
  //   WebSerial.println(fb_dataObject.dataPath());
  // } else {
  //   WebSerial.println(fb_dataObject.errorReason());
  // }
      
  // snprintf(fullPath, sizeof(fullPath), "%s%d/%s", path, mktime(&tempTime), "rawAnalog");
  // if (Firebase.RTDB.setInt(&fb_dataObject, fullPath, averageAnalogValueSample)) {
  //   WebSerial.println(fb_dataObject.dataPath());
  // } else {
  //   WebSerial.println(fb_dataObject.errorReason());
  // }

  // snprintf(fullPath, sizeof(fullPath), "%s%d/%s", path, mktime(&tempTime), "readAt");
  // if (Firebase.RTDB.setInt(&fb_dataObject, fullPath, mktime(&tempTime))) {
  //   WebSerial.println(fb_dataObject.dataPath());
  // } else {
  //   WebSerial.println(fb_dataObject.errorReason());
  // }
}

unsigned long millisSinceLastReady = 0;

void loop(void) {
  if (otaFlashMode == 0 && (millis() - millisSinceLastReady > 30000 || millisSinceLastReady == 0)) {
    WebSerial.println("Reading gas sensor, temporarily turning off WiFi");
    readGasSensor();
    if (Firebase.ready() && signupOk) {
      millisSinceLastReady = millis();
      // Firebase.ready() should be repeatedly called but with a set interval
      // of 15s to prevent spam. We can't use the delay() function because that
      // blocks the CPU and disables other function calls, hence we use the millis
      // timer, and manually check for every 15th second, we send Firebase.ready() and other
      // fb related operations
      writeToFirebase();
    }
  }
}