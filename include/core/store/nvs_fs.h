#pragma once

#ifndef NVS_FS_H
#define NVS_FS_H

#include "esp_err.h"
#include "nvs.h"
#include "nvs_flash.h"

void init_nvs_fs(void);
esp_err_t nvs_delete_keys_with_prefix(const char *nspace, const char *prefix);

#endif
