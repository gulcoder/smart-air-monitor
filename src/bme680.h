#ifndef BME680_H
#define BME680_H

#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "bme68x.h"
#include <stdbool.h>

// Initiera BME680 på vald I2C instans och SDA/SCL pins
bool bme680_init(i2c_inst_t *i2c, uint sda_pin, uint scl_pin);

// Läs sensorvärden: temperatur (°C), luftfuktighet (%), tryck (hPa), gas (ohm)
bool bme680_read(float *temperature, float *humidity, float *pressure, float *gas);

#endif

