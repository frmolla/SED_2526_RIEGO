// Includes de la práctica p2.2
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <esp_system.h>
#include <esp_log.h>
// Incluimos los drivers del componente externo
#include <si7021.h>
#include <i2cdev.h>
// Pueden faltar includes 2.2 final
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <nvs_flash.h>
#include <mqtt_client.h>

#include "esp_netif.h"
#include "lwip/inet.h"

#include "driver/i2c.h"
#include "esp_adc/adc_oneshot.h"

#include <stdlib.h>
#include "driver/gpio.h"

#include <string.h>

#include "esp_sntp.h"
#include <time.h>

static const char* TAG = "pFinalSensor";
#define VERSION "1.0.0"

// G04
#define TOPIC_STATUS   "sed/G04/sensor/status"
#define TOPIC_LED      "sed/G04/actuador/led"
#define TOPIC_TEMP     "sed/G04/sensorT/temp"
#define TOPIC_DIST     "sed/G04/sensorD/dist"
#define TOPIC_SOIL     "sed/G04/sensorS/soil"
#define TOPIC_DELAY    "sed/G04/delay"

// Configuración ADC (Humedad Tierra)
#define SOIL_ADC_CHANNEL            ADC_CHANNEL_2 // GPIO 2
#define DISTANCE_ADC_CHANNEL        ADC_CHANNEL_3 // GPIO 3

static float delay = 10.0; // Tiempo

typedef struct
{
    char topic[64];
    char payload[32];
} mqtt_msg_t;

static SemaphoreHandle_t adc_mutex;
static SemaphoreHandle_t i2c_mutex;

static QueueHandle_t mqtt_queue;

void iniciar_reloj() {
    // Configuramos el servidor y la zona horaria (España)
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
}

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

void task_lectura_7021(void* pvParameters) {
    // Recuperamos el cliente MQTT pasado desde app_main
    //esp_mqtt_client_handle_t client = pvParameters;
    i2c_dev_t dev = { 0 }; // Estructura del dispositivo I2C
    char payload[16]; // Buffer para el texto de la temperatura

    // Inicializar el descriptor del dispositivo
    // PUERTO_I2C, SDA_PIN, SCL_PIN son gestionados por la librería si se usa i2cdev_init()
    ESP_ERROR_CHECK(si7021_init_desc(&dev, 0, 10, 8));

    mqtt_msg_t msg;
    float temperature;

    while (1) {
        // Proteger I2C
        xSemaphoreTake(i2c_mutex, portMAX_DELAY);
        // Leer valores del sensor
        esp_err_t res1 = si7021_measure_temperature(&dev, &temperature);
        xSemaphoreGive(i2c_mutex);

        if (res1 == ESP_OK) {
            ESP_LOGI(TAG, "Temperatura: %.2f C %", temperature);

            // Preparar msg
            snprintf(msg.topic, sizeof(msg.topic), "%s", TOPIC_TEMP);
            snprintf(msg.payload, sizeof(msg.payload), "%.2f", temperature);
            // Encolamos la información
            xQueueSend(mqtt_queue, &msg, portMAX_DELAY);
        }
        else {
            ESP_LOGE(TAG, "Error leyendo la temperatura: %d (%s)", res1, esp_err_to_name(res1));
        }
        // Esperar 10 segundos
        vTaskDelay(pdMS_TO_TICKS(delay * 1000));
    }
}

void task_lectura_soil(void* pvParameters)
{
    // Recuperar parámetros pasados en la estructura
    adc_oneshot_unit_handle_t adc_handle = (adc_oneshot_unit_handle_t)pvParameters;

    char payload[16];

    mqtt_msg_t msg;
    int humedad_raw;

    while (1)
    {
        // Proteger ADC
        xSemaphoreTake(adc_mutex, portMAX_DELAY);
        adc_oneshot_read(adc_handle, SOIL_ADC_CHANNEL, &humedad_raw);
        xSemaphoreGive(adc_mutex);

        // Convertir a porcentaje
        float soil_pct = 100.0f * (1.0f - ((float)(humedad_raw - 1100) / (2500.0f - 1100.0f)));

        if (soil_pct < 0.0f)
            soil_pct = 0.0f;

        if (soil_pct > 100.0f)
            soil_pct = 100.0f;

        ESP_LOGI(TAG, "Humedad suelo: %.2f %%", soil_pct);

        // Preparar msg
        snprintf(msg.topic, sizeof(msg.topic), "%s", TOPIC_SOIL);
        snprintf(msg.payload, sizeof(msg.payload), "%.2f", soil_pct);

        xQueueSend(mqtt_queue, &msg, portMAX_DELAY);

        vTaskDelay(pdMS_TO_TICKS(delay * 1000));
    }
}

void task_lectura_distancia(void* pvParameters)
{
    // Recuperar parámetros pasados en la estructura
    adc_oneshot_unit_handle_t adc_handle = (adc_oneshot_unit_handle_t)pvParameters;

    char payload[16];

    mqtt_msg_t msg;
    int dist_raw;

    while (1)
    {
        // Proteger ADC
        xSemaphoreTake(adc_mutex, portMAX_DELAY);
        adc_oneshot_read(adc_handle, DISTANCE_ADC_CHANNEL, &dist_raw);
        xSemaphoreGive(adc_mutex);

        // El sensor Sharp da más voltaje cuanto más cerca está el objeto.
        // Una fórmula para convertirlo a cm (aproximada):
        float distancia_cm = 10000.0 / (dist_raw + 1);

        ESP_LOGI(TAG, "Distancia: %.2f cm%", distancia_cm);

        // Preparar msg
        snprintf(msg.topic, sizeof(msg.topic), "%s", TOPIC_DIST);
        snprintf(msg.payload, sizeof(msg.payload), "%.2f", distancia_cm);

        xQueueSend(mqtt_queue, &msg, portMAX_DELAY);

        vTaskDelay(pdMS_TO_TICKS(delay * 1000));
    }
}

void task_logica_horario_led(void*) {  
    while (1)
    {
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        // Encender de 08:00 a 20:00 (Luz de día para la planta)
        if (timeinfo.tm_hour >= 8 && timeinfo.tm_hour < 20) {
            mqtt_msg_t msg;
            snprintf(msg.topic, sizeof(msg.topic), "%s", TOPIC_LED);
            snprintf(msg.payload, sizeof(msg.payload), "%s", "ON");
            xQueueSend(mqtt_queue, &msg, portMAX_DELAY);
        }
        else {
            mqtt_msg_t msg;
            snprintf(msg.topic, sizeof(msg.topic), "%s", TOPIC_LED);
            snprintf(msg.payload, sizeof(msg.payload), "%s", "OFF");
            xQueueSend(mqtt_queue, &msg, portMAX_DELAY);
        }
        vTaskDelay(pdMS_TO_TICKS(delay * 1000));
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
        // Suscribirse al tópico delay
        esp_mqtt_client_subscribe(client, TOPIC_DELAY, 1);
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "Mensaje recibido en Tópico: %.*s", event->topic_len, event->topic);
        ESP_LOGI(TAG, "Datos: %.*s", event->data_len, event->data);

        // Procesamos la actualizacion de delay
        char buffer[64] = { 0 };
        memcpy(buffer, event->data, event->data_len);
        delay = atof(buffer);
        ESP_LOGI(TAG, "Cambio delay: %.2f %", delay);

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

void app_main() {

    adc_mutex = xSemaphoreCreateMutex();
    i2c_mutex = xSemaphoreCreateMutex();

    // Crear cola MQTT
    mqtt_queue = xQueueCreate(
        10,
        sizeof(mqtt_msg_t)
    );

    // Iniciar I2C y configurar el LED
    // Inicializar la librería I2C helper
    ESP_ERROR_CHECK(i2cdev_init());

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

    // Tarea para publicar MQTT
    xTaskCreate(task_mqtt_publish, "task_mqtt", configMINIMAL_STACK_SIZE * 8, client, 5, NULL);

    // Crear la tarea de lectura temp
    xTaskCreate(task_lectura_7021, "lectura_sensor_temp", configMINIMAL_STACK_SIZE * 8, NULL, 5, NULL);

    // Inicializar ADC una sola vez
    adc_oneshot_unit_handle_t adc_handle = NULL;
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1
    };
    adc_oneshot_new_unit(&init_config, &adc_handle);

    // Configurar canales ADC
    adc_oneshot_chan_cfg_t adc_config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_11
    };
    adc_oneshot_config_channel(adc_handle, SOIL_ADC_CHANNEL, &adc_config);
    adc_oneshot_config_channel(adc_handle, DISTANCE_ADC_CHANNEL, &adc_config);

    // Crear la tarea de lectura humd
    xTaskCreate(task_lectura_soil, "lectura_sensor_soil", configMINIMAL_STACK_SIZE * 8, adc_handle, 5, NULL);

    // Crear la tarea de lectura dist
    xTaskCreate(task_lectura_distancia, "lectura_sensor_dist", configMINIMAL_STACK_SIZE * 8, adc_handle, 5, NULL);

    // Crear la tarea para la luz artifical
    iniciar_reloj();
    xTaskCreate(task_logica_horario_led, "led_horario", configMINIMAL_STACK_SIZE * 8, NULL, 5, NULL);
}