#include <CircularBuffer.h>
#include <ESP8266WiFi.h>
#include <espnow.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>

#define USE_OTA
#ifdef USE_OTA
#include <ElegantOTA.h>
#endif

#include <ArduinoJson.h>
#include <LittleFS.h>
#include "esp8266_pir_now.h"
#include "sekritz.h"

const char compile_date[] = __DATE__ " " __TIME__;

typedef struct {
   uint32_t src;	//  last 3 bytes of MAC
   uint32_t timestamp;	//  seconds since epoch.  pretty sure I don't need more than second resolution
   struct_PIR_msg msg;
} struct_log_entry;

struct_log_entry log_entry;

CircularBuffer<struct_log_entry,256> history;

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

#if 0
String mactostr(uint8_t *mac) {
   //  I LOVE CPP
   char macStr[18] = { 0 };
   sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
   return String(macStr);
}
#endif

//  offloaded name lookup completely to the client
String longtoname(uint32_t longmac) {
   return String(longmac, HEX);
}

String mactoname(uint8_t *mac) {
   //  prtty much just care about last 3 bytes
   uint32_t i=mac[5] + (mac[4]<<8) + (mac[3]<< 16);
   return longtoname(i);
}

StaticJsonDocument<1024> sensors;

//  json file functions from the ESP side:
//  add newly found sensors to the assoc array
//  store persistent status

//  load seems to be a pretty "free" operation
void loadSensors() {
   File file = LittleFS.open("/www/sensors.json", "r");

   deserializeJson(sensors, file);

   file.close();
}

//  this is less free...
void saveSensors() {
   File file = LittleFS.open("/www/sensors.json", "w");
   if (!file) {
      return;
   }
   serializeJson(sensors, file);
   file.close();
}

WiFiUDP		ntpUDP;
NTPClient	timeClient(ntpUDP);

unsigned long	epoch;  //  offline substitute for NTPClient.

AsyncWebServer webserver(80);

#define WIFI_STA_WAIT 10000

//  pretty sure I would be better served with a getNames() or something but of course that doesn't exist
//  i.e. snooze is a setstatus(2) plus start and duration...  bleh fix later.
boolean setstatus(AsyncWebServerRequest *request, uint32_t value) {
   //  all params starting with "0x" are names
   boolean wb = false;
   for (int i=0; i<request->params(); i++) {
      AsyncWebParameter *p = request->getParam(i);
      if (p->name().substring(0, 2) == "0x") {
         if (sensors.containsKey(p->name())) {
            sensors[p->name()]["status"] = value;
            wb = true;
         }
      }
   }
   return wb;
}

//  yeah...  fixme plox
boolean setNames(AsyncWebServerRequest *request) {
   //  all params starting with "0x" are names
   boolean wb = false;
   for (int i=0; i<request->params(); i++) {
      AsyncWebParameter *p = request->getParam(i);
      if (p->name().substring(0, 2) == "0x") {
         if (sensors.containsKey(p->name())) {
            sensors[p->name()]["name"] = request->arg(p->name());
            wb = true;
         }
      }
   }
   return wb;
}

boolean setSnooze(AsyncWebServerRequest *request, uint32_t tstart, uint32_t duration) {
   //  all params starting with "0x" are names
   boolean wb = false;
   for (int i=0; i<request->params(); i++) {
      AsyncWebParameter *p = request->getParam(i);
      if (p->name().substring(0, 2) == "0x") {
         if (sensors.containsKey(p->name())) {
            sensors[p->name()]["status"] = 2;
            sensors[p->name()]["start"] = tstart;
            sensors[p->name()]["duration"] = duration;
            wb = true;
         }
      }
   }
   return wb;
}

void infra_setup() {
   // Begin LittleFS
   if (!LittleFS.begin())
   {
      Serial.println("error initializing littlefs");
      delay(5000);
      ESP.reset();
   }

   StaticJsonDocument<256> cfg;
   File file = LittleFS.open("config.json","r");
   deserializeJson(cfg, file);
   file.close();

   WiFi.begin(String(cfg["wifi"]["sta"]["ssid"]), String(cfg["wifi"]["sta"]["psk"]));

   unsigned long wifi_start_time=millis();
   reset_requested = 0;  //  extremely low likelihood of hitting this exactly at zero
   

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
      WiFi.softAP(String(cfg["wifi"]["ap"]["ssid"]), String(cfg["wifi"]["ap"]["psk"]), cfg["wifi"]["ap"]["chan"], 1);   //  ERROR CHECKING IS 4 SUCKAZ
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
         else if (p->value().compareTo("enable") == 0) {
            if (setstatus(request, 1)) {
               saveSensors();
            }
            request->redirect("/");
         }
         else if (p->value().compareTo("disable") == 0) {
            if (setstatus(request, 0)) {
               saveSensors();
            }
            request->redirect("/");
         }
         else if (p->value().compareTo("snooze") == 0) {
            //  ugh this will get tedious fast need to figure out how to shove my fist in NTPClient
            uint32_t tstart = WiFi.status() == WL_CONNECTED ? timeClient.getEpochTime() : epoch + millis()/1000;
            uint32_t duration = strtoul(request->arg("snooze").c_str(), NULL, 10);  //  lol c++
            if (setSnooze(request, tstart, duration)) {
               saveSensors();
            }
            request->redirect("/");
         }
         else if (p->value().compareTo("rename") == 0) {
            if (setNames(request)) {
               saveSensors();
            }
            request->redirect("/");
         }
         else if (p->value().compareTo("now") == 0) {
            //  kind of would like to find a way to shove my fist in NTPClient...  ah whatevs
            if (WiFi.status() == WL_CONNECTED) {
               request->send(200, "text/plain", "ay stupid u can't use this");
            }
            else {
               if (!request->hasParam("now")) {
                  request->send(200, "text/plain", "FU PENGUIN");
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

   //  hope I has enuf memory for dis...
   //  actually I wonder if the server code kicks out the buffers as soon as the first print is reached.
   //  that would be amazeballs
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
         response->print(", ");
         response->print(history[i].msg.temperature);
         response->print(", ");
         response->print(history[i].msg.humidity);
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

   webserver.on("/debug", HTTP_GET, [](AsyncWebServerRequest *request) {
      AsyncResponseStream *response = request->beginResponseStream("text/plain");
      response->print(compile_date);
      response->print("\n");
      response->print("heap :");
      response->print(String(ESP.getFreeHeap()));
      response->print("\n");
      request->send(response);
   });

   webserver.serveStatic("/", LittleFS, "/www/").setDefaultFile("index.html");

#ifdef USE_OTA
   ElegantOTA.begin(&webserver);
#endif

   loadSensors();
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

   String sensor = "0x"+String(log_entry.src, HEX);

   //  if it's a new sensor, add it to the json doc, default disabled
   if (!sensors.containsKey(sensor)) {
      sensors.createNestedObject(sensor);
      sensors[sensor]["status"] = 0;
      saveSensors();
      return;  //  and we're done
   }

   if (sensors[sensor]["status"] != 0) {
      if (sensors[sensor]["status"] == 2) {
         // ah shit here's where the rollover maths come into play.
         unsigned long tstart = sensors[sensor]["start"];
         unsigned long tduration = sensors[sensor]["duration"];
         if (log_entry.timestamp - tstart > tduration) {
            sensors[sensor]["status"] = 1;
            saveSensors();
         }
         else {
            return;  //  and we're done
         }
      }
      start_buzzer();
   }
   //print_PIR_msg(&log_entry.msg, mactoname(mac));
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
