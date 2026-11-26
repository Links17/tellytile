#pragma once

#include "tv_config.h"

typedef struct {
    tv_connection_t tv;
    char origin[TV_ORIGIN_CAP];
    char code[5];
    char audio_peer[16];
    uint16_t audio_port;
} portal_config_t;

enum { PORTAL_SCAN_RESULTS = 24 };
typedef struct {
    char ssid[33];
    int rssi;
    bool open;
} portal_network_t;

bool portal_config_valid(const portal_config_t *config);
/* The body is NUL-terminated at length. NULL secret fields retain matching credentials. */
bool portal_config_parse(const char *body, size_t length, const portal_config_t *saved, portal_config_t *out);
char *portal_config_public_json(const portal_config_t *saved);
char *portal_scan_json(const portal_network_t *networks, size_t count);
bool portal_config_read(portal_config_t *config);
bool portal_consume_pairing_code(const char *origin, const char *code);
void portal_serve(void (*display)(const char *network));
