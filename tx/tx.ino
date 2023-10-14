#include <ESP8266WiFi.h>
#include <espnow.h>
#include "esp8266_pir_now.h"
#include "sekritz.h"

#ifndef MASTER_ADDRESS
uint8_t masterAddr[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
#endif

struct_PIR_msg PIR_msg;

unsigned long lastTime = 0;
unsigned long timerDelay = 5000;

void OnDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
   Serial.print("Last Packet Send Status: ");
   if (sendStatus == 0){
      Serial.println("Delivery success");
   }
   else{
      Serial.println("Delivery fail");
   }
}
 
void setup() {
   Serial.begin(115200);

   pinMode(LED_BUILTIN, OUTPUT);

   WiFi.mode(WIFI_STA);

   if (esp_now_init() != 0) {
      Serial.println("Error initializing ESP-NOW");
      return;
   }

   esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
   esp_now_register_send_cb(OnDataSent);
   esp_now_add_peer(masterAddr, ESP_NOW_ROLE_SLAVE, 1, NULL, 0);
}

unsigned long loopStart;

void loop() {
   // i hate this but it's going away anyways when deep sleep is implemented
   loopStart = millis();

   if ((loopStart - lastTime) >= timerDelay) {
      digitalWrite(LED_BUILTIN, LOW);  //  LED sink
      // Set values to send
      PIR_msg.id = 0;
      PIR_msg.voltage = 3300;
      PIR_msg.failberts = millis();
      PIR_msg.temperature = 2500;
      PIR_msg.humidity = 300;

      esp_now_send(masterAddr, (uint8_t *) &PIR_msg, sizeof(PIR_msg));

      lastTime = loopStart;
      digitalWrite(LED_BUILTIN, HIGH);
   }

   delay(millis() - loopStart + timerDelay);
}
