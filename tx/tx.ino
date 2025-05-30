#include <ESP8266WiFi.h>
#include <espnow.h>
#include <RTCMemory.h>
#include <ArduinoJson.h>
#include "esp8266_pir_now.h"

#define USE_SHT
#ifdef USE_SHT
#include <WEMOS_SHT3X.h>

SHT3X sht30(0x45);
float cTemp;
float humidity;

#endif


//  ESPRESSIF CORE IS STILL DICKED UP BUT YOU CAN FORCE THE CHANNEL WITH WIFI_SET_CHANNEL()

//  by the way, esptool.py under Arduino is broken past 2.5.0 for uploading LittleFS.  Great job guys.  And it fucking changes the baud to 408000 for no apparent reason.
//  only way to get it to work is to apparently hit it with --no-stub but then it uses the factory rom which apparently only does 115200 baud.

#define PIR_PIN D3

struct_PIR_msg PIR_msg;

#define LATCH_PIN D5
int session_tries;
#define MAX_TRIES 3

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
   uint8_t masterAddr[6];
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


void deepSleep(bool save=true) {
   if (save) {
      rtcMemory.save();
   }
   digitalWrite(LATCH_PIN, LOW);
   ESP.deepSleep(0);  //  man this is some unreliable shit
}

void setup() {

   //  todo:  for starters, use a better board with EN pinned out and PIR not on a literal fucking bootstrap pin.
   //  the very first thing is to look at the voltage.  If it's
   //  below threshold (say 2.9v) immediately go back to deep sleep.
   //  there's really not that much energy under 3.0v anyways.
   //  there should also be a hardware voltage supervisor, but we prefer
   //  to put ourselves to sleep instead of getting pantsed.  particularly
   //  if we're in the middle of some flash routine.  totally uncool man.
   //  lvc:  drops EN at 2.8v.
   //  code:  go back to sleep at 2.9v.

   int adcValue = analogRead(A0);
   uint32_t voltage = adcValue * 367 / 1023;  //  100k resistor divider with (220k + 43k) and some fudge
   if (voltage < 290) {
      deepSleep(false);
   }

   Serial.begin(115200);

#ifdef DEBUG
   delay(1000);
   Serial.println();
#endif

   pinMode(D0, WAKEUP_PULLUP);
   pinMode(LED_BUILTIN, OUTPUT);
   pinMode(LATCH_PIN, OUTPUT);
   digitalWrite(LATCH_PIN, HIGH);

   WiFi.disconnect();
   WiFi.persistent(false);
   WiFi.mode(WIFI_STA);

   if (esp_now_init() != 0) {
      Serial.println("Error initializing ESP-NOW");
      return;
   }

   if (rtcMemory.begin()) {
      //  this doesn't work.  D3 is a bootstrap pin so it must be high to even wake up.
      pinMode(PIR_PIN, INPUT);
      if (!digitalRead(PIR_PIN)) {
         deepSleep(false);
      }
      data = rtcMemory.getData();
   } else {
      data = rtcMemory.getData();
      data->fails = 0;
      data->wifi_chan = -1;

      LittleFS.begin();
      StaticJsonDocument<256> cfg;
      File file = LittleFS.open("/config.json", "r");
      auto error = deserializeJson(cfg, file);

      file.close();

      if (error) {
         Serial.println("json error");
         deepSleep(false);  //  what happens when we begin() but never save?  debug later.
      }

      //  JSON doesn't handle native hex numbers for some shitty esoteric reason.  trash.

      for (int i=0; i<6; i++) {
         data->masterAddr[i] = (uint8_t) strtoul(cfg["esp-now"]["master"][i], NULL, 16);
      }

      file.close();
   }  // "first boot" setup

   esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
   esp_now_register_send_cb(OnDataSent);

   xmit_state = XMIT_IDLE;

   //  like loop preload
   wifi_search_chan = data->wifi_chan == -1 ? 1 : data->wifi_chan;
   if (esp_now_add_peer(data->masterAddr, ESP_NOW_ROLE_SLAVE, wifi_search_chan, NULL, 0)) {
      Serial.println("error adding peer");
   }
   wifi_set_channel(wifi_search_chan);

   session_tries = 0;
}

void loop() {
   switch (xmit_state) {
      case XMIT_FINISHED:
         if (data->wifi_chan == -1) {
            if (data->fails == 0) {
               data->wifi_chan = wifi_search_chan;
               deepSleep();  // defo save the channel
            }  //  message succeeded
            else {
               //  OK so obviously you can only have one peer associated with a channel...  ?
               if (esp_now_del_peer(data->masterAddr)) {
                  Serial.println("error deleting peer");
               }
               wifi_search_chan += 1;
               if (wifi_search_chan > 11) {
                  wifi_search_chan = 1;
               } //  halt but do not catch fire?
               wifi_set_channel(wifi_search_chan);
               if (esp_now_add_peer(data->masterAddr, ESP_NOW_ROLE_SLAVE, wifi_search_chan, NULL, 0)) {
                  Serial.println("error adding peer");
               }
            }  //  message failed, cycle channel
         }  //  in search mode
         else {
            if ((data->fails != 0) && (session_tries < MAX_TRIES)) {
               rtcMemory.save();
               delay(random(500,1000));
            }  //  random backoff and retry because deepsleep with timeout seems buggy
            else {
               deepSleep();  // save failberts
            }
         }  //  not in search mode
         xmit_state = XMIT_IDLE;
         break;
      case XMIT_IDLE:
         {
            int adcValue = analogRead(A0);
            uint32_t voltage = adcValue * 367 / 1023;  //  100k resistor divider with (220k + whatever you add in; usually 43k or 51k) and some fudge

            PIR_msg.id = 0;  //  actually we have a mac on the RX end so...  this is more like a cmd
            PIR_msg.voltage = (uint16_t)(voltage);
            PIR_msg.failberts = data->fails;
#ifdef USE_SHT
            if (sht30.get()==0) {
               //Serial.println(String(sht30.cTemp)+"C");
               //Serial.println(String(sht30.humidity)+"%");
               PIR_msg.temperature = (int) (sht30.cTemp * 10);
               PIR_msg.humidity    = (int) (sht30.humidity * 10);
#ifdef DEBUG
               Serial.println(String(PIR_msg.temperature/10)+"C");
               Serial.println(String(PIR_msg.humidity/10)+"%");
               delay(100);
#endif
            } else
#endif
            {
               PIR_msg.temperature = 1000;  //  bogus value
               PIR_msg.humidity = 1100;     //  bogus value
            }
            if (data->wifi_chan == -1) {
               Serial.print("searching channel ");
               Serial.println(wifi_search_chan);
            }

            xmit_state = XMIT_STARTED;
            session_tries++;
            digitalWrite(LED_BUILTIN, LOW);
            esp_now_send(data->masterAddr, (uint8_t *)&PIR_msg, sizeof(PIR_msg));
         }
      case XMIT_STARTED:
         delay(100);
         break;
      default:
         Serial.println("WAT?");
         break;
   }
}
