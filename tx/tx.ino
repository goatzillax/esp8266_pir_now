#include <ESP8266WiFi.h>
#include <espnow.h>
#include <RTCMemory.h>
#include "esp8266_pir_now.h"
#include "sekritz.h"

#ifndef MASTER_ADDRESS
uint8_t masterAddr[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
#endif

struct_PIR_msg PIR_msg;

boolean gotosleep;

typedef struct {
   int fails;
} rtcData;

rtcData *data;

RTCMemory<rtcData> rtcMemory;

void OnDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
   if (sendStatus == 0){
      //Serial.println("Delivery success");
      data->fails = 0;
   }
   else{
      //Serial.println("Delivery fail");
      data->fails++;
   }
   gotosleep = true;
}

void setup() {
   //WiFi.persistent(false);
   Serial.begin(115200);
   //delay(1000);
   Serial.println();
   gotosleep = false;

   pinMode(LED_BUILTIN, OUTPUT);
   digitalWrite(LED_BUILTIN, LOW);
   WiFi.mode(WIFI_STA);
   //WiFi.disconnect();

   if (esp_now_init() != 0) {
      Serial.println("Error initializing ESP-NOW");
      return;
   }

   if (rtcMemory.begin()) {
      data = rtcMemory.getData();
   } else {
      data = rtcMemory.getData();
      data->fails = 0;
   }

   esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
   esp_now_register_send_cb(OnDataSent);
   esp_now_add_peer(masterAddr, ESP_NOW_ROLE_SLAVE, WIFI_CHANNEL, NULL, 0);

   // Set values to send
   PIR_msg.id = 0;
   PIR_msg.voltage = 10000;		//  bogus value
   PIR_msg.failberts = data->fails;
   PIR_msg.temperature = 10000;		//  bogus value
   PIR_msg.humidity = 1100;		//  bogus value

   esp_now_send(masterAddr, (uint8_t *) &PIR_msg, sizeof(PIR_msg));

}

void loop() {
   if (gotosleep) {
      digitalWrite(LED_BUILTIN, HIGH);
      rtcMemory.save();
      //  reschedule a wakeup to try again if failed
      //  doesn't seem to reliably put the device to sleep...
      ESP.deepSleep(0);  //  man this is some unreliable shit
      ESP.deepSleep(ESP.deepSleepMax());
   }
   delay(100);
}
