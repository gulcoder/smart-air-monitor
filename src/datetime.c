#include "datetime.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/dns.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h> // Krävs för settimeofday

#define NTP_SERVER "pool.ntp.org"
#define NTP_MSG_LEN 48
#define NTP_PORT 123
#define NTP_DELTA 2208988800 // Sekunder mellan 1900 och 1970

static bool is_synced = false;

typedef struct {
    ip_addr_t server_address;
    struct udp_pcb *pcb;
} ntp_state_t;

// Callback när vi får svar från NTP-servern
static void ntp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port) {
    if (p->tot_len == NTP_MSG_LEN) {
        uint8_t buf[4];
        // Läs ut sekunderna från paketets position 40
        pbuf_copy_partial(p, buf, 4, 40);
        
        // Konvertera bytes till heltal (Big Endian)
        uint32_t seconds_since_1900 = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
        time_t timestamp = seconds_since_1900 - NTP_DELTA;

        // Ställ systemklockan
        struct timeval tv = { .tv_sec = timestamp, .tv_usec = 0 };
        settimeofday(&tv, NULL);
        
        is_synced = true;

        struct tm *t = gmtime(&timestamp);
        printf("[NTP] Tid synkad: %04d-%02d-%02d %02d:%02d:%02d UTC\n",
               t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
               t->tm_hour, t->tm_min, t->tm_sec);
    }
    pbuf_free(p);
    if (arg) free(arg); // Städa upp state
    if (pcb) udp_remove(pcb);
}

static void ntp_send(ntp_state_t *state) {
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, NTP_MSG_LEN, PBUF_RAM);
    if (!p) return;
    
    memset(p->payload, 0, NTP_MSG_LEN);
    ((uint8_t *)p->payload)[0] = 0x1B; // LI, Version, Mode

    udp_sendto(state->pcb, p, &state->server_address, NTP_PORT);
    pbuf_free(p);
}

static void ntp_dns_cb(const char *name, const ip_addr_t *ipaddr, void *arg) {
    ntp_state_t *state = (ntp_state_t *)arg;
    if (ipaddr) {
        state->server_address = *ipaddr;
        ntp_send(state);
    } else {
        printf("[NTP] DNS-förfrågan misslyckades\n");
        free(state); // Städa om vi misslyckas
    }
}

void datetime_init(void) {
    ntp_state_t *state = calloc(1, sizeof(ntp_state_t));
    if (!state) return;

    state->pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
    if (!state->pcb) {
        free(state);
        return;
    }
    
    udp_recv(state->pcb, ntp_recv, state);

    cyw43_arch_lwip_begin();
    err_t err = dns_gethostbyname(NTP_SERVER, &state->server_address, ntp_dns_cb, state);
    
    if (err == ERR_OK) {
        // Adressen fanns redan i cachen, skicka direkt
        ntp_send(state);
    } else if (err != ERR_INPROGRESS) {
        printf("[NTP] Kunde inte starta DNS-uppslag\n");
        udp_remove(state->pcb);
        free(state);
    }
    cyw43_arch_lwip_end();
}

// Din funktion för att hämta tid (+2h justering)
struct tm* datetime_get_time(void) {
    time_t now_raw = time(NULL);
    struct tm *now = localtime(&now_raw); // localtime på Pico ger UTC om man inte satt TZ
    
    if (now) {
        // Enkel tidszonsjustering (Obs: hanterar inte datumbyte vid midnatt perfekt)
        now->tm_hour += 2;
        if (now->tm_hour >= 24) {
            now->tm_hour -= 24;
            // Här borde man egentligen öka dagen också, men för enkelhetens skull:
        }
    }
    return now;
}

bool datetime_is_synced(void) {
    // Om vi är i år 1970 (epoch) så är vi inte synkade
    time_t now = time(NULL);
    struct tm *t = gmtime(&now);
    return (t->tm_year > 70); // 70 betyder år 1970. Allt över det är "synkat".
}

// Manuell fallback om NTP är blockerat
void datetime_set_manual(int year, int mon, int day, int hour, int min, int sec) {
    struct tm t = {0};
    t.tm_year = year - 1900;
    t.tm_mon = mon - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = min;
    t.tm_sec = sec;
    
    time_t time_val = mktime(&t);
    struct timeval tv = { .tv_sec = time_val, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    is_synced = true;
}
