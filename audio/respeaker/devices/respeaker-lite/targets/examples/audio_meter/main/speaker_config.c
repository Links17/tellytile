#include "speaker_config.h"

#include <string.h>

bool speaker_config_decode(const uint8_t *record, size_t size, speaker_config_t *out)
{
    if (size < 12 || size > SPEAKER_RECORD_MAX || memcmp(record, "TVS1", 4)) return false;
    const size_t ssid_size = record[4], password_size = record[5];
    const uint8_t *sender = record + 6;
    const unsigned port = (unsigned)record[10] << 8 | record[11];
    if (!ssid_size || ssid_size > 32 || password_size > 63 ||
        (password_size && password_size < 8) || size != 12 + ssid_size + password_size ||
        !port || !sender[0] || sender[0] == 127 || sender[0] >= 224 ||
        memchr(record + 12, 0, ssid_size + password_size)) return false;

    speaker_config_t next = {.port = port};
    memcpy(next.ssid, record + 12, ssid_size);
    memcpy(next.password, record + 12 + ssid_size, password_size);
    memcpy(next.sender, sender, sizeof next.sender);
    *out = next;
    return true;
}

static int hex(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

size_t speaker_config_command(const char *line, size_t length, uint8_t record[SPEAKER_RECORD_MAX])
{
    const char prefix[] = "TVSET1 ";
    const size_t offset = sizeof prefix - 1;
    if (length < offset || length > SPEAKER_LINE_MAX || (length - offset) % 2 ||
        memcmp(line, prefix, offset)) return 0;
    const size_t size = (length - offset) / 2;
    for (size_t i = 0; i < size; ++i) {
        int high = hex(line[offset + i * 2]), low = hex(line[offset + i * 2 + 1]);
        if (high < 0 || low < 0) return 0;
        record[i] = (uint8_t)(high << 4 | low);
    }
    speaker_config_t config;
    bool valid = speaker_config_decode(record, size, &config);
    memset(&config, 0, sizeof config);
    return valid ? size : 0;
}
