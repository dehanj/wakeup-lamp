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

/* MQTT */
#define mqtt_server "IP_ADDRESS"
#define mqtt_user "username"
#define mqtt_password "pw"
#define wakeup_enable_topic "bedroom/wakeup/enable"
#define wakeup_time_topic "bedroom/wakeup/time"
#define wakeup_dur_topic "bedroom/wakeup/wake_dur"
#define wakeup_lit_dur_topic "bedroom/wakeup/lit_dur"
#define wakeup_light_topic "bedroom/wakeup/light"
#define wakeup_light_cb_topic "bedroom/wakeup/light_cb"
#define wakeup_weekend_topic "bedroom/wakeup/weekend"

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

/* touck button */
#define TOUCH_BUTTON 15

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

typedef struct {
  uint8_t hour;
  uint8_t minute;
  uint8_t duration_light_transition;
  uint8_t duration_lit;
  bool enable;
  bool weekends;
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
static wakeup_alarm_t wakeup_alarms;
static volatile bool button_pressed = false;
static bool alarm_active = false;

/* Program */

void IRAM_ATTR touch_isr()
{
  button_pressed = true;
}

void setup()
{
  Serial.begin(115200);
  Serial.println("Setup");

  client.setServer(mqtt_server, 1883);
  client.setCallback(mqtt_callback);

  attachInterrupt(TOUCH_BUTTON, touch_isr, RISING);

  ledcSetup(0, 5000, 8);
  ledcAttachPin(WARM_WHITE_LED_PIN, 0);

  memset(&wakeup_alarms, 0, sizeof(wakeup_alarm_t));
  wakeup_alarms.duration_light_transition = 15;
  wakeup_alarms.duration_lit = 15;

  xTaskCreatePinnedToCore(
    task_network_loop,
    "task_network_loop", // A name just for humans
    3072, // This stack size can be checked & adjusted by reading the Stack Highwater
    NULL,
    2, // Priority, with 3 (configMAX_PRIORITIES - 1) being the highest, and 0 being the lowest.
    NULL,
    ARDUINO_RUNNING_CORE);

  xTaskCreatePinnedToCore(
    task_wakeup_alarm,
    "task_wakeup_alarm",
    1024, // Stack size
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

  if (strcmp(topic, wakeup_enable_topic) == 0) {
    payload[length] = '\0';
    String s = String((char *) payload);
    if (s.compareTo("on") == 0) {
      wakeup_alarms.enable = 1;
    } else {
      wakeup_alarms.enable = 0;
    }

  } else if (strcmp(topic, wakeup_time_topic) == 0) {
    char tmp1[20];

    strncpy(tmp1, (char *) payload, length);
    wakeup_alarms.hour = atoi(strtok(tmp1, ":"));
    wakeup_alarms.minute = atoi(strtok(NULL, ":"));

  } else if (strcmp(topic, wakeup_dur_topic) == 0) {
    payload[length] = '\0';
    String s = String((char *) payload);
    wakeup_alarms.duration_light_transition = s.toInt();

  } else if (strcmp(topic, wakeup_lit_dur_topic) == 0) {
    payload[length] = '\0';
    String s = String((char *) payload);
    wakeup_alarms.duration_lit = s.toInt();

  } else if (strcmp(topic, wakeup_light_topic) == 0) {
    payload[length] = '\0';
    String s = String((char *) payload);

    if (s.compareTo("on") == 0) {
      light_on();
    } else {
      light_off();
    }

  } else if (strcmp(topic, wakeup_weekend_topic) == 0) {
    payload[length] = '\0';
    String s = String((char *) payload);

    if (s.compareTo("on") == 0) {
      wakeup_alarms.weekends = 1;
    } else {
      wakeup_alarms.weekends = 0;
    }
  }

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

  for (;;) {

    if (WiFi.status() != WL_CONNECTED) {
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
    }

    if (WiFi.status() == WL_CONNECTED && ntp_init == false) {
      init_ntp_time();
      ntp_init = true;
    }

    if ((!client.connected()) && (WiFi.status() == WL_CONNECTED) ) {
      // Loop until we're reconnected
      while (!client.connected()) {
        Serial.print("Attempting MQTT connection...");
        // Attempt to connect
        if (client.connect("Wakeup-client", mqtt_user, mqtt_password)) {
          Serial.println("connected");
          client.subscribe(wakeup_enable_topic);
          client.subscribe(wakeup_time_topic);
          client.subscribe(wakeup_dur_topic);
          client.subscribe(wakeup_lit_dur_topic);
          client.subscribe(wakeup_light_topic);
          client.subscribe(wakeup_weekend_topic);
        } else {
          Serial.print("failed, rc=");
          Serial.print(client.state());
          Serial.println(" try again in 5 seconds");
          vTaskDelay(5000 / portTICK_PERIOD_MS);
        }
      }
    }
    client.loop();
    // Serial.println(uxTaskGetStackHighWaterMark(NULL));

    if (button_pressed == true) {
      if (alarm_active == true) {
        alarm_active = false;
        leds_off();
        light_off();
      } else {
        if (is_light_on() == true) {
          light_off();
          client.publish(wakeup_light_cb_topic, "off");
        } else {
          light_on();
          client.publish(wakeup_light_cb_topic, "on");
        }
      }
      button_pressed = false;
    }

    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}

void task_wakeup_alarm(
  void *pvParameters)
{
  (void) pvParameters;

  for (;;) {

    if ( wakeup_alarms.enable ) {
      update_ntp_time();
      print_local_time();

      if (timeinfo.tm_hour == wakeup_alarms.hour
          && timeinfo.tm_min == wakeup_alarms.minute
          && wakeup_alarms.enable) {

        if (!(timeinfo.tm_wday == 0 || timeinfo.tm_wday == 6)) {
          trigg_wakeup_sequence();
        } else {
          if (wakeup_alarms.weekends) {
            trigg_wakeup_sequence();
          }
        }
      }
      // Serial.println(uxTaskGetStackHighWaterMark(NULL));

      uint delay = (60 - timeinfo.tm_sec) + 5; /* Aim is xx:xx:05 */

      vTaskDelay( (delay * 1000) / portTICK_PERIOD_MS );
    } else {
      vTaskDelay( 1000 / portTICK_PERIOD_MS );
    }

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
  alarm_active = true;

  Serial.println("Sequence started");
  /* 255 * 235 = 1 minute */
  int convertToDelaySteps = 235;
  int fadeDelay = wakeup_alarms.duration_light_transition
                  * convertToDelaySteps;
  Serial.println(fadeDelay);

  int k = 0;
  int j = 20;
  int l = 0;

  // Serial.println(uxTaskGetStackHighWaterMark(NULL));

  for (int i = 0; i < 256; i++) {
    if (wakeup_alarms.enable == 1 && alarm_active == true) {

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
  }
  if (wakeup_alarms.enable == 1 && alarm_active == true) {
    Serial.println("Sequence done");
    vTaskDelay(
      (wakeup_alarms.duration_lit * 1000 * 60)
      / portTICK_PERIOD_MS);
  }
  leds_off();
  light_off();
  alarm_active = false;
  vTaskDelete(NULL);
}

void leds_off()
{
  FastLED.clear();
  FastLED.show();
}

void light_on()
{
  ledcWrite(0, 255);
}

void light_off()
{
  ledcWrite(0, 0);
}

bool is_light_on()
{
  return ledcRead(0) > 0 ? true : false;
}
