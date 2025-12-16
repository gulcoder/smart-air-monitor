#include "bme680.h"
#include <string.h>
#include "bme68x_defs.h" // <-- VIKTIG: Lades till för att få I2C-adress-definitioner

static struct bme68x_dev gas_sensor;
static i2c_inst_t *bme680_i2c;

// Vi antar standard I2C-adress 0x76. 
// Ändra till BME68X_I2C_ADDR_HIGH (0x77) om du använder den.
static uint8_t bme_dev_addr = BME68X_I2C_ADDR_LOW;

// Callbackfunktion för delay (Pico SDK)
static void bme68x_delay_us(uint32_t period, void *intf_ptr) {
    sleep_us(period);
}

// I2C read enligt NYA BME68x API
// (Signaturen är ändrad: 'dev_id' är borta)
static int8_t i2c_read(uint8_t reg_addr, uint8_t *data, uint32_t len, void *intf_ptr) {
    // Hämta I2C-adressen från intf_ptr
    uint8_t dev_id = *(uint8_t*)intf_ptr;
    // Hämta I2C-bussen från den globala variabeln
    i2c_inst_t *i2c = bme680_i2c;

    if (i2c_write_blocking(i2c, dev_id, &reg_addr, 1, true) != 1) return -1; // true = skicka STOP efteråt
    if (i2c_read_blocking(i2c, dev_id, data, (long unsigned int)len, false) != (int)len) return -1; // false = skicka STOP
    return 0;
}

// I2C write enligt NYA BME68x API
// (Signaturen är ändrad: 'dev_id' är borta)
static int8_t i2c_write(uint8_t reg_addr, const uint8_t *data, uint32_t len, void *intf_ptr) {
    // Hämta I2C-adressen från intf_ptr
    uint8_t dev_id = *(uint8_t*)intf_ptr;
    // Hämta I2C-bussen från den globala variabeln
    i2c_inst_t *i2c = bme680_i2c;
    
    uint8_t buf[len + 1];
    buf[0] = reg_addr;
    memcpy(buf + 1, data, len); // Effektivare än en for-loop

    if (i2c_write_blocking(i2c, dev_id, buf, len + 1, false) != (int)(len + 1)) return -1; // false = skicka STOP
    return 0;
}

// Init BME680
bool bme680_init(i2c_inst_t *i2c, uint sda_pin, uint scl_pin) {
    bme680_i2c = i2c; // Spara I2C-bussen globalt för read/write

    // Init I2C
    i2c_init(i2c, 100000);
    gpio_set_function(sda_pin, GPIO_FUNC_I2C);
    gpio_set_function(scl_pin, GPIO_FUNC_I2C);
    gpio_pull_up(sda_pin);
    gpio_pull_up(scl_pin);

    memset(&gas_sensor, 0, sizeof(gas_sensor));
    gas_sensor.intf = BME68X_I2C_INTF;
    
    // ** FIX 1: Peka intf_ptr till vår I2C-adress-variabel **
    gas_sensor.intf_ptr = &bme_dev_addr; 
    
    gas_sensor.read = i2c_read;
    gas_sensor.write = i2c_write;
    gas_sensor.delay_us = bme68x_delay_us;
    
    // ** FIX 2: Denna rad är borttagen, den finns inte i nya API:et **
    // gas_sensor.variant = BME68X_VARIANT_680;

    // API:et kommer nu anropa bme68x_init(), som anropar vår i2c_read()
    // som nu korrekt hämtar adressen (0x76) från intf_ptr.
    if (bme68x_init(&gas_sensor) != BME68X_OK) return false;

    return true;
}

// Läs sensorvärden
bool bme680_read(float *temperature, float *humidity, float *pressure, float *gas) {
    struct bme68x_data data;
    uint8_t n_fields = 0;
    
    // Konfigurera sensorn för att mäta
    struct bme68x_conf conf;
    conf.filter = BME68X_FILTER_OFF;
    conf.odr = BME68X_ODR_NONE;
    conf.os_hum = BME68X_OS_16X;
    conf.os_pres = BME68X_OS_1X;
    conf.os_temp = BME68X_OS_2X;
    if (bme68x_set_conf(&conf, &gas_sensor) != BME68X_OK) return false;

    // 2. Konfigurera Värmaren (Detta är NYTT!)
    struct bme68x_heatr_conf heatr_conf;
    heatr_conf.enable = BME68X_ENABLE;       // Slå PÅ värmaren
    heatr_conf.heatr_temp = 320;             // Värm till 320 grader
    heatr_conf.heatr_dur = 150;              // Håll i 150 ms
    if (bme68x_set_heatr_conf(BME68X_FORCED_MODE, &heatr_conf, &gas_sensor) != BME68X_OK) {
        return false;
    }

    // Sätt forced mode för att läsa en gång
    if (bme68x_set_op_mode(BME68X_FORCED_MODE, &gas_sensor) != BME68X_OK) return false;

    // 4. Beräkna väntetid (Mätning + Värmare) - HÄR ÄR SKILLNADEN
    uint32_t meas_dur = bme68x_get_meas_dur(BME68X_FORCED_MODE, &conf, &gas_sensor);
    uint32_t heat_dur = heatr_conf.heatr_dur * 1000; // ms till us

    // Vi väntar tillräckligt länge för båda
    gas_sensor.delay_us(meas_dur + heat_dur, gas_sensor.intf_ptr);



    // Läs sensor
    if (bme68x_get_data(BME68X_FORCED_MODE, &data, &n_fields, &gas_sensor) != BME68X_OK) return false;
    
    // n_fields > 0 kollar bara om *någon* data kom, men vi vill ha temp/hum/tryck
    if (n_fields && (data.status & BME68X_NEW_DATA_MSK)) {
        if (temperature) *temperature = data.temperature;
        if (humidity)    *humidity    = data.humidity;
        if (pressure)    *pressure    = data.pressure / 100.0f; // Pa → hPa
        
        // Nu kollar vi om gas-datan är giltig
        if (gas) {
            if (data.status & BME68X_GASM_VALID_MSK) {
                *gas = (float)data.gas_resistance;
            } else {
                // Om mätningen misslyckades (t.ex. för kort väntetid), behåll gammalt eller 0
                // Men eftersom vi väntade rätt tid ovan, borde detta funka!
                *gas = 0.0f; 
            }
        }
        return true;
    }

    return false;
} 
