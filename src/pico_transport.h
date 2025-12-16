#ifndef PICO_TRANSPORT_H
#define PICO_TRANSPORT_H

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

// 1. Definiera Timer (Detta saknades och orsakade felet!)
typedef struct Timer {
    uint64_t end_time;
} Timer;

// 2. Definiera Network
typedef struct Network
{
    int my_socket;
    int (*mqttread) (struct Network*, unsigned char*, int, int);
    int (*mqttwrite) (struct Network*, unsigned char*, int, int);
    void (*disconnect) (struct Network*);
} Network;

// 3. Funktionsprototyper som Paho behöver
void TLSConnect(Network* n, char* hostname, int port, const char* ca_cert, const char* client_cert, const char* client_key);

void TimerInit(Timer*);
char TimerIsExpired(Timer*);
void TimerCountdownMS(Timer*, unsigned int);
void TimerCountdown(Timer*, unsigned int);
int TimerLeftMS(Timer*);

#endif
