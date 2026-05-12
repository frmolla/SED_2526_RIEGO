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

static float delay = 10.0; // Tiempo

// Definimos el pin donde conectaste el "IN" del relé
#define RELAY_PIN 13 

static const char* TAG = "pFinalActuador";

#define LED_GPIO 2
#define TOPIC_STATUS   "sed/G04/statusA"
#define TOPIC_LED      "sed/G04/actuador/led"
#define TOPIC_TEMP     "sed/G04/sensorT/temp"
#define TOPIC_SOIL     "sed/G04/sensorS/soil"
#define TOPIC_DELAY     "sed/G04/delay"

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

static void mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Conectado al Broker");
        // Publicar mensaje "Online" para coherencia con LWT
        esp_mqtt_client_publish(client, TOPIC_STATUS, "Online", 0, 1, 0);
        // Suscribirse al tópico del LED 
        esp_mqtt_client_subscribe(client, TOPIC_TEMP, 1);
        esp_mqtt_client_subscribe(client, TOPIC_SOIL, 1);
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "Mensaje recibido en Tópico: %.*s", event->topic_len, event->topic);
        ESP_LOGI(TAG, "Datos: %.*s", event->data_len, event->data);
        
        float temperatura;
        float humedad;

        char buffer[64] = { 0 };
        memcpy(buffer, event->data, event->data_len);
        ESP_LOGI("LOG EVENT", "Valor: %.2f", event->data);

        if (strcmp(event->topic, "sed/G04/sensorT/temp") == 0) {
            temperatura = atof(buffer);
            ESP_LOGI("LOG", "Valor: %.2f", temperatura);
        }      
        if (strcmp(event->topic, "sed/G04/sensorS/soil") == 0) {
            humedad = atof(buffer);
            
            ESP_LOGI("LOG", "Valor: %.2f", humedad);

            if (humedad > 24) {
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
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "Error en el stack MQTT");
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

void task_periodic(void* pvParameters) {
    while (1) {
        ESP_LOGI(TAG, "Tarea periódica");
        // Esperar 10 segundos
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void app_main(void)
{
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

    esp_mqtt_client_handle_t client = mqtt_app_start();

    // Limpiar la configuración anterior del pin y establecerlo como salida
    gpio_reset_pin(RELAY_PIN);
    gpio_set_direction(RELAY_PIN, GPIO_MODE_OUTPUT);

    //xTaskCreate(task_procesar, "funcionamiento bomba", configMINIMAL_STACK_SIZE * 8, client, 5, NULL);
    xTaskCreate(task_periodic, "periodic task", configMINIMAL_STACK_SIZE * 8, NULL, 5, NULL);
}