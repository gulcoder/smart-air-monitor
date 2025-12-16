#ifndef DATETIME_H
#define DATETIME_H

#include <time.h>
#include <stdbool.h>

// Initiera och starta NTP-tidssynk
void datetime_init(void);

// Hjälpfunktion: Kollar om tiden har blivit synkad än
bool datetime_is_synced(void);

// Hämtar aktuell tid (med din +2h justering)
struct tm* datetime_get_time(void);

// Manuell "nöd-funktion" om NTP blockeras av brandväggen
void datetime_set_manual(int year, int mon, int day, int hour, int min, int sec);

#endif
