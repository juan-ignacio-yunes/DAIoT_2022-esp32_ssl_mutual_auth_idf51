/* MQTT Mutual Authentication Example */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#include "mqtt_client.h"

#include "cJSON.h"

#define MUESTRA_X_SEGUNDOS 10

static const char *TAG = "ESP32-juan_yunes";

typedef struct {
    esp_mqtt_client_handle_t client;
} mqtt_task_params_t;

static void http_get_task(void *pvParameters)
{
    mqtt_task_params_t *task_params = (mqtt_task_params_t *)pvParameters;
    esp_mqtt_client_handle_t client = task_params->client;

    float temperature = 22.0;
    float humidity = 60.0;


    while(1) {
        
        temperature = roundf(temperature * 100) / 100;
        humidity = roundf(humidity * 100) / 100;

        ESP_LOGI(TAG, "dispositivoId: 10");
        ESP_LOGI(TAG, "nombre: ESP32 Juan Yunes");
        ESP_LOGI(TAG, "ubicacion: Living");
        ESP_LOGI(TAG, "temperature: %.2f °C", temperature);
        ESP_LOGI(TAG, "humedad: %.2f %%", humidity);
        ESP_LOGI(TAG, "MQTT_EVENT_ENVIAR");

        // Crear un objeto JSON
        cJSON *root = cJSON_CreateObject();
        cJSON_AddNumberToObject(root, "dispositivoId", 10);
        cJSON_AddStringToObject(root, "nombre", "ESP32 Juan Yunes");
        cJSON_AddStringToObject(root, "ubicacion", "Living");
        cJSON_AddNumberToObject(root, "temperatura", temperature);
        cJSON_AddNumberToObject(root, "humedad", humidity);

        // Convertir el objeto JSON a una cadena
        const char *json_string = cJSON_Print(root);

        // Publicar el JSON
        esp_mqtt_client_publish(client, "/daiot", json_string, 0, 1, 0);

        // Liberar la memoria utilizada por el objeto JSON
        cJSON_Delete(root);
        free((void*)json_string); // cJSON_Print uses malloc

        temperature += 0.1;
        humidity += 0.1;

        // Esperar el tiempo definido antes de enviar la siguiente muestra
        vTaskDelay(MUESTRA_X_SEGUNDOS * 1000 / portTICK_PERIOD_MS);
    } 
}

// Set your local broker URI
#define BROKER_URI "mqtts://192.168.100.24:8883"

extern const uint8_t client_cert_pem_start[] asm("_binary_client_crt_start");
extern const uint8_t client_cert_pem_end[] asm("_binary_client_crt_end");
extern const uint8_t client_key_pem_start[] asm("_binary_client_key_start");
extern const uint8_t client_key_pem_end[] asm("_binary_client_key_end");
extern const uint8_t server_cert_pem_start[] asm("_binary_broker_CA_crt_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_broker_CA_crt_end");

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%ld", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        msg_id = esp_mqtt_client_subscribe(client, "/topic/qos0", 0);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "/topic/qos1", 1);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_unsubscribe(client, "/topic/qos1");
        ESP_LOGI(TAG, "sent unsubscribe successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

static void mqtt_app_start(void)
{
    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = BROKER_URI,
        .broker.verification.certificate = (const char *)server_cert_pem_start,
        .credentials.authentication.certificate = (const char *)client_cert_pem_start,
        .credentials.authentication.key = (const char *)client_key_pem_start,
    };

    ESP_LOGI(TAG, "[APP] Free memory: %ld bytes", esp_get_free_heap_size());
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);

    mqtt_task_params_t task_params = {
        .client = client
    };

    xTaskCreate(&http_get_task, "http_get_task", 4096, &task_params, 5, NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %ld bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_BASE", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* This helper function configures Wi-Fi or Ethernet, as selected in menuconfig.
     * Read "Establishing Wi-Fi or Ethernet Connection" section in
     * examples/protocols/README.md for more information about this function.
     */
    ESP_ERROR_CHECK(example_connect());

    mqtt_app_start();
}
