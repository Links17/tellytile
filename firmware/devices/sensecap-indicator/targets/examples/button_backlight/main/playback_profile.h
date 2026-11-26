#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "cJSON.h"

enum { TV_SMOOTH, TV_STANDARD, TV_ULTRA, TV_PROFILE_COUNT };
typedef struct {
    const char *id;
    const char *label;
    unsigned target_fps;
} tv_profile_t;

extern const tv_profile_t tv_profiles[TV_PROFILE_COUNT];
unsigned tv_profile_restore(unsigned saved);
static inline bool tv_profile_changes(unsigned current, unsigned requested, bool available)
{
    return available && requested < TV_PROFILE_COUNT && requested != current;
}
bool tv_session_config_valid(const cJSON *config, uint32_t session, unsigned profile,
                             bool *supports_profiles);
