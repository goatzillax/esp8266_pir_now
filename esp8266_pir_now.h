#ifndef ESP8266_PIR_NOW_H
#define ESP8266_PIR_NOW_H

typedef struct {
   uint16_t	id;
   int16_t	voltage;	//  voltage * 100
   uint32_t	failberts;
   int16_t	temperature;	//  temperature(c) * 10
   int16_t	humidity;	//  humidity * 10
} struct_PIR_msg;


static inline void print_PIR_msg(struct_PIR_msg *msg, String prefix) {
   //  i hate cpp so much

   Serial.print(prefix);
   Serial.print(" ID: ");
   Serial.println(msg->id);

   Serial.print(prefix);
   Serial.print(" Voltage: ");
   Serial.println((float)msg->voltage / 100);

   Serial.print(prefix);
   Serial.print(" Failberts: ");
   Serial.println(msg->failberts);

   Serial.print(prefix);
   Serial.print(" Temperature: ");
   Serial.println((float) msg->temperature / 100);

   Serial.print(prefix);
   Serial.print(" humidity: ");
   Serial.println((float) msg->humidity / 10);

}

#endif
