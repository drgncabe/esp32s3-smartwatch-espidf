#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "nvs_fs";

void init_nvs_fs(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

esp_err_t nvs_delete_keys_with_prefix(const char *nspace, const char *prefix)
{
    nvs_handle_t handle;
    esp_err_t err1 = nvs_open(nspace, NVS_READWRITE, &handle);
    if (err1 != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS handle: %s", esp_err_to_name(err1));
        return err1;
    }

    nvs_iterator_t it = NULL;
    esp_err_t err;
    size_t prefix_len = strlen(prefix);

    err = nvs_entry_find("nvs", NULL, NVS_TYPE_ANY, &it);
    while (err == ESP_OK && it != NULL)
    {

        nvs_entry_info_t info;
        nvs_entry_info(it, &info);

        ESP_LOGI(TAG, "Key: %s", info.key);
        if (strncmp(info.key, prefix, prefix_len) == 0)
        {
            nvs_erase_key(handle, info.key);
        }

        err = nvs_entry_next(&it);
    }

    if (it)
    {
        nvs_release_iterator(it);
    }

    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error committing NVS changes: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }
    nvs_close(handle);
    return ESP_OK;
}