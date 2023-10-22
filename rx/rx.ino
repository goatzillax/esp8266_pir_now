#include <ESP8266WiFi.h>
#include <espnow.h>
#include <CircularBuffer.h>
#include "esp8266_pir_now.h"
#include "sekritz.h"

struct_PIR_msg PIR_msg;

typedef struct {
   uint32_t src;  //  last 3 bytes of MAC
   uint32_t timestamp;  //  seconds since epoch.  pretty sure I don't need more that second resolution
   struct_PIR_msg msg;
} struct_log_entry;

CircularBuffer<struct_log_entry,128> history;

String mactostr(uint8_t *mac) {
   //  I LOVE CPP
   char macStr[18] = { 0 };
   sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
   return String(macStr);
}

String mactoname(uint8_t *mac) {
   //  prtty much just care about last 3 bytes
   uint32_t i=mac[5] + (mac[4]<<8) + (mac[3]<< 16);

   //  define ur own maczzzzz

   switch(i) {
      case PIR00:
         return String("PIR00");
         break;
      case PIR01:
         return String("PIR01");
         break;
      default:
         return mactostr(mac);
         break;
   }
}

void OnDataRecv(uint8_t *mac, uint8_t *incomingData, uint8_t len) {
   digitalWrite(LED_BUILTIN, LOW);
   if (len != sizeof(PIR_msg)) {
      return;  //  or halt but DO NOT CATCH FIRE
   }  //  change this if payloads start to vary
   memcpy(&PIR_msg, incomingData, sizeof(PIR_msg));
   print_PIR_msg(&PIR_msg, mactoname(mac));
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

   WiFi.mode(WIFI_STA);

   if (esp_now_init() != 0) {
      Serial.println("Error initializing ESP-NOW");
     return;
   }
   esp_now_set_self_role(ESP_NOW_ROLE_SLAVE);
   esp_now_register_recv_cb(OnDataRecv);
}

void loop() {
   delay(100);  
}
