#include "pico_transport.h"
#include "MQTTPacket.h"
#include "MQTTClient.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/mutex.h"
#include <string.h>
#include "mqtt_client.h"
#include "config.h"

// --- Certifikat och key ---
static const char client_cert[] = 
"-----BEGIN CERTIFICATE-----\n"
"KLISTRA_IN_DITT_KLIENT_CERTIFIKAT_HAR\n"
"-----END CERTIFICATE-----\n";
static const char client_key[]  = 
"-----BEGIN PRIVATE KEY-----\n"
"KLISTRA_IN_DIN_PRIVATA_KLIENT_NYCKEL_HAR\n"
"-----END PRIVATE KEY-----\n";
static const char ca_cert[]     =
"-----BEGIN CERTIFICATE-----\n"
"KLISTRA_IN_DIN_ROOT_CA_HAR\n"
"-----END CERTIFICATE-----\n";


static MQTTClient client;
static Network network; // VIKTIGT: Måste vara static/global så den lever kvar efter init!
static unsigned char sendbuf[2048]; // Buffertar för MQTT (öka om du skickar stor data)
static unsigned char readbuf[2048];

bool mqtt_publish(const char* topic, const char* payload);

// ==========================================
// INITIERING
// ==========================================

bool mqtt_init() {
    // Notera: Vi initierar INTE Wi-Fi här. Det görs i main.c.
    // Vi antar att nätverket redan är uppe.
    printf("\n=== RÖNTGEN-CHECK AV NYCKEL ===\n");
    int len = strlen(client_key);

    // Vi tittar på de sista 8 tecknen
    printf("Totallängd: %d\n", len);
    printf("De sista tecknen (ASCII-koder):\n");

    for (int i = len - 8; i <= len; i++) {
        char c = client_key[i];
        if (c == '\0') printf("Position %d: [NULL] (Slutet)\n", i);
        else if (c == '\n') printf("Position %d: [10] (Radbrytning - RÄTT!)\n", i);
        else if (c == '\r') printf("Position %d: [13] (Carriage Return - FEL!)\n", i);
        else printf("Position %d: [%d] '%c'\n", i, c, c);
    }
    printf("===============================\n");

    printf("Setting up MQTT connection to %s:%d...\n", MQTT_BROKER_HOST, MQTT_BROKER_PORT);


    // 1. Starta TLS-koppling (Använder certifikaten ovan)
    TLSConnect(&network, MQTT_BROKER_HOST, MQTT_BROKER_PORT, ca_cert, client_cert, client_key);
    
    // 2. Initiera Paho MQTT-klienten
    MQTTClientInit(&client, &network, 30000, sendbuf, sizeof(sendbuf), readbuf, sizeof(readbuf));

    // 3. Konfigurera inloggningsdata
    MQTTPacket_connectData connectData = MQTTPacket_connectData_initializer;
    connectData.MQTTVersion = 4;
    connectData.clientID.cstring = MQTT_CLIENT_ID;
    connectData.keepAliveInterval = 60; 
    connectData.cleansession = 1;

    connectData.willFlag = 1;
    connectData.will.topicName.cstring = MQTT_STATUS_TOPIC;
    connectData.will.message.cstring = "{\"connected\": false";
    connectData.will.qos = 1;
    connectData.will.retained = 0;

    // 4. Anslut till mäklaren (Broker)
    printf("Sending MQTT Connect packet...\n");
    int rc = MQTTConnect(&client, &connectData);
    
    if (rc != 0) {
        printf("MQTT connection failed with return code: %d\n", rc);
        return false;
    }

    //mqtt_publish(MQTT_TOPIC, "");


    if(!mqtt_publish(MQTT_STATUS_TOPIC,"{\"connected\": true}")){
	    printf("VARNING: Kunde inte skicka true-statusmeddelande. \n");
    }

    printf("MQTT connected successfully!\n");
    return true;
}

// ==========================================
// PUBLICERA
// ==========================================

bool mqtt_publish(const char* topic, const char* payload) {
    MQTTMessage message;
    memset(&message, 0, sizeof(message));
    
    message.qos = QOS0;
    message.retained = 0;
    message.payload = (void*)payload;
    message.payloadlen = strlen(payload);

    int rc = MQTTPublish(&client, topic, &message);
    
    if (rc != 0) {
        printf("Failed to publish, return code: %d\n", rc);
        return false;
    }
    return true;
}

// ==========================================
// LOOP (Håll vid liv)
// ==========================================

void mqtt_loop() {
    // Denna måste anropas regelbundet i main-loopen
    // för att skicka "ping" till servern och ta emot data.
    if (client.isconnected) {
        MQTTYield(&client, 100); // Vänta max 100ms på trafik
    }
}
