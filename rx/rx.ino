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
      WiFi.softAP(SOFTAP_SSID, SOFTAP_PSK, SOFTAP_CHAN);   //  ERROR CHECKING IS 4 SUCKAZ
      Serial.println(WiFi.softAPIP());
   }

   webserver.on("/history", HTTP_GET, [](AsyncWebServerRequest *request) {
      AsyncResponseStream *response = request->beginResponseStream("text/plain");
      int i;
      for (i=history.size()-1; i>=0; i--) {
         response->print(getFormattedDateTime(history[i].timestamp));
         response->print(" ");
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
   webserver.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
   webserver.begin();
}

void infra_loop() {
   if (WiFi.status() == WL_CONNECTED) {
      timeClient.update();
   }
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
   delay(10000);
}
