#include <CircularBuffer.h>
#include <ESP8266WiFi.h>
#include <espnow.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include "esp8266_pir_now.h"
#include "sekritz.h"

//struct_PIR_msg PIR_msg;
//  TODO:  beeper

typedef struct {
   uint32_t src;	//  last 3 bytes of MAC
   uint32_t timestamp;	//  seconds since epoch.  pretty sure I don't need more that second resolution
   struct_PIR_msg msg;
} struct_log_entry;

struct_log_entry log_entry;

CircularBuffer<struct_log_entry,128> history;

String mactostr(uint8_t *mac) {
   //  I LOVE CPP
   char macStr[18] = { 0 };
   sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
   return String(macStr);
}

String longtoname(uint32_t longmac) {
   //  define ur own maczzzzz
   switch(longmac) {
      case PIR00:
         return String("PIR00");
         break;
      case PIR01:
         return String("PIR01");
         break;
      default:
         return String(longmac, HEX);
         break;
   }
}

String mactoname(uint8_t *mac) {
   //  prtty much just care about last 3 bytes
   uint32_t i=mac[5] + (mac[4]<<8) + (mac[3]<< 16);
   return longtoname(i);
}

#define INFRA
#ifdef INFRA
WiFiUDP		ntpUDP;
NTPClient	timeClient(ntpUDP);

AsyncWebServer webserver(80);

void infra_setup() {
   WiFi.begin(WIFI_SSID, WIFI_PSK);

   // Begin LittleFS
   if (!LittleFS.begin())
   {
      Serial.println("error initializing littlefs");
      return;  //  restart?
   }

   while (WiFi.status() != WL_CONNECTED) {
      delay(100);
   }
   Serial.println(WiFi.localIP());

   webserver.on("/history", HTTP_GET, [](AsyncWebServerRequest *request) {
      AsyncResponseStream *response = request->beginResponseStream("text/plain");
      int i;
      for (i=0; i<history.size(); i++) {
         response->print(longtoname(history[i].src));
         response->print(history[i].timestamp);
         response->print(history[i].msg.id);
         response->print(history[i].msg.voltage);
         response->print(history[i].msg.failberts);
         response->print("\n");  //  cmon yall docs
      }
      request->send(response);
   }
   webserver.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
   webserver.begin();

   timeClient.setTimeOffset(-5*3600);  //  meh...  consider making this the client's problem
   timeClient.begin();  //  dis feels optional but whatevs
}

void infra_loop() {
   timeClient.update();
//   if(timeClient.isTimeSet()) {
//      Serial.println(timeClient.getFormattedTime());
//   }
}
#else
void infra_setup() {
}

void infra_loop() {
}
#endif

void OnDataRecv(uint8_t *mac, uint8_t *incomingData, uint8_t len) {
   digitalWrite(LED_BUILTIN, LOW);
   if (len != sizeof(log_entry.msg)) {
      return;  //  or halt but DO NOT CATCH FIRE
   }  //  change this if payloads start to vary
   memcpy(&log_entry.msg, incomingData, sizeof(log_entry.msg));
#ifdef INFRA
   log_entry.src = mac[5] + (mac[4]<<8) + (mac[3]<< 16);
   log_entry.timestamp = timeClient.getEpochTime();
   history.push(log_entry);   
#endif
   print_PIR_msg(&log_entry.msg, mactoname(mac));
   digitalWrite(LED_BUILTIN, HIGH);
}

void setup() {
   Serial.begin(115200);

   pinMode(LED_BUILTIN, OUTPUT);
   digitalWrite(LED_BUILTIN, HIGH);

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
   delay(1000);
}
