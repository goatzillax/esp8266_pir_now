#include <CircularBuffer.h>
#include <ESP8266WiFi.h>
#include <espnow.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>

#include <ElegantOTA.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "esp8266_pir_now.h"
#include "sekritz.h"

typedef struct {
   uint32_t src;	//  last 3 bytes of MAC
   uint32_t timestamp;	//  seconds since epoch.  pretty sure I don't need more that second resolution
   struct_PIR_msg msg;
} struct_log_entry;

struct_log_entry log_entry;

CircularBuffer<struct_log_entry,128> history;

#define BUZZER_PATTERN_LEN  6
#define BUZZER_DELAY        1000
const int buzzer_pattern[BUZZER_PATTERN_LEN] = { HIGH, LOW, HIGH, LOW, HIGH, LOW };

int buzzer_state=BUZZER_PATTERN_LEN;
unsigned long buzzer_start;

//  Receiver don't got no PIR on itself
#define BUZZER_PIN	D5

void start_buzzer() {
   if (buzzer_state == BUZZER_PATTERN_LEN) {
      buzzer_state = 0;
      buzzer_start = millis();
   }
}

unsigned long reset_requested;

void cycle_reset() {
   if (reset_requested == 0) {
      return;
   }
   if (millis() - reset_requested > 5000) {
      ESP.reset();
   }
}

//  I like writing schedulers.  I really really like writing schedulers.
void cycle_buzzer() {
   if (buzzer_state == BUZZER_PATTERN_LEN) {
      return;
   }

   digitalWrite(BUZZER_PIN, buzzer_pattern[buzzer_state]);
   digitalWrite(LED_BUILTIN, !buzzer_pattern[buzzer_state]);

   if (millis() - buzzer_start > BUZZER_DELAY) {
      buzzer_state++;
      buzzer_start = millis();
   }
}

String mactostr(uint8_t *mac) {
   //  I LOVE CPP
   char macStr[18] = { 0 };
   sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
   return String(macStr);
}

//  offloaded name lookup completely to the client
String longtoname(uint32_t longmac) {
   return String(longmac, HEX);
}

String mactoname(uint8_t *mac) {
   //  prtty much just care about last 3 bytes
   uint32_t i=mac[5] + (mac[4]<<8) + (mac[3]<< 16);
   return longtoname(i);
}

WiFiUDP		ntpUDP;
NTPClient	timeClient(ntpUDP);

unsigned long	epoch;  //  offline substitute for NTPClient.

AsyncWebServer webserver(80);

#define WIFI_STA_WAIT 10000

// Taken from forked NTP client https://github.com/taranais/NTPClient/blob/master/NTPClient.cpp
String getFormattedTime(unsigned long secs) {
  unsigned long rawTime = secs;
  unsigned long hours = (rawTime % 86400L) / 3600;
  String hoursStr = hours < 10 ? "0" + String(hours) : String(hours);

  unsigned long minutes = (rawTime % 3600) / 60;
  String minuteStr = minutes < 10 ? "0" + String(minutes) : String(minutes);

  unsigned long seconds = rawTime % 60;
  String secondStr = seconds < 10 ? "0" + String(seconds) : String(seconds);

  return hoursStr + minuteStr + secondStr;
}

// Based on https://github.com/PaulStoffregen/Time/blob/master/Time.cpp
// currently assumes UTC timezone, instead of using this->_timeOffset
#define LEAP_YEAR(Y)     ( (Y>0) && !(Y%4) && ( (Y%100) || !(Y%400) ) )
String getFormattedDateTime(unsigned long secs) {
  unsigned long rawTime = secs / 86400L;  // in days
  unsigned long days = 0, year = 1970;
  uint8_t month;
  static const uint8_t monthDays[]={31,28,31,30,31,30,31,31,30,31,30,31};

  while((days += (LEAP_YEAR(year) ? 366 : 365)) <= rawTime)
    year++;
  rawTime -= days - (LEAP_YEAR(year) ? 366 : 365); // now it is days in this year, starting at 0
  days=0;
  for (month=0; month<12; month++) {
    uint8_t monthLength;
    if (month==1) { // february
      monthLength = LEAP_YEAR(year) ? 29 : 28;
    } else {
      monthLength = monthDays[month];
    }
    if (rawTime < monthLength) break;
    rawTime -= monthLength;
  }
  String monthStr = ++month < 10 ? "0" + String(month) : String(month); // jan is month 1
  String dayStr = ++rawTime < 10 ? "0" + String(rawTime) : String(rawTime); // day of month
  return String(year) + monthStr + dayStr + "T" + getFormattedTime(secs) + "Z";
}


void infra_setup() {
   WiFi.begin(WIFI_SSID, WIFI_PSK);

   unsigned long wifi_start_time=millis();
   reset_requested = 0;  //  extremely low likelihood of hitting this exactly at zero
   
   // Begin LittleFS
   if (!LittleFS.begin())
   {
      Serial.println("error initializing littlefs");
      return;  //  restart?
   }

   while (WiFi.status() != WL_CONNECTED) {
      if (millis() - wifi_start_time > WIFI_STA_WAIT) {
         break;
      }
      delay(100);  //  get the feeling this is needed...
   }
   //  schitt or get off the pot time
   if (WiFi.status() == WL_CONNECTED) {
      Serial.println(WiFi.localIP());
      timeClient.begin();  //  dis feels optional but whatevs
   }
   else {
      WiFi.disconnect();
      //  no longer in infra mode (forever?)
      //  Scan for clearest channel?
      WiFi.softAP(SOFTAP_SSID, SOFTAP_PSK, SOFTAP_CHAN, 1);   //  ERROR CHECKING IS 4 SUCKAZ
      Serial.println(WiFi.softAPIP());
   }

   //  kekeke

   webserver.on("/action_page.php", HTTP_GET, [](AsyncWebServerRequest *request) {
      AsyncWebParameter *p = NULL;
      if (request->hasParam("cmd")) {
         p = request->getParam("cmd");

         if (p->value().compareTo("restart") == 0) {
            //  can't reset here because we can't confirm the client has moved off this URL...
            reset_requested = millis();
            request->redirect("/");
         }
         else if (p->value().compareTo("now") == 0) {
            //  kind of would like to find a way to shove my fist in NTPClient...  ah whatevs
            if (WiFi.status() == WL_CONNECTED) {
               request->send(200, "text/plain", "ay stupid u can't use this");
            }
            else {
               if (!request->hasParam("now")) {
                  request->send(200, "text/plain", "FU");
                  return;
               }
               //  lol c++
               epoch =  strtoul(request->getParam("now")->value().c_str(), NULL, 10) - millis()/1000;
               Serial.println(epoch);
               request->send(200, "text/plain", "k u have 49 days to come back and fix this");
            }
         }
      }
   });

   webserver.on("/history", HTTP_GET, [](AsyncWebServerRequest *request) {
      AsyncResponseStream *response = request->beginResponseStream("text/plain");
      int i;
      for (i=history.size()-1; i>=0; i--) {
         response->print(getFormattedDateTime(history[i].timestamp));
         response->print(" 0x");
         response->print(longtoname(history[i].src));
         response->print(" ");
         response->print(history[i].msg.id);
         response->print(" ");
         response->print(String((float) history[i].msg.voltage/100));
         response->print("v ");
         response->print(history[i].msg.failberts);
         response->print("\n");
      }
      request->send(response);
   });

   //  hope I has enuf memory for dis...
   webserver.on("/history.json", HTTP_GET, [](AsyncWebServerRequest *request) {
      AsyncResponseStream *response = request->beginResponseStream("application/json");
      int i;
      response->print("[\n");
      for (i=history.size()-1; i>=0; i--) {
         response->print("[");
         response->print(history[i].timestamp);
         response->print(" ,\"0x");
         response->print(longtoname(history[i].src));
         response->print("\", ");
         response->print(history[i].msg.id);
         response->print(", ");
         response->print(String((float) history[i].msg.voltage/100));
         response->print(", ");
         response->print(history[i].msg.failberts);
         response->print("]");
         if (i != 0) {
            response->print(",\n");
         }
         else {
            response->print("\n");
         }
      }
      response->print("]");
      request->send(response);
   });


   webserver.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

   ElegantOTA.begin(&webserver);

   webserver.begin();
}

void infra_loop() {
   if (WiFi.status() == WL_CONNECTED) {
      timeClient.update();
   }
}

void OnDataRecv(uint8_t *mac, uint8_t *incomingData, uint8_t len) {
   if (len != sizeof(log_entry.msg)) {
      return;  //  or halt but DO NOT CATCH FIRE
   }  //  change this if payloads start to vary
   memcpy(&log_entry.msg, incomingData, sizeof(log_entry.msg));

   log_entry.src = mac[5] + (mac[4]<<8) + (mac[3]<< 16);
   if (WiFi.status() == WL_CONNECTED) {
      //  uh, wat 2 do if time ain't set?
      log_entry.timestamp = timeClient.getEpochTime();
   }
   else {
      log_entry.timestamp = epoch + millis()/1000;
   }
   history.push(log_entry);   
   start_buzzer();
   print_PIR_msg(&log_entry.msg, mactoname(mac));
}

void setup() {
   Serial.begin(115200);

   pinMode(LED_BUILTIN, OUTPUT);
   pinMode(BUZZER_PIN, OUTPUT);

   digitalWrite(LED_BUILTIN, HIGH);
   digitalWrite(BUZZER_PIN, LOW);

#define DEBUG
#ifdef DEBUG
   while (!Serial) {}
   Serial.println();
   Serial.print("ESP8266 Board MAC Address:  ");
   Serial.println(WiFi.macAddress());
#endif

   WiFi.disconnect();
   WiFi.persistent(false);
   WiFi.setSleepMode(WIFI_NONE_SLEEP);  // fucking clown shoes I swear
   WiFi.mode(WIFI_STA);

   infra_setup();

   if (esp_now_init() != 0) {
      Serial.println("Error initializing ESP-NOW");
      return;
   }
   esp_now_set_self_role(ESP_NOW_ROLE_SLAVE);
   esp_now_register_recv_cb(OnDataRecv);
}

void loop() {
   infra_loop();
   cycle_buzzer();
   cycle_reset();
   delay(100);
}
