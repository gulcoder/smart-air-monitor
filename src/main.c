#include "lwip/apps/sntp.h"
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/i2c.h"
#include "wifi.h"
#include "bme680.h"
#include "mqtt_client.h"
#include "config.h"
#include "lwip/dns.h"
#include "datetime.h"

// I2C-pinnar
#define SDA_PIN 4
#define SCL_PIN 5



// --- STATUS ENUM ---
typedef enum {
    NET_OK,
    NET_ERR_WIFI_DOWN,
    NET_ERR_DNS_FAILED
} NetStatus;

// --- 1. SCAN RESULT CALLBACK (M√•ste ligga f√∂re main) ---
static int scan_result(void *env, const cyw43_ev_scan_result_t *result) {
    if (result) {
        printf("HITTADE: '%s' (Auth: %d) RSSI: %d\n", result->ssid, result->auth_mode, result->rssi);
    }
    return 0;
}

// --- 2. CHECK STATUS FUNCTION (M√•ste ligga f√∂re main) ---
NetStatus check_wifi_and_dns(const char* hostname) {
    // Kolla l√§nkstatus
    int link_status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
    if (link_status != CYW43_LINK_UP) {
        return NET_ERR_WIFI_DOWN;
    }

    // Kolla DNS
    ip_addr_t ip;
    err_t err = dns_gethostbyname(hostname, &ip, NULL, NULL);
    
    if (err == ERR_OK || err == ERR_INPROGRESS) {
        return NET_OK;
    } else {
        return NET_ERR_DNS_FAILED;
    }
}

void print_pico_time() {
    time_t now;
    time(&now);
    struct tm *t = localtime(&now);
    printf("\n========================================\n");
    printf("PICO TID JUST NU: %04d-%02d-%02d %02d:%02d:%02d\n",
           t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
           t->tm_hour, t->tm_min, t->tm_sec);
    printf("========================================\n\n");
}

// --- 3. MAIN FUNCTION ---
int main() {
    stdio_init_all();

    printf("System booting... Waiting 5s...\n");
    sleep_ms(5000);
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("--- Program Start ---\n");

    // Initiera Wi-Fi-chippet
    if (cyw43_arch_init()) {
        printf("Wi-Fi init failed! Halting.\n");
        return 1;
    }
    cyw43_arch_enable_sta_mode();

    // --- K√ñR WIFI SCANNER ---
    printf("\n--- Startar Wi-Fi Scan (5 sekunder) ---\n");
    cyw43_wifi_scan_options_t scan_options = {0};
    cyw43_wifi_scan(&cyw43_state, &scan_options, NULL, scan_result);
    sleep_ms(5000);
    printf("--- Scan klar ---\n\n");

    // Skriv ut MAC
    uint8_t mac[6];
    cyw43_wifi_get_mac(&cyw43_state, CYW43_ITF_STA, mac);
    printf("MIN MAC-ADRESS: %02x:%02x:%02x:%02x:%02x:%02x\n\n", 
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // St√§ng av str√∂mspar-l√§ge
    cyw43_wifi_pm(&cyw43_state, cyw43_pm_value(CYW43_NO_POWERSAVE_MODE, 20, 1, 1, 1));

    // Anslut till N√§tverk
    printf("Connecting to Wi-Fi SSID: %s...\n", WIFI_SSID);
    int result = cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, NULL, CYW43_AUTH_OPEN, 30000);
    
    if (result != 0) {
        printf("Failed to connect to Wi-Fi (Error: %d). Stoppar h√§r.\n", result);
        while(true) { sleep_ms(1000); } 
    }

    uint8_t *ip = (uint8_t *)&cyw43_state.netif[0].ip_addr.addr;
    printf("Connected! IP: %d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);

    printf("Initierar tidsmodul...\n");
    datetime_init();

    int wait = 0;
    while(!datetime_is_synced() && wait < 10){
	    printf(".");
	    sleep_ms(1000);
	    wait++;
    }

    if(datetime_is_synced()){
	    printf("n[TID] Synkad via NTP!\n");
    }else {
	    printf("\n[TID] NTP Timeout. S√§tter manuell tid (2025-11-24).\n");
	    datetime_set_manual(2025, 11, 24, 12, 0, 0);
    }

    // --- KOLLA STATUS & STARTA MQTT ---
    printf("Checking Network Status via Switch...\n");
    NetStatus status = check_wifi_and_dns("mqtt.stockholm.se");

    switch (status) {
        case NET_OK:
            printf("[STATUS] N√§tverk & DNS OK. F√∂rs√∂ker starta MQTT...\n");
            if (mqtt_init()) {
                printf("MQTT Initialized & Connected!\n");
            } else {
                printf("MQTT Init Failed (TLS Error).\n");
            }
            break;

        case NET_ERR_WIFI_DOWN:
            printf("[STATUS FEL] Wi-Fi l√§nk nere!\n");
            break;

        case NET_ERR_DNS_FAILED:
            printf("[STATUS FEL] DNS misslyckades! (Ingen internet?)\n");
            break;
            
        default:
            printf("[STATUS] Ok√§nt fel.\n");
            break;
    }

    // Initiera Sensor
    i2c_init(i2c0, 100 * 1000);
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_PIN);
    gpio_pull_up(SCL_PIN);

    printf("Initializing BME680...\n");
    bool sensor_ok = bme680_init(i2c0, SDA_PIN, SCL_PIN);
    if (!sensor_ok) printf("VARNING: BME680 hittades inte.\n");

    const uint32_t WARMUP_TIME_MS = 30 * 60 * 1000;
    uint32_t start_time = to_ms_since_boot(get_absolute_time());
    bool sending_activate = false;

    printf("Startar m√tning. Data skickas till Yggio om %d minuter.\n", 30);


    // Huvudloop
    while (1) {

        float temp = 0, hum = 0, pres = 0, gas = 0;

        if (sensor_ok) {
            bme680_read(&temp, &hum, &pres, &gas);
	    printf("SENSOR: Temp: %.2f C, Hum: %.2f %%, Pres: %.0f hPa, Gas: %.0f Ohm\n", temp, hum, pres, gas);
        } else {
            printf("SIMULERING: Skapar fejk-data...\n");
            temp = 20.5f; hum = 50.0f; pres = 1013.0f; gas = 1000.0f;
        }

	if (!sending_activate){
		uint32_t current_time = to_ms_since_boot(get_absolute_time());

		if ((current_time - start_time) > WARMUP_TIME_MS){
			sending_activate = true;
			printf("\n--- 30 minuter har passerat! Skickar data till Yggio nu. ---\n");
		} else{
			uint32_t elapsed = current_time - start_time;
			uint32_t remaining = WARMUP_TIME_MS - elapsed;
			printf("...Skickat data om ca %d sekunder.\n", remaining/1000);
		}
	}

	if (sending_activate){
		char payload[256];
        	snprintf(payload, sizeof(payload), 
				"{\"connected\": true, \"temperature\": %.2f, \"humidity\":%.2f, \"pressure\":%.2f, \"gas\":%.2f}", 
				temp, hum, pres, gas);

        	printf("Sending MQTT: %s\n", payload);
        	if(mqtt_publish(MQTT_TOPIC, payload)) {
             		printf(">> Publicering OK!\n");
        	} else {
             		printf(">> Publicering misslyckades.\n");
	    		printf(">> F√rs√ker √teransluta..\n");

	     		if (mqtt_init()){
				printf(">> √•teransluten f√rs√ker skicka igen..\n");

		     		if (mqtt_publish(MQTT_TOPIC, payload)){
			     		printf(">> Publicering OK (efter reconnect)!\n");
				}
			} else {
		     		printf(">> Kunde inte √teransluta just nu. F√rs√ker n√sta varv.\n");
			}
		}
	}

        mqtt_loop(); 
        printf("Waiting 5s...\n\n");
        sleep_ms(5000);
    }

    return 0;
}
