#include "tv_config.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    char normalized[TV_ORIGIN_CAP];
    assert(tv_origin_from_input("TV-LAN.local", 10745, NULL, normalized));
    assert(strcmp(normalized, "http://tv-lan.local:10745") == 0);
    assert(tv_origin_from_input("https://TV-LAN.local:443", 10745, NULL, normalized));
    assert(strcmp(normalized, "https://tv-lan.local") == 0);
    assert(!tv_origin_from_input("https://tv-lan.local/portal", 10745, NULL, normalized));
    assert(!tv_origin_from_input("tv-lan.local:70000", 10745, NULL, normalized));
    assert(!tv_origin_from_input("user@tv-lan.local", 10745, NULL, normalized));

    tv_connection_t accepted = {.ssid = "Test AP", .password = "8charsok",
                                .host = "127.0.0.1", .port = 10745,
                                .token = "p1.021122334455.0123456789abcdef"};
    assert(tv_connection_valid(&accepted));
    accepted.port = 0;
    assert(!tv_connection_valid(&accepted));
    accepted.port = 10745;
    accepted.token[2] = ' ';
    assert(!tv_connection_valid(&accepted));
    puts("Original firmware service validation and origin normalization passed.");
}
