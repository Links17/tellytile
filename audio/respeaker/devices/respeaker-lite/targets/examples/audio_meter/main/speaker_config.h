#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { SPEAKER_RECORD_MAX = 12 + 32 + 63, SPEAKER_LINE_MAX = 7 + 2 * SPEAKER_RECORD_MAX };

typedef struct {
    char ssid[33];
    char password[64];
    uint8_t sender[4];
    uint16_t port;
} speaker_config_t;

bool speaker_config_decode(const uint8_t *record, size_t size, speaker_config_t *out);
size_t speaker_config_command(const char *line, size_t length, uint8_t record[SPEAKER_RECORD_MAX]);
