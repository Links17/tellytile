#pragma once

#include "speaker_config.h"
#include "esp_err.h"

bool speaker_setup_load(speaker_config_t *config);
esp_err_t speaker_setup_start(void);
