// Standard C
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>

// FreeRTOS / ESP-IDF
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_err.h"

#include "esp_wifi.h"
#include "esp_netif.h"

#include "esp_spiffs.h"
#include "esp_timer.h"

#include "esp_http_client.h"
#include "esp_tls.h"

#define WIFI_SSID  "XYZ"
#define WIFI_PASS  "12345678"

// Keep learningcontainer.com to match the embedded Google CA chain
#define DOWNLOAD_URL "https://speed.hetzner.de/1MB.bin"    /// save and build // sorry 
#define SPIFFS_OUTPUT_FILE "/spiffs/download_file.bin"

#define TARGET_SPEED_KBPS 400
#define CHUNK (16 * 1024)

static const char *TAG = "https_spiffs_dl";

extern const char server_cert_pem_start[] asm("_binary_server_cert_pem_start");
extern const char server_cert_pem_end[]   asm("_binary_server_cert_pem_end");

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t wifi_init_sta(void) {
    s_wifi_event_group = xEventGroupCreate();

    // Minimal, robust init sequence
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) return err;

    if (esp_netif_get_handle_from_ifkey("WIFI_STA_DEF") == NULL) {
        esp_netif_create_default_wifi_sta();
    }

    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        &wifi_event_handler, NULL, NULL);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "",
            .password = "",
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    // Throughput-oriented tweaks (best-effort)
    esp_wifi_set_ps(WIFI_PS_NONE);
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
    esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT40);
    esp_wifi_set_max_tx_power(78);

    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished. Connecting to SSID:%s", WIFI_SSID);
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                                           pdFALSE, pdTRUE, pdMS_TO_TICKS(20000));
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to AP");
        return ESP_OK;
    }
    ESP_LOGE(TAG, "Failed to connect to AP within timeout");
    return ESP_FAIL;
}

static esp_err_t mount_spiffs(void) {
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "spiffs",
        .max_files = 5,
        .format_if_mount_failed = true,
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret == ESP_OK) {
        size_t total = 0, used = 0;
        esp_spiffs_info(NULL, &total, &used);
        ESP_LOGI(TAG, "SPIFFS mounted. total: %d, used: %d", (int)total, (int)used);
    }
    return ret;
}

static void download_task(void *pvParameters) {
    ESP_LOGI(TAG, "Preparing HTTPS for URL: %s", DOWNLOAD_URL);

    esp_http_client_config_t config = {
        .url = DOWNLOAD_URL,
        .keep_alive_enable = true,
        .timeout_ms = 20000,
        .buffer_size = (8 * 1024),
        .buffer_size_tx = (8 * 1024),
        .cert_pem = server_cert_pem_start,
        .cert_len = (server_cert_pem_end - server_cert_pem_start),
        .skip_cert_common_name_check = false,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        vTaskDelete(NULL);
        return;
    }
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        vTaskDelete(NULL);
        return;
    }

    FILE *f = fopen(SPIFFS_OUTPUT_FILE, "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file: %s", SPIFFS_OUTPUT_FILE);
        esp_http_client_cleanup(client);
        vTaskDelete(NULL);
        return;
    }
    setvbuf(f, NULL, _IOFBF, 16 * 1024);

    int content_length = esp_http_client_fetch_headers(client);
    if (content_length > 0) {
        ESP_LOGI(TAG, "Content-Length: %d", content_length);
    }

    size_t buf_size = CHUNK;
    char *buffer = malloc(buf_size);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate %d bytes", (int)buf_size);
        fclose(f);
        esp_http_client_cleanup(client);
        vTaskDelete(NULL);
        return;
    }

    int64_t t_start = esp_timer_get_time();
    ssize_t total_bytes = 0;
    while (1) {
        int r = esp_http_client_read(client, buffer, buf_size);
        if (r > 0) {
            size_t written = fwrite(buffer, 1, r, f);
            if (written != (size_t)r) {
                ESP_LOGE(TAG, "File write error: wrote %zu instead of %d", written, r);
                break;
            }
            total_bytes += written;
        } else if (r == 0) {
            ESP_LOGI(TAG, "Download complete");
            break;
        } else {
            ESP_LOGE(TAG, "Read error: %d", r);
            break;
        }
    }

    int64_t t_end = esp_timer_get_time();
    double elapsed_sec = (t_end - t_start) / 1000000.0;
    double kbps = (double)total_bytes / 1024.0 / elapsed_sec;
    ESP_LOGI(TAG, "Total bytes: %d", (int)total_bytes);
    ESP_LOGI(TAG, "Elapsed time: %.3f s", elapsed_sec);
    ESP_LOGI(TAG, "Throughput: %.2f KB/s", kbps);

    free(buffer);
    fclose(f);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    vTaskDelete(NULL);
}

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    if (mount_spiffs() != ESP_OK) {
        ESP_LOGW(TAG, "SPIFFS mount failed; continuing");
    }

    if (wifi_init_sta() != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed");
        return;
    }

    xTaskCreatePinnedToCore(download_task, "download_task", 12 * 1024, NULL, 10, NULL, 1);
}