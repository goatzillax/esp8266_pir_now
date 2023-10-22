#include <ESP8266WiFi.h>
#include <espnow.h>
#include <RTCMemory.h>
#include "esp8266_pir_now.h"
#include "sekritz.h"

//  ESPRESSIF CORE IS FUCKING BUGGED SO MOST OF THIS CODE MEANS DICK ALL BECAUSE ESP-NOW SDK IS LOCKED TO CHANNEL 1 ON AN 8266.  I wrote all this code for literally nothing.  THANKS ESPRESSIF.

#ifndef MASTER_ADDRESS
uint8_t masterAddr[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
#endif

#define PIR_PIN D3

struct_PIR_msg PIR_msg;

enum e_xmit_state {
   XMIT_IDLE,
   XMIT_STARTED,
   XMIT_FINISHED
};

enum e_xmit_state xmit_state;

int wifi_search_chan;

typedef struct {
   uint32_t fails;
   int wifi_chan;
} rtcData;

rtcData *data;

RTCMemory<rtcData> rtcMemory;

void OnDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
   digitalWrite(LED_BUILTIN, HIGH);
   if (sendStatus == 0) {
      //Serial.println("Delivery success");
      data->fails = 0;
   } else {
      //Serial.println("Delivery fail");
      data->fails++;
   }
   xmit_state = XMIT_FINISHED;
}

void deepSleep() {
   rtcMemory.save();
   ESP.deepSleep(0);  //  man this is some unreliable shit
   ESP.deepSleep(ESP.deepSleepMax());
}

void setup() {
   Serial.begin(115200);

   //Serial.println();

   pinMode(D0, WAKEUP_PULLUP);
   pinMode(LED_BUILTIN, OUTPUT);

   WiFi.disconnect();
   WiFi.persistent(false);
   WiFi.mode(WIFI_STA);

   if (esp_now_init() != 0) {
      Serial.println("Error initializing ESP-NOW");
      return;
   }

   if (rtcMemory.begin()) {
      //  ok might need to debug this, but if this isn't firstboot then bail cuz this was a timer wakeup
      pinMode(PIR_PIN, INPUT);
      if (!digitalRead(PIR_PIN)) {
         deepSleep();  //  remember this automatically saves...
      }
      data = rtcMemory.getData();
   } else {
      data = rtcMemory.getData();
      data->fails = 0;
      data->wifi_chan = -1;
   }  // "first boot" setup

   esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
   esp_now_register_send_cb(OnDataSent);

   xmit_state = XMIT_IDLE;

   //  like loop preload
   wifi_search_chan = data->wifi_chan == -1 ? 1 : data->wifi_chan;
   if (esp_now_add_peer(masterAddr, ESP_NOW_ROLE_SLAVE, wifi_search_chan, NULL, 0)) {
      Serial.println("error adding peer");
   }

}

void loop() {
   switch (xmit_state) {
      case XMIT_FINISHED:
         if (data->wifi_chan == -1) {
            if (data->fails == 0) {
               data->wifi_chan = wifi_search_chan;
               deepSleep();
            }  //  message succeeded
            else {
               //  OK so obviously you can only have one peer associated with a channel...  ?
               if (esp_now_del_peer(masterAddr)) {
                  Serial.println("error deleting peer");
               }
               //wifi_search_chan += 1;
               if (wifi_search_chan > 11) {
                  wifi_search_chan = 1;
               } //  halt but do not catch fire?
               if (esp_now_add_peer(masterAddr, ESP_NOW_ROLE_SLAVE, wifi_search_chan, NULL, 0)) {
                  Serial.println("error adding peer");
               }
            }  //  message failed, cycle channel
         }  //  in search mode
         else {
            deepSleep(); 
         }  //  not in search mode
         xmit_state = XMIT_IDLE;
         break;
      case XMIT_IDLE:
         {
            int adcValue = analogRead(A0);
            uint32_t voltage = adcValue * 355 / 1023;  //  100k resistor divider with (220k + 43k) and some fudge

            PIR_msg.id = 0;  //  actually we have a mac on the RX end so...  this is more like a cmd
            PIR_msg.voltage = (uint16_t)(voltage);
            PIR_msg.failberts = data->fails;
            PIR_msg.temperature = 10000;  //  bogus value
            PIR_msg.humidity = 1100;      //  bogus value

            if (data->wifi_chan == -1) {
               Serial.print("searching channel ");
               Serial.println(wifi_search_chan);
            }

            xmit_state = XMIT_STARTED;
            digitalWrite(LED_BUILTIN, LOW);
            esp_now_send(masterAddr, (uint8_t *)&PIR_msg, sizeof(PIR_msg));
         }
      case XMIT_STARTED:
         delay(100);
         break;
      default:
         Serial.println("WAT?");
         break;
   }
}
