#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#include <esp_system.h>
#include <esp_log.h>

#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <nvs_flash.h>
#include <mqtt_client.h>

#include "esp_netif.h"
#include "lwip/inet.h"

#include <string.h>

#include "esp_ota_ops.h"
#include <inttypes.h>

// Includes para Mender
#include <esp_mac.h>
#include "mender-client.h"
#include "mender-flash.h"

// Definimos el pin donde conectaste el "IN" del relé
#define RELAY_PIN 13 
#define LED_GPIO 4

static const char* TAG = "pFinalActuador";
#define VERSION "1.0.2"

#define TOPIC_STATUS   "sed/G04/actuador/status"
#define TOPIC_LED      "sed/G04/actuador/led"
#define TOPIC_TEMP     "sed/G04/sensorT/temp"
#define TOPIC_SOIL     "sed/G04/sensorS/soil"

typedef struct
{
    char topic[64];
    char payload[32];
} mqtt_msg_t;

static QueueHandle_t mqtt_queue;

void task_mqtt_publish(void* pvParameters)
{
    esp_mqtt_client_handle_t client = (esp_mqtt_client_handle_t)pvParameters;

    mqtt_msg_t msg;

    while (1)
    {
        if (xQueueReceive(mqtt_queue, &msg, portMAX_DELAY))
        {
            if (client != NULL)
            {
                esp_mqtt_client_publish(client, msg.topic, msg.payload, 0, 1, 0);

                ESP_LOGI(TAG, "MQTT -> Topic: %s | Payload: %s", msg.topic, msg.payload);
            }
        }
    }
}

void wifi_init_sta(void) {
    // Inicializar la pila de red y el bucle de eventos
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    // Configuración por defecto del WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Configurar SSID y Password desde el menuconfig
    wifi_config_t wifi_config = {
      .sta = {
        .ssid = CONFIG_ESP_WIFI_SSID,
        .password = CONFIG_ESP_WIFI_PASSWORD,
      },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    // Arrancar el WiFi
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI("WIFI", "Conectando a %s...", CONFIG_ESP_WIFI_SSID);
    esp_wifi_connect();
}

// Configuración Mender
// --- Variables globales
mender_client_config_t mender_config;
mender_client_callbacks_t mender_callbacks;
mender_keystore_t mender_identity[2];
char mender_mac_address[18];
// --- Callbacks
static mender_err_t mender_network_connect_cb(void) { return MENDER_OK; }
static mender_err_t mender_network_release_cb(void) { return MENDER_OK; }
static mender_err_t mender_auth_failure_cb(void) { return MENDER_OK; }

static mender_err_t mender_deployment_status_cb(mender_deployment_status_t status, char* desc) {
    ESP_LOGI("MENDER", "Estado del despliegue: %s", desc ? desc : "Desconocido");
    return MENDER_OK;
}

// static mender_err_t mender_auth_success_cb(void) { 
//   ESP_LOGI("MENDER", "Autenticacion exitosa con el servidor");
//   // Esto es VITAL: le dice al ESP32 que el firmware funciona y no debe volver a la versión anterior
//   return mender_flash_confirm_image(); 
// }

static mender_err_t mender_auth_success_cb(void) {
    ESP_LOGI("MENDER", "Autenticacion exitosa con el servidor");
    return MENDER_OK;
}

static mender_err_t mender_restart_cb(void) {
    ESP_LOGI("MENDER", "Reiniciando el sistema por peticion de OTA...");
    esp_restart();
    return MENDER_OK;
}

static void mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Conectado al Broker");
        // Publicar mensaje "Online" para coherencia con LWT
        mqtt_msg_t msg;
        snprintf(msg.topic, sizeof(msg.topic), "%s", TOPIC_STATUS);
        snprintf(msg.payload, sizeof(msg.payload), "%s", "Online");
        xQueueSend(mqtt_queue, &msg, portMAX_DELAY);
        // Suscribirse al tópico del LED 
        esp_mqtt_client_subscribe(client, TOPIC_TEMP, 1);
        esp_mqtt_client_subscribe(client, TOPIC_SOIL, 1);
        esp_mqtt_client_subscribe(client, TOPIC_LED, 1);
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "Mensaje recibido en Tópico: %.*s", event->topic_len, event->topic);
        ESP_LOGI(TAG, "Datos: %.*s", event->data_len, event->data);
        

        char buffer[64] = { 0 };
        memcpy(buffer, event->data, event->data_len);
        ESP_LOGI("LOG EVENT", "Valor: %.2f", event->data);

        if (strcmp(event->topic, "sed/G04/sensorT/temp") == 0) {
            float temperatura;
            temperatura = atof(buffer);
            ESP_LOGI("LOG", "Valor: %.2f", temperatura);
        }      
        if (strcmp(event->topic, "sed/G04/sensorS/soil") == 0) {

            float humedad;
            humedad = atof(buffer);
            
            ESP_LOGI("LOG", "Valor: %.2f", humedad);

            if (humedad < 15) {
                printf("ACTIVANDO RELE (Bomba ON)...\n");
                // Enviar 3.3V (Nivel Alto / 1) para activar el relé
                gpio_set_level(RELAY_PIN, 1);
                vTaskDelay(2000 / portTICK_PERIOD_MS); // Mantener encendido 2 segundos
                ESP_LOGI(TAG, "BOMBA encendida");
            }
            else {
                printf("DESACTIVANDO RELE (Bomba OFF)...\n");
                // Enviar 0V (Nivel Bajo / 0) para desactivar el relé
                gpio_set_level(RELAY_PIN, 0);
            }
        }
        // Procesar comando para el LED
        if (strncmp("ON", event->data, event->data_len) == 0) {
            // En lugar de 1 (máximo voltaje), enviamos pulsos rápidos
            gpio_set_level(LED_GPIO, 1); // Led encendido
            ESP_LOGI(TAG, "LED encendido");
        }
        else if (strncmp("OFF", event->data, event->data_len) == 0) {
            gpio_set_level(LED_GPIO, 0); // Apagado
            ESP_LOGI(TAG, "LED apagado");
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "Error en el stack MQTT");
        // comprobar
        wifi_ap_record_t ap_info;

        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            ESP_LOGI(TAG, "Conectado a: %s", ap_info.ssid);
        }
        else {
            ESP_LOGI(TAG, "No conectado, reconectando...");
            wifi_init_sta(); // Reconectamos la red 
        }
        break;

    default:
        break;
    }
}

static esp_mqtt_client_handle_t mqtt_app_start(void) {
    esp_mqtt_client_config_t mqtt_cfg = {
      .broker.address.uri = CONFIG_BROKER_URL, // mqtt://IP del router ASUS
      .session.last_will = {
        .topic = TOPIC_STATUS,
        .msg = "Offline",
        .msg_len = strlen("Offline"),
        .qos = 1,
        .retain = 1,
      },
      .session.keepalive = 10, // Detección rápida de desconexión
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
    return client;
}

// Función de diagnóstico: Verifica si estamos conectados al Wi-Fi
bool perform_health_check() {
    wifi_ap_record_t ap_info;
    // Si podemos leer la info del AP, es que estamos conectados
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        ESP_LOGI("HEALTH", "Conectado al AP con RSSI: %d", ap_info.rssi);
        return true;
    }
    ESP_LOGE("HEALTH", "Fallo de conexión Wi-Fi.");
    return false;
}

// Lógica de supervivencia
void check_and_commit_ota() {
    esp_ota_img_states_t ota_state;
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI("OTA", "Imagen en periodo de prueba. Iniciando auto-diagnóstico...");
            if (perform_health_check()) {
                ESP_LOGI("OTA", "Salud verificada. Marcando firmware como VLIDO.");
                esp_ota_mark_app_valid_cancel_rollback();
            }
            else {
                ESP_LOGE("OTA", "Error en diagnóstico. Forzando Rollback y reiniciando...");
                esp_ota_mark_app_invalid_rollback_and_reboot();
            }
        }
    }
}

void app_main(void)
{
    // Crear cola MQTT
    mqtt_queue = xQueueCreate(
        10,
        sizeof(mqtt_msg_t)
    );

    const esp_partition_t* running = esp_ota_get_running_partition();
    printf("--- SISTEMA INICIADO ---\n");
    printf("Versión de Firmware: %s\n", VERSION);
    printf("Ejecutando desde partición: %s\n", running->label);
    printf("Dirección Offset: 0x%08" PRIx32 "\n", running->address);

    // Iniciar WiFi
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    wifi_init_sta(); // Conectamos a la red 

    // Iniciar MQTT
    // Esperar un poco antes de iniciar MQTT o usar Event Groups
    // Una solución sencilla para el lab es un pequeño retardo, 
    // aunque lo ideal es registrar un handler para IP_EVENT_STA_GOT_IP
    vTaskDelay(pdMS_TO_TICKS(12000));

    // DNS
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_dns_info_t dns;

    dns.ip.u_addr.ip4.addr = ipaddr_addr("8.8.8.8");
    dns.ip.type = IPADDR_TYPE_V4;

    esp_netif_set_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns);
    // DNS

    check_and_commit_ota();

    // --- INICIO CONFIGURACIÓN MENDER ---
    // 1. Obtener la dirección MAC (Mender la exige como identificador único)
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    sprintf(mender_mac_address, "%02x:%02x:%02x:%02x:%02x:%02x",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    // 2. Identidad
    mender_identity[0].name = "mac";
    mender_identity[0].value = mender_mac_address;
    mender_identity[1].name = NULL;
    mender_identity[1].value = NULL;

    // 3. Configurar los parámetros
    mender_config.identity = mender_identity;
    mender_config.artifact_name = VERSION;
    mender_config.device_type = "esp32";
    mender_config.host = CONFIG_MENDER_SERVER_HOST;
    mender_config.tenant_token = CONFIG_MENDER_SERVER_TENANT_TOKEN;
    mender_config.authentication_poll_interval = 60;
    mender_config.update_poll_interval = 60;
    mender_config.recommissioning = false;

    // 4. Definir los callbacks obligatorios
    mender_callbacks.network_connect = mender_network_connect_cb;
    mender_callbacks.network_release = mender_network_release_cb;
    mender_callbacks.authentication_success = mender_auth_success_cb;
    mender_callbacks.authentication_failure = mender_auth_failure_cb;
    mender_callbacks.deployment_status = mender_deployment_status_cb;
    mender_callbacks.restart = mender_restart_cb;

    // 5. Arrancar el cliente Mender en segundo plano
    ESP_LOGI("MENDER", "Iniciando cliente con MAC: %s", mender_mac_address);
    if (mender_client_init(&mender_config, &mender_callbacks) == MENDER_OK) {
        mender_client_activate(); // ¡Esto pone a Mender a funcionar en segundo plano!
    }
    else {
        ESP_LOGE("MENDER", "Fallo al inicializar Mender");
    }
    // --- FIN CONFIGURACIÓN MENDER ---

    // Limpiar la configuración anterior del pin y establecerlo como salida
    gpio_reset_pin(RELAY_PIN);
    gpio_set_direction(RELAY_PIN, GPIO_MODE_OUTPUT);

    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);

    esp_mqtt_client_handle_t client = mqtt_app_start();

    // Tarea para publicar MQTT
    xTaskCreate(task_mqtt_publish, "task_mqtt", configMINIMAL_STACK_SIZE * 8, client, 5, NULL);
}