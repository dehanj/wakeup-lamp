#if CONFIG_FREERTOS_UNICORE
#define ARDUINO_RUNNING_CORE 0
#else
#define ARDUINO_RUNNING_CORE 1
#endif

/* LED */
#define FASTLED_ALLOW_INTERRUPTS 0
#include "FastLED.h"

/* WIFI */
#include <WiFi.h>

/* NTP */
#include "time.h"

/* MQTT */
#include <PubSubClient.h>

/* For web sever */
#include <Arduino.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

/* SPIFFS */
#include <SPIFFS.h>

/* MQTT */
#define mqtt_server "YOUR_MQTT_SERVER_HOST"
#define mqtt_user "your_username"
#define mqtt_password "your_password"
#define wakeup_topic "bedroom/wakeup"

/* Wifi */
const char *ssid = "YOUR_SSID";
const char *password = "YOUR_PW";

/* NTP */
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;
const int daylightOffset_sec = 0;
bool ntp_init = false;

/* FASTLED */
#define NUM_LEDS 16
#define RGB_LED_PIN 13
#define WARM_WHITE_LED_PIN 12
CRGB leds[NUM_LEDS];

/* Alarm struct */
/*
   Day is regulatated by an array of structs, location
    0 = SUNDAY
    1 = MONDAY
    2 = TUSEDAY
    3 = WEDNESDAY
    4 = THURSDAT
    5 = FRIDAY
    6 = SATURDAY
*/
const char* WEEKDAYS[] = { "/Sunday.txt", "/Monday.txt", "/Tuesday.txt", "/Wednesday.txt", "/Thursday.txt", "/Friday.txt", "/Saturday.txt"};


typedef struct {
  uint8_t hour[7];
  uint8_t minute[7];
  uint8_t duration_light_transition[7];
  uint8_t duration_lit[7];
  bool reoccuring[7];
  bool set[7];
  uint8_t id[7];
} wakeup_alarm_t;

/* TASKS */
void task_network_loop(
  void *pvParameters);
void task_wakeup_sequence(
  void *pvParameters);
void task_wakeup_alarm(
  void *pvParameters);

/* GLOBAL */
WiFiClient espClient;
PubSubClient client(espClient);
static struct tm timeinfo;
AsyncWebServer server(80);
static wakeup_alarm_t wakeup_alarms;


/* Website index */

const char index_html[] PROGMEM =
  R"rawliteral(
<!DOCTYPE HTML><html><head>
  <title>ESP Wakeup Input Form</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <script>
    function submitMessage() {
      alert("Saved value!");
      setTimeout(function(){ document.location.reload(false); }, 500);
    }
  </script></head><body>
    <form action="/get" target="hidden-form">
    Write on format: Hour[0-12] : Minute[0-60] : light_duration[1-60] : duration_lit[1-60] : Reoccurring[0-1] : set[0-1]
  </form><br>
  <form action="/get" target="hidden-form">
    Monday (current value %Monday%):
    <input type="text" name="mHour">
    <input type="submit" value="Submit" onclick="submitMessage()">
  </form><br>
  <form action="/get" target="hidden-form">
    Tuesday (current value %Tuesday%):
    <input type="text" name="tuHour">
    <input type="submit" value="Submit" onclick="submitMessage()">
  </form><br>
  <form action="/get" target="hidden-form">
    Wednesday (current value %Wednesday%):
    <input type="text" name="wHour">
    <input type="submit" value="Submit" onclick="submitMessage()">
  </form>
    <form action="/get" target="hidden-form">
    Thursday (current value %Thursday%):
    <input type="text" name="thHour">
    <input type="submit" value="Submit" onclick="submitMessage()">
  </form>
    <form action="/get" target="hidden-form">
    Friday (current value %Friday%):
    <input type="text" name="fHour">
    <input type="submit" value="Submit" onclick="submitMessage()">
  </form>
    <form action="/get" target="hidden-form">
    Saturday (current value %Saturday%):
    <input type="text" name="saHour">
    <input type="submit" value="Submit" onclick="submitMessage()">
  </form>
    <form action="/get" target="hidden-form">
    Sunday (current value %Sunday%):
    <input type="texter" name="suHour">
    <input type="submit" value="Submit" onclick="submitMessage()">
  </form>
    <form action="/get" target="hidden-form">
    Clear all:
    <input type="submit" value="Clear" onclick="submitMessage()">
  </form>
  <iframe style="display:none" name="hidden-form"></iframe>
</body></html>)rawliteral";

/* Program */

void setup()
{
  Serial.begin(115200);
  Serial.println("Setup");

  client.setServer(mqtt_server, 1883);
  client.setCallback(mqtt_callback);

  ledcSetup(0, 5000, 8);
  ledcAttachPin(WARM_WHITE_LED_PIN, 0);

  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  memset(&wakeup_alarms, 0x00, sizeof(wakeup_alarm_t));

  xTaskCreatePinnedToCore(
    task_network_loop,
    "task_network_loop", // A name just for humans
    10000, // This stack size can be checked & adjusted by reading the Stack Highwater
    NULL,
    2, // Priority, with 3 (configMAX_PRIORITIES - 1) being the highest, and 0 being the lowest.
    NULL,
    ARDUINO_RUNNING_CORE);

  xTaskCreatePinnedToCore(
    task_wakeup_alarm,
    "task_wakeup_alarm",
    10000, // Stack size
    NULL,
    2, // Priority
    NULL,
    ARDUINO_RUNNING_CORE);

  LEDS.addLeds<WS2812B, RGB_LED_PIN, RGB>(leds, NUM_LEDS);
  LEDS.setBrightness(255);

  leds_off();

}

void loop()
{
}

void mqtt_callback(
  char *topic,
  byte *payload,
  unsigned int length)
{
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char) payload[i]);
  }
  Serial.println();

}

void init_ntp_time()
{
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
}

void update_ntp_time()
{
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }
}

void print_local_time()
{
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
}

void task_network_loop(
  void *pvParameters)
{
  (void) pvParameters;

  for (;;) { // A Task shall never return or exit.
    Serial.println("network loop");

    if (WiFi.status() != WL_CONNECTED) {
      // WiFi.disconnect();
      // Connect to Wi-Fi
      Serial.print("Connecting to ");
      Serial.println(ssid);
      WiFi.begin(ssid, password);
      while (WiFi.status() != WL_CONNECTED) {
        vTaskDelay(500 / portTICK_PERIOD_MS);
        Serial.print(".");
      }
      Serial.println("");
      Serial.println("WiFi connected.");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());

      setup_webiste();
    }

    if (WiFi.status() == WL_CONNECTED && ntp_init == false) {
      init_ntp_time();
      ntp_init = true;
    }

    if (0 && (!client.connected()) && (WiFi.status() == WL_CONNECTED) ) {
      // Loop until we're reconnected
      while (!client.connected()) {
        Serial.print("Attempting MQTT connection...");
        // Attempt to connect
        if (client.connect("Wakeup-client", mqtt_user, mqtt_password)) {
          Serial.println("connected");
          client.subscribe(wakeup_topic);
        } else {
          Serial.print("failed, rc=");
          Serial.print(client.state());
          Serial.println(" try again in 5 seconds");
          // Wait 5 seconds before retrying
          vTaskDelay(5000 / portTICK_PERIOD_MS);
        }
      }
    }
    client.loop();
    // Serial.println(uxTaskGetStackHighWaterMark(NULL));

    vTaskDelay(3000 / portTICK_PERIOD_MS);
  }
}

void task_wakeup_alarm(
  void *pvParameters)
{
  (void) pvParameters;

  for (;;) { // A Task shall never return or exit.
    Serial.println("Wakeup alarm");
    parse_stored_alarms();

    update_ntp_time();
    print_local_time();

    if (timeinfo.tm_hour == wakeup_alarms.hour[timeinfo.tm_wday]
        && timeinfo.tm_min == wakeup_alarms.minute[timeinfo.tm_wday]
        && wakeup_alarms.set[timeinfo.tm_wday]) {

      if (wakeup_alarms.reoccuring[timeinfo.tm_wday] == false) {
        wakeup_alarms.set[timeinfo.tm_wday] = false;
      }
      trigg_wakeup_sequence();
    }
    // Serial.println(uxTaskGetStackHighWaterMark(NULL));
    vTaskDelay( (60 * 998) / portTICK_PERIOD_MS); // Just under one minute to make sure we have a trigger
  }
}

void trigg_wakeup_sequence()
{

  xTaskCreatePinnedToCore(
    task_wakeup_sequence,
    "task_wakeup_sequence",
    1024, // Stack size
    NULL,
    2, // Priority
    NULL,
    ARDUINO_RUNNING_CORE);
}

void task_wakeup_sequence(
  void *pvParameters)
{
  (void) pvParameters;

  Serial.println("Sequence started");
  /* 255 * 235 = 1 minute */
  int convertToDelaySteps = 235;
  int fadeDelay = wakeup_alarms.duration_light_transition[timeinfo.tm_wday]
                  * convertToDelaySteps;
  Serial.println(fadeDelay);

  int k = 0;
  int j = 20;
  int l = 0;

  // Serial.println(uxTaskGetStackHighWaterMark(NULL));

  for (int i = 0; i < 256; i++) {




    if (i > 200) {
      k++;
      l = l + 3;
    } else {
      if (i % 3 == 0) {
        l++;
      }
    }

    j++;

    if (j > 255) {
      j = 255;
    }

    fill_solid(leds, NUM_LEDS, CHSV(80, 255 - k, j));
    ledcWrite(0, l);
    FastLED.show();
    vTaskDelay(fadeDelay / portTICK_PERIOD_MS);
  }
  Serial.println("Sequence done");
  vTaskDelay(
    (wakeup_alarms.duration_lit[timeinfo.tm_wday] * 1000 * 60)
    / portTICK_PERIOD_MS);
  leds_off();
  ledcWrite(0, 0);
  vTaskDelete(NULL);

}

void leds_off()
{
  FastLED.clear();
  FastLED.show();
}


/* Website */

void notFound(
  AsyncWebServerRequest *request)
{
  request->send(404, "text/plain", "Not found");
}

// Replaces placeholder with stored values
String processor(
  const String& var)
{
  Serial.println(var);
  if (var == "Monday") {
    return readFile(SPIFFS, "/Monday.txt");
  } else if (var == "Tuesday") {
    return readFile(SPIFFS, "/Tuesday.txt");
  } else if (var == "Wednesday") {
    return readFile(SPIFFS, "/Wednesday.txt");
  } else if (var == "Thursday") {
    return readFile(SPIFFS, "/Thursday.txt");
  } else if (var == "Friday") {
    return readFile(SPIFFS, "/Friday.txt");
  } else if (var == "Saturday") {
    return readFile(SPIFFS, "/Saturday.txt");
  } else if (var == "Sunday") {
    return readFile(SPIFFS, "/Sunday.txt");
  }
  return String();
}

void setup_webiste() {
  // Send web page with input fields to client
  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send_P(200, "text/html", index_html, processor);
  });
  // Send a GET request to <ESP_IP>/get?inputString=<inputMessage>
  server.on("/get", HTTP_GET, [] (AsyncWebServerRequest * request) {
    String tmp;
    if (request->hasParam("mHour")) {
      tmp += random(255);
      tmp += ":";
      tmp += request->getParam("mHour")->value();

      writeFile(SPIFFS, "/Monday.txt", tmp.c_str());


    }
    else if (request->hasParam("tuHour")) {
      tmp += random(255);
      tmp += ":";
      tmp += request->getParam("tuHour")->value();

      writeFile(SPIFFS, "/Tuesday.txt", tmp.c_str());

    }
    else if (request->hasParam("wHour")) {
      tmp += random(255);
      tmp += ":";
      tmp += request->getParam("wHour")->value();

      writeFile(SPIFFS, "/Wednesday.txt", tmp.c_str());

    }
    else if (request->hasParam("thHour")) {
      tmp += random(255);
      tmp += ":";
      tmp += request->getParam("thHour")->value();

      writeFile(SPIFFS, "/Thursday.txt", tmp.c_str());

    }
    else if (request->hasParam("fHour")) {
      tmp += random(255);
      tmp += ":";
      tmp += request->getParam("fHour")->value();

      writeFile(SPIFFS, "/Friday.txt", tmp.c_str());

    }
    else if (request->hasParam("saHour")) {
      tmp += random(255);
      tmp += ":";
      tmp += request->getParam("saHour")->value();

      writeFile(SPIFFS, "/Saturday.txt", tmp.c_str());

    }
    else if (request->hasParam("suHour")) {
      tmp += random(255);
      tmp += ":";
      tmp += request->getParam("suHour")->value();



      writeFile(SPIFFS, "/Sunday.txt", tmp.c_str());

    } else {
      //            inputMessage = "No message sent";
    }
    Serial.println("new input");
    //    request->send(200, "text/text", "Thanks!");
  });
  server.onNotFound(notFound);
  server.begin();
}

String readFile(fs::FS &fs, const char * path) {
  Serial.printf("Reading file: %s\r\n", path);
  File file = fs.open(path, "r");
  if (!file || file.isDirectory()) {
    Serial.println("- empty file or failed to open file");
    return String();
  }
  Serial.println("- read from file:");
  String fileContent;
  while (file.available()) {
    fileContent += String((char)file.read());
  }
  Serial.println(fileContent);
  return fileContent;
}

void writeFile(fs::FS &fs, const char * path, const char * message) {
  Serial.printf("Writing file: %s\r\n", path);
  File file = fs.open(path, "w");
  if (!file) {
    Serial.println("- failed to open file for writing");
    return;
  }
  if (file.print(message)) {
    Serial.println("- file written");
  } else {
    Serial.println("- write failed");
  }
}

void parse_stored_alarms() {
  char tmp1[20];
  char* token;
  for (int i = 0; i < 7; i++) {
    String tmp = readFile(SPIFFS, WEEKDAYS[i]);


    if (tmp.length() > 0 ) {
      strcpy(tmp1, tmp.c_str());
      token = strtok(tmp1, ":");

      if (wakeup_alarms.id[i] != atoi(token))
      {
        wakeup_alarms.hour[i] = atoi(strtok(NULL, ":"));
        wakeup_alarms.minute[i] = atoi(strtok(NULL, ":"));
        wakeup_alarms.duration_light_transition[i] = atoi(strtok(NULL, ":"));
        wakeup_alarms.duration_lit[i] = atoi(strtok(NULL, ":"));
        wakeup_alarms.reoccuring[i] = atoi(strtok(NULL, ":"));
        wakeup_alarms.set[i] = atoi(strtok(NULL, ":"));
      }
    }
  }
}

int check_valid_alarm_string(String input) {

}
