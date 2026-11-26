#include "playback_profile.h"
#include "av_protocol.h"

#include <string.h>

const tv_profile_t tv_profiles[TV_PROFILE_COUNT] = {
    [TV_SMOOTH] = {"smooth", "流畅", 8},
    [TV_STANDARD] = {"standard", "标清", 15},
    [TV_ULTRA] = {"ultra", "超清", 30},
};

unsigned tv_profile_restore(unsigned saved)
{
    return saved < TV_PROFILE_COUNT ? saved : TV_ULTRA;
}

static bool number_is(const cJSON *o, const char *key, double value)
{
    const cJSON *n = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsNumber(n) && n->valuedouble == value;
}

static bool string_is(const cJSON *value, const char *expected)
{
    return cJSON_IsString(value) && !strcmp(value->valuestring, expected);
}

bool tv_session_config_valid(const cJSON *config, uint32_t session, unsigned profile,
                             bool *supports_profiles)
{
    if (!cJSON_IsObject(config) || profile >= TV_PROFILE_COUNT) return false;
    const cJSON *fps = cJSON_GetObjectItemCaseSensitive(config, "fps");
    const cJSON *quality = cJSON_GetObjectItemCaseSensitive(config, "quality");
    const cJSON *delay = cJSON_GetObjectItemCaseSensitive(config, "start_delay_ms");
    if (!number_is(config, "session", session) ||
        !number_is(config, "width", AV_VIDEO_WIDTH) || !number_is(config, "height", AV_VIDEO_HEIGHT) ||
        !number_is(config, "stripe_rows", AV_VIDEO_STRIPE_ROWS) ||
        !number_is(config, "video_max_bytes", AV_VIDEO_MAX) ||
        !number_is(config, "sample_rate", 16000) || !number_is(config, "sample_bits", 16) ||
        !number_is(config, "channels", 1) || !number_is(config, "audio_chunk_ms", AV_AUDIO_MS) ||
        !string_is(cJSON_GetObjectItemCaseSensitive(config, "audio_codec"), "ima_adpcm") ||
        !cJSON_IsNumber(fps) || fps->valueint < 1 || fps->valueint > 30 || fps->valuedouble != fps->valueint ||
        (quality && !string_is(quality, tv_profiles[profile].id)) ||
        (delay && !number_is(config, "start_delay_ms", 200))) return false;
    *supports_profiles = quality != NULL;
    return true;
}
