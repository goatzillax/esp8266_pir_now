#include <ESP8266WiFi.h>
#include <espnow.h>
#include "esp8266_pir_now.h"
#include "sekritz.h"

struct_PIR_msg PIR_msg;

void OnDataRecv(uint8_t * mac, uint8_t *incomingData, uint8_t len) {
   digitalWrite(LED_BUILTIN, LOW);
   memcpy(&PIR_msg, incomingData, sizeof(PIR_msg));
   print_PIR_msg(&PIR_msg);
   digitalWrite(LED_BUILTIN, HIGH);
}
 
void setup() {
   Serial.begin(115200);

   pinMode(LED_BUILTIN, OUTPUT);
   digitalWrite(LED_BUILTIN, HIGH);
//#define DEBUG
#ifdef DEBUG
   while (!Serial) {}
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
