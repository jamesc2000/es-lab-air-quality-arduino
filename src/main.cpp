#include <Arduino.h>

#include <WiFi.h>
#include <WebSerialLite.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <AsyncElegantOTA.h>

#include <FirebaseESP32.h>
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>

#define LED_BUILTIN 33
#define GAS_SENSOR 15
#define OTA_MODE_EN 13

const char *host = "esp32";
const char *ssid = "HUAWEI-V4XU";
const char *password = "Tataycruz";

const char *firebaseApiKey = "";
const char *firebaseDbUrl = "https://airquality-6d70f-default-rtdb.asia-southeast1.firebasedatabase.app/";

FirebaseData fb_dataObject;
FirebaseAuth fb_auth;
FirebaseConfig fb_config;

AsyncWebServer server2(80); // HTTP server descriptor assigned to port 80, used by the OTA and webserial server

int otaFlashMode;
int sensorValues[3];

void readGasSensor() {
  // The ADC of the GPIO pins are shared with the ADC used by the
  // WiFi antenna. To use analogRead, we first need to disconnect
  // WiFi and then reconnect after
  WiFi.disconnect(true, false);

  for (int i = 0; i < 3; ++i) {
    sensorValues[i] = analogRead(GAS_SENSOR);
    delay(1000);
  }

  WiFi.begin(ssid, password);
}

void receiveWebSerial(uint8_t* data, size_t len) {
  String d = "";
  for(int i = 0; i < len; i++){
    d += char(data[i]);
  }

  WebSerial.println(d);

  if (d.equals("mode")) {
    // Implement Serial command that shows current OTA mode for debugging purposes
    if (otaFlashMode == 1) {
      WebSerial.print("MODE: OTA flash mode");
    } else {
      WebSerial.print("MODE: Run mode");
    }
    WebSerial.println();
  }

  if (d.equals("read sensor")) {
    // Implement serial command that forces sensor read. This will
    // momentarily turn off WiFi
    readGasSensor();
  }

  if (d.equals("show sensor")) {
    // Implement serial command that shows contents of sensorValues array

    for (int i = 0; i < 3; ++i) {
      WebSerial.print("GAS: ");
      WebSerial.println(sensorValues[i]);
      delay(50);
    }
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
  if (digitalRead(OTA_MODE_EN) == 0) {
    // Pull GPIO 13 low to enable to ota flash mode
    otaFlashMode = 1;
  } else {
    otaFlashMode = 0;
  }

  pinMode(GAS_SENSOR, INPUT);
  for (int i = 0; i < 3; ++i) {
    sensorValues[i] = analogRead(GAS_SENSOR);
    delay(1000);
  }

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
    fb_config.api_key = firebaseApiKey;
    fb_config.database_url = firebaseDbUrl;
    fb_config.token_status_callback = tokenStatusCallback;
    fb_auth.user.email = ""; // Anonymous login
    fb_auth.user.password = "";

    Firebase.begin(&fb_config, &fb_auth);
    // Don't let firebase library control wifi auto connect,
    // we want to control this on our own
    Firebase.reconnectWiFi(false); 
    Firebase.setDoubleDigits(5);
  }

  // If all goes well, turn on the buil-in red LED of the ESP32
  // Note that LED_BUILTIN has reverse logic, writing LOW to it turns it on
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
}

unsigned long millisSinceLastReady = 0;

void loop(void) {
  if (otaFlashMode == 0) {
    if (Firebase.ready() && (millis() - millisSinceLastReady > 15000 || millisSinceLastReady == 0)) {
      millisSinceLastReady = millis();
      // Firebase.ready() should be repeatedly called but with a set interval
      // of 15s to prevent spam. We can't use the delay() function because that
      // blocks the CPU and disables other function calls, hence we use the millis
      // timer, and manually check for every 15th second, we send Firebase.ready() and other
      // fb related operations


      WebSerial.println("Hey");
    }
    // WebSerial.println("Hey");

    // for (int i = 0; i < 3; ++i) {
    //   WebSerial.print("GAS: ");
    //   WebSerial.println(sensorValues[i]);
    //   delay(50);
    // }

    // delay(5000);
  }
}