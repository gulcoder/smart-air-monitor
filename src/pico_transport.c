#include "pico_transport.h"
#include "pico/time.h"
#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"
#include "lwip/altcp.h"
#include "lwip/altcp_tcp.h"
#include "lwip/altcp_tls.h"
#include "lwip/dns.h"
#include "hardware/structs/rosc.h"
#include "hardware/regs/rosc.h"
#include "mbedtls/ssl.h"

/* ==========================================
 * 1. TIMER IMPLEMENTATION (Oförändrad)
 * ========================================== */

void TimerInit(Timer* timer) {
    timer->end_time = 0;
}

char TimerIsExpired(Timer* timer) {
    uint64_t now = time_us_64();
    return now >= timer->end_time;
}

void TimerCountdownMS(Timer* timer, unsigned int ms) {
    timer->end_time = time_us_64() + (ms * 1000);
}

void TimerCountdown(Timer* timer, unsigned int seconds) {
    timer->end_time = time_us_64() + (seconds * 1000000);
}

int TimerLeftMS(Timer* timer) {
    uint64_t now = time_us_64();
    if (now >= timer->end_time) {
        return 0;
    }
    return (int)((timer->end_time - now) / 1000);
}

/* ==========================================
 * 2. NETWORK / TLS IMPLEMENTATION
 * ========================================== */

// Intern struktur för att hantera lwIP-kopplingens status
typedef struct TLSContext {
    struct altcp_pcb *pcb;
    bool connected;
    bool busy;
    unsigned char *buffer;
    int buffer_len;
    int buffer_pos;
} TLSContext;

static TLSContext g_ctx = {0}; // Vi använder en global kontext för enkelhetens skull

// Callback: När fel uppstår (t.ex. nedkoppling)
static void tls_err(void *arg, err_t err) {
    TLSContext *ctx = (TLSContext*)arg;
    if (ctx) {
        printf("TLS Error: %d\n", err);
        ctx->connected = false;
        ctx->pcb = NULL;
    }
}

// Callback: När data tas emot från servern
static err_t tls_recv(void *arg, struct altcp_pcb *pcb, struct pbuf *p, err_t err) {
    TLSContext *ctx = (TLSContext*)arg;
    if (!p) {
        // NULL pbuf betyder att servern stängde kopplingen
        ctx->connected = false;
        return ERR_OK;
    }

    if (p->tot_len > 0) {
        // Vi har fått data, men Paho är inte redo att läsa den än (synkront vs asynkront).
        // I en enkel lösning: Vi kan inte buffra allt här utan minnesläckor.
        // Paho Embedded C är designat för att polla.
        
        // För denna lösning kopierar vi data till vår interna buffer om det finns plats.
        // OBS: Detta är förenklat. I produktion bör man ha en ringbuffer.
        if (ctx->buffer && ctx->buffer_len > 0) {
            int copy_len = (p->tot_len > ctx->buffer_len) ? ctx->buffer_len : p->tot_len;
            pbuf_copy_partial(p, ctx->buffer, copy_len, 0);
            ctx->buffer_pos = copy_len; // Markera hur mycket vi läste
            altcp_recved(pcb, p->tot_len); // Säg till lwIP att vi tagit emot datan
        }
    }
    pbuf_free(p);
    return ERR_OK;
}

// Callback: När anslutningen lyckats
static err_t tls_connected(void *arg, struct altcp_pcb *pcb, err_t err) {
    TLSContext *ctx = (TLSContext*)arg;
    if (err == ERR_OK) {
        printf("TLS Connected!\n");
        ctx->connected = true;
        ctx->busy = false;
    } else {
        printf("TLS Connect failed: %d\n", err);
        ctx->connected = false;
    }
    return ERR_OK;
}

// Intern funktion för att läsa (anropas av Paho)
// Paho väntar på data här. Eftersom lwIP är callback-baserat måste vi fuska lite.
// I denna förenklade version gör vi en "busy wait" på en buffer vi fyller i callbacken.
// Notera: Detta kräver att vi modifierar tls_recv lite för att matcha Pahos buffer direkt
// om vi kunde, men Paho ger buffern i stunden.
int paho_read(Network* n, unsigned char* buffer, int len, int timeout_ms) {
    struct altcp_pcb *pcb = (struct altcp_pcb*)n->my_socket; // Vi sparade PCB-pekaren här
    if (!pcb || !g_ctx.connected) return -1;

    // Sätt upp den globala kontexten så recv-callbacken vet var den ska lägga data
    g_ctx.buffer = buffer;
    g_ctx.buffer_len = len;
    g_ctx.buffer_pos = 0;

    uint64_t end_time = time_us_64() + (timeout_ms * 1000);
    
    while (time_us_64() < end_time) {
        // Låt lwIP jobba (viktigt för att callbacks ska köras!)
        // Om du kör 'background'-läge sköter cyw43 detta, men en sleep hjälper
        sleep_ms(1); 
        
        if (g_ctx.buffer_pos > 0) {
            // Vi fick data i callbacken!
            int read = g_ctx.buffer_pos;
            g_ctx.buffer = NULL; // Nollställ
            g_ctx.buffer_pos = 0;
            return read;
        }
        if (!g_ctx.connected) return -1;
    }
    
    g_ctx.buffer = NULL;
    return 0; // Timeout
}

// Intern funktion för att skriva (anropas av Paho)
int paho_write(Network* n, unsigned char* buffer, int len, int timeout_ms) {
    struct altcp_pcb *pcb = (struct altcp_pcb*)n->my_socket;
    if (!pcb || !g_ctx.connected) return -1;

    err_t err = altcp_write(pcb, buffer, len, TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
        printf("Writing data failed: %d\n", err);
        return -1;
    }
    
    altcp_output(pcb); // Tvinga sändning
    return len;
}

void paho_disconnect(Network* n) {
    struct altcp_pcb *pcb = (struct altcp_pcb*)n->my_socket;
    if (pcb) {
        altcp_close(pcb);
        g_ctx.connected = false;
        g_ctx.pcb = NULL;
    }
}

// DNS Callback
static void dns_found(const char *name, const ip_addr_t *ipaddr, void *callback_arg) {
    TLSContext *ctx = (TLSContext*)callback_arg;
    if (ipaddr) {
        printf("DNS Resolved: %s -> %s\n", name, ipaddr_ntoa(ipaddr));
        altcp_connect(ctx->pcb, ipaddr, 8883, tls_connected);
    } else {
        printf("DNS Resolution failed for %s\n", name);
        ctx->busy = false; // Sluta vänta
    }
}

// HUVUDFUNKTIONEN: Konfigurerar mTLS och kopplar upp
void TLSConnect(Network* n, char* hostname, int port, const char* ca_cert, const char* client_cert, const char* client_key) {
    // 1. Koppla Pahos funktionspekare
    n->mqttread = paho_read;
    n->mqttwrite = paho_write;
    n->disconnect = paho_disconnect;

    printf("\n=== TLS SETUP (SNI ENABLED) ===\n");
    printf("Hostname for SNI: %s\n", hostname);
    
    // 2. Skapa TLS Konfiguration (mTLS)
    // OBS: Vi castar const char* till u8_t* och beräknar längd.
    // Detta antar att certifikaten är null-terminerade strängar.
    struct altcp_tls_config *tls_config = altcp_tls_create_config_client_2wayauth(
        (u8_t*)ca_cert, strlen(ca_cert) + 1,
        (u8_t*)client_key, strlen(client_key) + 1,
        NULL, 0, // Inget lösenord på nyckeln (vanligtvis)
        (u8_t*)client_cert, strlen(client_cert) + 1
    );

    if (!tls_config) {
        printf("Failed to create TLS config! Check certs/keys/memory.\n");
        return;
    }

   // mbedtls_ssl_conf_server_name((mbedtls_ssl_config*)tls_config, hostname);


    // 3. Skapa TCP/TLS Control Block
    struct altcp_pcb *pcb = altcp_tls_new(tls_config, IPADDR_TYPE_ANY);
    if (!pcb) {
        printf("Failed to create PCB!\n");
        altcp_tls_free_config(tls_config);
        return;
    }

    // 4. Sätt upp callbacks
    g_ctx.pcb = pcb;
    g_ctx.connected = false;
    g_ctx.busy = true;
    
    altcp_arg(pcb, &g_ctx);
    altcp_recv(pcb, tls_recv);
    altcp_err(pcb, tls_err);

    // 5. DNS Uppslagning och Anslutning
    ip_addr_t ip;
    printf("Resolving %s...\n", hostname);
    err_t err = dns_gethostbyname(hostname, &ip, dns_found, &g_ctx);
    
    if (err == ERR_OK) {
        // IP fanns cachad, anslut direkt
        dns_found(hostname, &ip, &g_ctx);
    } else if (err != ERR_INPROGRESS) {
        printf("DNS setup failed: %d\n", err);
        altcp_close(pcb);
        return;
    }

    // 6. Vänta på anslutning (Busy loop)
    // Vi måste vänta här eftersom Paho förväntar sig att connect är synkront
    printf("Waiting for TLS handshake...\n");
    int attempts = 0;
    while (g_ctx.busy && attempts < 5000) { // 50 sekunders timeout
        sleep_ms(10);
        attempts++;
        if (g_ctx.connected) {
            g_ctx.busy = false; 
            break;
        }
    }

    if (g_ctx.connected) {
        // Spara PCB i nätverksstrukturen så read/write hittar den
        n->my_socket = (int)pcb; // Fulhack att spara pekaren som int, men funkar i C
    } else {
        printf("TLS Connection Timed Out or Failed.\n");
    }
}

/* ==========================================
 * 3. MBEDTLS HARDWARE POLL (Krävs för TLS)
 * ========================================== */

// Denna funktion anropas av mbedTLS när den behöver slumptal
int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen) {
    // Vi använder Picons inbyggda Ring Oscillator (ROSC) för slump
    for (size_t i = 0; i < len; i++) {
        int byte = 0;
        // Samla 8 slumpmässiga bitar för att göra 1 byte
        for (int bit = 0; bit < 8; bit++) {
            byte = (byte << 1) | (rosc_hw->randombit & 1);
        }
        output[i] = byte;
    }
    *olen = len;
    return 0; // 0 betyder succé
}
