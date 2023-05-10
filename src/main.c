#include "my_credentials.h"


#include <esp_log.h>

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#include <esp_wifi.h>

#include <nvs_flash.h>

#include "../lib/spotify/spotify.h"
#include <driver/gpio.h>



/*
Add mdns to components in platform in .platformio

edit menuconfig enabling esp_http_client basic auth, max uri length to 1024 and max header length to 1024

replace esp_http_perform() with function in "new_esp_http_perform_api.txt"
*/


#define BUTTON_VOLUME_UP        GPIO_NUM_4
#define BUTTON_VOLUME_DOWN      GPIO_NUM_15

#define BUTTON_SKIP_NEXT        GPIO_NUM_21
#define BUTTON_PLAY_PAUSE       GPIO_NUM_19
#define BUTTON_SKIP_PREVIOUS    GPIO_NUM_18

#define BUTTON_REPEAT_MODE      GPIO_NUM_22
#define BUTTON_SHUFFLE          GPIO_NUM_23


#define REDIRECT_URI            "http://esp32_spotify.local:3000/callback"
#define REDIRECT_URI_ENC        "http%3A%2F%2Fesp32_spotify.local%3A3000%2Fcallback"

#define MY_MIN(a,b)             (((a)<(b))?(a):(b))

#define WIFI_CONNECTED_BIT      BIT0

static const char *TAG = "spotify";
static EventGroupHandle_t wifi_event_group;



// void get_spotify_token()
// {

//     esp_http_client_config_t config = {
//         .url = SPOTIFY_REQUEST_TOKEN_ENDPOINT,
//         .event_handler = my_http_event_handler,
//         .method = HTTP_METHOD_POST,
//         .transport_type = HTTP_TRANSPORT_OVER_SSL,
//         .crt_bundle_attach = esp_crt_bundle_attach,
//         .keep_alive_enable = true,
//         .buffer_size = 2048,
//     };

//     esp_http_client_handle_t client = esp_http_client_init(&config);
//     esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");

//     unsigned char *buff = malloc(2048);
//     int written = sprintf((char *)buff, "%s:%s", SPOTIFY_CLIENT_ID, SPOTIFY_CLIENT_SECRET);
    
//     unsigned char *base64_auth = malloc(2048);
//     size_t base64_auth_len = 0;
//     mbedtls_base64_encode(base64_auth, 2048, &base64_auth_len, buff, written);
//     sprintf((char*) buff, "Basic %s", (char*) base64_auth);
//     esp_http_client_set_header(client, "Authorization", (char*) buff);

//     written = sprintf((char *)buff, SPOTIFY_REQUEST_TOKEN_POST_TEMPLATE, SPOTIFY_REQUEST_CODE, REDIRECT_URI);

//     esp_http_client_set_post_field(client, (char *)buff, written);

//     if (esp_http_client_perform(client) == ESP_OK)
//     {
//         ESP_LOGI(TAG, "CLIENT_PERFORM_TOKEN_REQUEST");
//         esp_http_client_fetch_headers(client);
//         int cl = esp_http_client_get_content_length(client);
//         ESP_LOGI(TAG, "HTTP_RESPONSE_CODE: %d cl = %d", esp_http_client_get_status_code(client), cl);
//         if (cl > 0)
//         {
//             int eff_cl = esp_http_client_read_response(client, (char*)buff, 2048);
//             buff[eff_cl] = 0;

//             ESP_LOGI(TAG, "effective read: %d, data:\n%s", eff_cl, buff);

//             cJSON *root = cJSON_Parse((char*)buff);
//             cJSON *access_token_json = cJSON_GetObjectItem(root, "refresh_token");
//             //cJSON *token_type_json = cJSON_GetObjectItem(root, "token_type");
//             if (access_token_json && access_token_json->valuestring /*&& token_type_json && token_type_json->valuestring*/)
//             {
//                 strncpy(SPOTIFY_TOKEN, access_token_json->valuestring, SPOTIFY_TOKEN_LEN);
//                 //strncpy(token_type, token_type_json->valuestring, token_type);
//             }
//             cJSON_free(access_token_json);
//             //cJSON_free(token_type_json);
//             cJSON_Delete(root);
//             TOKEN_RECV = 1;
//         }

//         ESP_LOGI(TAG, "Access token: %s", SPOTIFY_TOKEN);
//         //ESP_LOGI(TAG, "Token type: %s", token_type);

        
//     }
//     free(buff);
//     free(base64_auth);
//     esp_http_client_cleanup(client);
// }

void wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{

    switch (event_id)
    {
    case WIFI_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case WIFI_EVENT_STA_CONNECTED:
        ESP_LOGI(TAG, "Wi-Fi connected");

        break;
    case WIFI_EVENT_STA_DISCONNECTED:
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        break;
    case IP_EVENT_STA_GOT_IP:
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "IP address obtained");
        break;
    default:
        break;
    }
}
void wifi_init()
{

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PSWD,
        },
    };

    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        wifi_event_handler,
        NULL,
        NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        wifi_event_handler,
        NULL,
        NULL));
    ESP_ERROR_CHECK(esp_wifi_start());

    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
};


// httpd_handle_t server = NULL;
// esp_err_t handle_get_auth_code(httpd_req_t *req)
// {

//     char *query = NULL;
//     size_t query_len = httpd_req_get_url_query_len(req);
//     if (query_len > 0)
//     {

//         query = malloc(query_len);
//         httpd_req_get_url_query_str(req, query, query_len);
//         // ESP_LOGI(TAG, "Received query str: %s", query);
//         if (httpd_query_key_value(query, SPOTIFY_REQUEST_CODE_QUERY_KEY, SPOTIFY_AUTH_CODE, SPOTIFY_TOKEN_LEN) == ESP_OK)
//         {

//             ESP_LOGI(TAG, "Received auth code: %s", SPOTIFY_AUTH_CODE);

//             ACCESS_CODE_RECV = 1;
//             httpd_stop(server);
//         }

//         free(query);
//     }

//     return ESP_OK;
// }

// const char *webpageTemplate = "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><meta http-equiv=\"X-UA-Compatible\" content=\"IE=edge\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no\" /></head><body><div><a href=\"https://accounts.spotify.com/authorize?client_id=%s&response_type=code&redirect_uri=%s&scope=%s\">spotify Auth</a></div></body></html>";

// esp_err_t handle_get_index(httpd_req_t *req)
// {
//     char webpage[800];
//     int written = sprintf(webpage, webpageTemplate, SPOTIFY_CLIENT_ID, REDIRECT_URI_ENC, SPOTIFY_REQUEST_SCOPE);
//     httpd_resp_send(req, webpage, written);

//     return ESP_OK;
// }

// const httpd_uri_t authorization_uri = {
//     .uri = "/callback",
//     .method = HTTP_GET,
//     .handler = handle_get_auth_code,
//     .user_ctx = NULL,
// };
// const httpd_uri_t index_uri = {
//     .uri = "/",
//     .method = HTTP_GET,
//     .handler = handle_get_index,
//     .user_ctx = NULL,
// };

// void start_ws()
// {
//     httpd_config_t config = HTTPD_DEFAULT_CONFIG();
//     config.server_port = 3000;

//     if (httpd_start(&server, &config) == ESP_OK)
//     {
//         if (server == NULL)
//         {
//             ESP_LOGE(TAG, "Server creation error");
//             return;
//         }
//         httpd_register_uri_handler(server, &authorization_uri);
//         httpd_register_uri_handler(server, &index_uri);
//     }
// }



#define DEBOUNCE_TIME_US 300000 // = 300 ms

typedef struct {
    uint8_t BUTTON_ID;
    int64_t LAST_PRESSED;
}BUTTON_INFO;

BUTTON_INFO button_playpause;
BUTTON_INFO button_next;
BUTTON_INFO button_previous;
BUTTON_INFO button_volume_up;
BUTTON_INFO button_volume_down;
BUTTON_INFO button_shuffle;
BUTTON_INFO button_repeat_mode;
BUTTON_INFO button_add_to_favourite;

enum BUTTON_ID{
    BUTTON_PLAY_PAUSE_ID = 0,
    BUTTON_SKIP_NEXT_ID,
    BUTTON_SKIP_PREVIOUS_ID,
    BUTTON_VOLUME_UP_ID,
    BUTTON_VOLUME_DOWN_ID,
    BUTTON_REPEAT_MODE_ID,
    BUTTON_SHUFFLE_ID,
    BUTTON_ADD_TO_FAVOURITE_ID
};

void initButtons(){
    button_playpause.BUTTON_ID = BUTTON_PLAY_PAUSE_ID;
    button_playpause.LAST_PRESSED = 0;

    button_next.BUTTON_ID = BUTTON_SKIP_NEXT_ID;
    button_next.LAST_PRESSED = 0;

    button_previous.BUTTON_ID = BUTTON_SKIP_PREVIOUS_ID;
    button_previous.LAST_PRESSED = 0;

    button_volume_up.BUTTON_ID = BUTTON_VOLUME_UP_ID;
    button_volume_up.LAST_PRESSED = 0;

    button_volume_down.BUTTON_ID = BUTTON_VOLUME_DOWN_ID;
    button_volume_down.LAST_PRESSED = 0;

    button_repeat_mode.BUTTON_ID = BUTTON_REPEAT_MODE_ID;
    button_repeat_mode.LAST_PRESSED = 0;

    button_shuffle.BUTTON_ID = BUTTON_SHUFFLE_ID;
    button_shuffle.LAST_PRESSED = 0;
}

void song_change(SpotifyInfoBase_t *data){
    ESP_LOGI(TAG, "Song has changed-> %s", data->song_name);
}

QueueHandle_t xButtonEvtQueue;

void IRAM_ATTR gpio_interrupt_handler(void *arg){

    BUTTON_INFO *info = (BUTTON_INFO*)arg;
    int64_t now_millis = 0;
    if( ((now_millis = esp_timer_get_time()) - info->LAST_PRESSED) > DEBOUNCE_TIME_US){
        info->LAST_PRESSED = now_millis;
        BaseType_t xHigerPriorityTaskWoken = pdFALSE;
        xQueueSendFromISR(xButtonEvtQueue, &(info->BUTTON_ID), &xHigerPriorityTaskWoken);

        if(xHigerPriorityTaskWoken){
            portYIELD_FROM_ISR();
        }
    }
}

void task_buttons_handler(void *pvParams){

    uint8_t BUTTON_ID = 0;

    for(;;){
        if(xQueueReceive(xButtonEvtQueue, &BUTTON_ID, portMAX_DELAY) == pdTRUE){

            switch (BUTTON_ID){
                case BUTTON_PLAY_PAUSE_ID:{
                    ESP_LOGI(TAG, "\tPAUSE/PLAY PRESSED");
                    spotify_action_play_pause();
                    break;
                }

                case BUTTON_SKIP_NEXT_ID:{
                    ESP_LOGI(TAG, "\tPLAY NEXT PRESSED");
                    spotify_action_skip_next();
                    break;
                }

                case BUTTON_SKIP_PREVIOUS_ID:{
                    ESP_LOGI(TAG, "\tPLAY PREVIOUS PRESSED");
                    spotify_action_skip_previous();
                    break;
                }

                case BUTTON_VOLUME_UP_ID:{
                    ESP_LOGI(TAG, "\tVOLUME UP PRESSED");
                    spotify_action_increase_playback_volume(10);
                    break;
                }

                case BUTTON_VOLUME_DOWN_ID:{
                    ESP_LOGI(TAG, "\tVOLUME DOWN PRESSED");
                    spotify_action_decrease_playback_volume(10);
                    break;
                }
                case BUTTON_SHUFFLE_ID:{
                    ESP_LOGI(TAG, "\tSHUFFLE BUTTON PRESSED");
                    spotify_action_toggle_playback_shuffle(SPOTIFY_TOGGLE_SHUFFLE_AUTO);
                    break;
                }
                case BUTTON_REPEAT_MODE_ID:{
                    ESP_LOGI(TAG, "\tREPEAT MODE PRESSED");
                    spotify_action_set_repeat_mode(SPOTIFY_SET_REPEAT_MODE_AUTO);
                    break;
                }
                case BUTTON_ADD_TO_FAVOURITE_ID:{
                    ESP_LOGI(TAG, "\tADD TO FAVOURITE PRESSED");
                    break;
                }

                default:
                    break;
            }
        }
    }
}




void my_pp_change(uint8_t is_playing){
    ESP_LOGI(TAG, "PLAYBACK CHANGED TO %s", (is_playing)? "PLAY": "PAUSE");
}

void my_song_change(SpotifyInfo_t *info){
    ESP_LOGI(TAG, "SONG CHANGED TO %s, album art: %s [h:%d, w:%d]", 
        info->item->name, 
        info->item->album->images[info->item->album->images_count-1]->url,
        info->item->album->images[info->item->album->images_count-1]->height,
        info->item->album->images[info->item->album->images_count-1]->width
    );
}

void app_main()
{

    nvs_flash_init();

    //gpio_set_direction(BUILTIN_LED_PIN, GPIO_MODE_OUTPUT);

    wifi_init();
    

    //spotify_first_time_init("esp32_spotify", 3000, "/callback");

    // mdns_init();
    // mdns_hostname_set("esp32_spotify");

    // start_ws();

    // while(!ACCESS_CODE_RECV){ vTaskDelay( 1000/portTICK_PERIOD_MS);}

    //ESP_LOGI(TAG, "\tWAITING_FOR_TOKEN");
    //get_spotify_token();

    // while (!TOKEN_RECV)
    // {
    //     vTaskDelay(1000 / portTICK_PERIOD_MS);
    // }
    //printf("\n");

    // TOKEN GOTTTTTTT

    initButtons();

    xButtonEvtQueue = xQueueCreate(10, sizeof(uint8_t));

    gpio_config_t btn_skip_prev_config ={
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLDOWN_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE,
        .pin_bit_mask = (1ULL << BUTTON_SKIP_PREVIOUS)
    };
    gpio_config_t btn_skip_next_config ={
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLDOWN_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE,
        .pin_bit_mask = (1ULL << BUTTON_SKIP_NEXT)
    };
    gpio_config_t btn_play_pause_config ={
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLDOWN_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE,
        .pin_bit_mask = (1ULL << BUTTON_PLAY_PAUSE)
    };
    gpio_config_t btn_volume_up_config ={
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLDOWN_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE,
        .pin_bit_mask = (1ULL << BUTTON_VOLUME_UP)
    };
    gpio_config_t btn_volume_down_config ={
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLDOWN_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE,
        .pin_bit_mask = (1ULL << BUTTON_VOLUME_DOWN)
    };
    gpio_config_t btn_shuffle_config ={
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLDOWN_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE,
        .pin_bit_mask = (1ULL << BUTTON_SHUFFLE)
    };
    gpio_config_t btn_repeat_mode_config ={
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLDOWN_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE,
        .pin_bit_mask = (1ULL << BUTTON_REPEAT_MODE)
    };

    gpio_config(&btn_skip_prev_config);
    gpio_config(&btn_skip_next_config);
    gpio_config(&btn_play_pause_config);
    gpio_config(&btn_volume_up_config);
    gpio_config(&btn_volume_down_config);
    gpio_config(&btn_shuffle_config);
    gpio_config(&btn_repeat_mode_config);


    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_PLAY_PAUSE, gpio_interrupt_handler, (void*)&button_playpause);
    gpio_isr_handler_add(BUTTON_SKIP_PREVIOUS, gpio_interrupt_handler, (void*)&button_previous);
    gpio_isr_handler_add(BUTTON_SKIP_NEXT, gpio_interrupt_handler, (void*)&button_next);
    gpio_isr_handler_add(BUTTON_VOLUME_UP, gpio_interrupt_handler, (void*)&button_volume_up);
    gpio_isr_handler_add(BUTTON_VOLUME_DOWN, gpio_interrupt_handler, (void*)&button_volume_down);
    gpio_isr_handler_add(BUTTON_REPEAT_MODE, gpio_interrupt_handler, (void*)&button_repeat_mode);
    gpio_isr_handler_add(BUTTON_SHUFFLE, gpio_interrupt_handler, (void*)&button_shuffle);


    xTaskCreate(task_buttons_handler, "task_button_handler", 2048, NULL, 5, NULL);


    if(spotify_init(SPOTIFY_CLIENT_ID, SPOTIFY_CLIENT_SECRET, SPOTIFY_REFRESH_TOKEN, SPOTIFY_INFO) != ESP_OK){
        
    }
    spotify_add_on_play_pause_change_cb(&my_pp_change);
    spotify_add_on_song_change_cb(&my_song_change);
    spotify_set_update_delay(200);
    spotify_start();

    int min_total, min_elapsed, seconds_total, seconds_elapsed;

    while(1){

        //SpotifyInfoBase_t *_info = spotify_get_last_info();
        SpotifyInfo_t *_info = spotify_get_last_info();

        if(_info == NULL){
            vTaskDelay(1000 /portTICK_PERIOD_MS);
            continue;
        }
        
        seconds_total = _info->item->duration_ms / 1000;
        seconds_elapsed = _info->progress_ms /1000;

        min_total = seconds_total / 60;
        min_elapsed = seconds_elapsed / 60;

        if(_info->item->name == NULL) ESP_LOGI(TAG, "SONG_NAME IS NULL");
        else ESP_LOGI(TAG, "\t\t%s %d:%02d/%d:%02d volume: %u%%[stack available %lu]", _info->item->name, min_elapsed, seconds_elapsed%60, min_total, seconds_total%60, _info->device->volume_percent, esp_get_free_heap_size());

        // ESP_LOGI(TAG, "smallest image size-> h=%d, w=%d", 
        //     _info->item->album->images[_info->item->album->images_count-1]->height,
        //     _info->item->album->images[_info->item->album->images_count-1]->width
        // );
        vTaskDelay(500/portTICK_PERIOD_MS);

    }

}