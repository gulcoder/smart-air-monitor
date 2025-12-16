#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <stdbool.h>
#include <stdint.h>

bool mqtt_init(void);
bool mqtt_publish(const char* topic, const char* payload);
bool mqtt_loop(void);

#endif
