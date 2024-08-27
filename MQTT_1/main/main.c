#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdbool.h>

#include "driver/gpio.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"


#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/timers.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/timers.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "mqtt_client.h"
#include "nvs_flash.h"


//Define del Wifi
#define EXAMPLE_ESP_WIFI_SSID "JCPA 8614"
#define EXAMPLE_ESP_WIFI_PASS "6W99*67g"
#define EXAMPLE_ESP_MAXIMUM_RETRY 10
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

//Define del MQTT
#define Broker_Uri "ws://broker.emqx.io:8083/mqtt"
#define TOPIC_ROOM_1 "ESP32_1/ROOM_1"
#define TOPIC_ROOM_2 "ESP32_1/ROOM_2"


//Defines para los pines y tareas
//Pines
#define SEN_DOOR_1 GPIO_NUM_41
#define SEN_DOOR_2 GPIO_NUM_48
#define SEN_CLEAN_1 GPIO_NUM_42
#define SEN_CLEAN_2 GPIO_NUM_46
#define N_PINES 4

//Tiempos de las tareas
#define STACK_SIZE 1024
#define SEG 10000
#define mSEG_100 100
#define Min_2 2 * 60 * SEG


//Variables y Manejadores
static EventGroupHandle_t s_wifi_event_group;
static const char *TAG = "MSG";
static int s_retry_num = 0;
esp_mqtt_client_handle_t client;
esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event);


//Pines
int Pines [] = {SEN_DOOR_1, SEN_DOOR_2, SEN_CLEAN_1, SEN_CLEAN_2};

//Estructura de los estados de los pines
typedef struct Pines_States {
    unsigned int DSS_1; // door state sensor 1
    unsigned int DSS_2; // door state sensor 2
    unsigned int CSS_1; //clean state sensor 1
    unsigned int CSS_2; //clean state sensor 2
    unsigned int * SS;    //puntero para las variables
}Pines_States;

Pines_States Pin_State;
Pines_States Pin_State_Aux;

//Timer
volatile int seg;
volatile int counter;
int interval = 100;
int timerID = 1;
TimerHandle_t xTimers;
int cambio;

esp_err_t Pin_State_Compere (Pines_States *vPin_S, Pines_States *vPin_S_Aux, int vN);
esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event);
void Set_Pin (int *vPin, int vN);
void Pin_State_Read (int * vPin, Pines_States *vPin_S, int vN);
void Pub_Pin_State (Pines_States *vPin_S, int vN);
void wifi_init_sta(void);
void vTask_Create(void);
void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
void mqtt_app_start(void);

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data);



void vTimerCallback(TimerHandle_t pxTimer){
    counter = (counter < 9)? counter + 1:0;
    Pin_State_Read (&Pines[1], &Pin_State, N_PINES);
    if (counter  == 9) {
        seg = (seg < Min_2)? seg + 1:0;
        cambio = Pin_State_Compere (&Pin_State, &Pin_State_Aux, N_PINES);
        if(cambio){
            Pub_Pin_State (&Pin_State, N_PINES);
        }
   }
    if (seg == Min_2) {
        Pub_Pin_State (&Pin_State, N_PINES);
    }
}

esp_err_t Set_Timer(void){

    ESP_LOGI(TAG, "Timer Init Config");
    xTimers = xTimerCreate  ("Timer",
                            (pdMS_TO_TICKS(interval)),
                                pdTRUE,
                            (void *) timerID,
                            vTimerCallback
    );

          if( xTimers == NULL )
          {
            ESP_LOGE(TAG, "Timer was not created");
          }
          else
          {

              if( xTimerStart( xTimers , 0 ) != pdPASS )
              {
                ESP_LOGE(TAG, "Timer could not be set into the Active state");
              }
          }

    return ESP_OK;
}



void app_main(void){
    Pin_State.SS = &Pin_State.CSS_1;
    Pin_State_Aux.SS = &Pin_State_Aux.CSS_1;

    Set_Pin(&Pines[0], N_PINES);
    wifi_init_sta();
    mqtt_app_start();
    Pin_State_Read (&Pines[1], &Pin_State, N_PINES);
    Pub_Pin_State (&Pin_State, N_PINES);
    //Set_Timer();
    //vTask_Create();
}

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }

}

void wifi_init_sta(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    }
    else
    {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }

    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
    vEventGroupDelete(s_wifi_event_group);
}

void Set_Pin (int *vPin, int vN){
    for(int x = 0; x < vN; x++){
        gpio_set_direction(*(x+vPin), GPIO_MODE_INPUT);
        gpio_set_pull_mode(*(x+vPin), GPIO_PULLDOWN_ONLY);
    }
}

void Pin_State_Read (int * vPin, Pines_States *vPin_S, int vN){
    for(int i = 0; i < vN; i++){
        if(gpio_get_level(*(vPin + i)) == true) { *(i + vPin_S->SS) = true; }
        else { *(i + vPin_S->SS) = false; }
    }
}

int Pin_State_Compere (Pines_States *vPin_S, Pines_States *vPin_S_Aux, int vN){
        for(int j = 0; j < vN; j++){
        if(*(j + vPin_S->SS) == *(j + vPin_S_Aux->SS)){
            continue;
        }
        else {
            //Hubo un cambio
            *(vPin_S_Aux->SS) = *(vPin_S->SS);
            *(1 + vPin_S_Aux->SS) = *(1 + vPin_S->SS);
            *(2 + vPin_S_Aux->SS) = *(2 + vPin_S->SS);
            *(3 + vPin_S_Aux->SS) = *(3 + vPin_S->SS);
            return 1;
        }
    }
    return 0;
}

/*
// Tasks
void vTask_2MIN(void *pvParameters)
 {
  for( ;; )
  {
    Pub_Pin_State (&Pin_State, N_PINES);
    vTaskDelay(pdMS_TO_TICKS(Min_2));
  }
 }

void vTask_100mS(void *pvParameters)
 {
  for( ;; )
  {
    Pin_State_Read (&Pines[1], &Pin_State, N_PINES);
    vTaskDelay(pdMS_TO_TICKS(mSEG_100));
  }
 }

void vTask_SEG(void *pvParameters)
 {
    int cambio;
  for( ;; )
  {
    cambio = Pin_State_Compere (&Pin_State, &Pin_State_Aux, N_PINES);
    if(cambio){
        Pub_Pin_State (&Pin_State, N_PINES);
    }
    vTaskDelay(pdMS_TO_TICKS(SEG));
  }
 }
//Crear Tasks.
void vTask_Create( void ){

    static uint8_t ucParameterToPass;
    TaskHandle_t xHandle = NULL;
    xTaskCreate(vTask_100mS,
                "vTask_100mS",
                STACK_SIZE,
                &ucParameterToPass,
                5,
                &xHandle);


    xTaskCreate(vTask_SEG,
                "vTask_SEG",
                STACK_SIZE,
                &ucParameterToPass,
                4,
                &xHandle);

    xTaskCreate(vTask_2MIN,
                "vTask_2MIN",
                STACK_SIZE,
                &ucParameterToPass,
                4,
                &xHandle);

}
*/
esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;
    switch (event->event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        esp_mqtt_client_subscribe(client, "ESP-32", 0);
        esp_mqtt_client_publish(client, "my topic", "READY", 0, 1, 0);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        //ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("\nTOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%*s\r\n",event->data_len,event->data);
        for (int i = 0; i <event->data_len; i++)  {event->data[i]='\0';}
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
    return ESP_OK;
}

void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%ld", base, event_id);
    mqtt_event_handler_cb(event_data);
}


void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
         .broker.address.uri = Broker_Uri,
    };
    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    esp_mqtt_client_start(client);
    esp_mqtt_client_subscribe(client, TOPIC_ROOM_1, 0);
    esp_mqtt_client_subscribe(client, TOPIC_ROOM_2, 0);
}


