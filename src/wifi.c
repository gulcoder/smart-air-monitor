#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "wifi.h"

bool connect_to_wifi(const char* ssid, const char* password) {
    // Initiera Wi-Fi-drivrutinen
    if (cyw43_arch_init()) {
        printf("Wi-Fi init failed!\n");
        return false;
    }

    cyw43_arch_enable_sta_mode();
    printf("Wi-Fi initialized, trying to connect...\n");

    // Försök ansluta med timeout 30s
    if (cyw43_arch_wifi_connect_timeout_ms(ssid, password, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("Wi-Fi connect failed!\n");
        return false;
    }

    printf("Wi-Fi connected!\n");

    // Optional: print IP
    uint8_t *ip = (uint8_t*)&cyw43_state.netif[0].ip_addr.addr;
    printf("IP address: %d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);

    return true;
}

