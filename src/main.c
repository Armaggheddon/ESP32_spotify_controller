#include "my_credentials.h"


#include <esp_log.h>
#include <string.h>

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#include <esp_wifi.h>

#include <nvs_flash.h>

#include "../lib/spotify/spotify.h"
#include "../lib/buttons_manager/buttons_manager.h"
#include "../lib/s7789_display/st7789_display.h"
#include "../lib/jpeg_decode/jpeg_decode.h"

#include <driver/gpio.h>
#include <driver/spi_master.h>


#define USE_BUTTONS 1
#define USE_DISPLAY 1

/*  Buttons are active low: each one shorts the pin to ground and the internal
    pull up keeps it high when released, so presses are falling edges.
    NOTE: these pins must NOT overlap the DISPLAY_* pins below, otherwise the
    button configuration would take over the SPI/control pins and the display
    would stop working. */
#define BUTTON_INTR_EDGE        GPIO_INTR_NEGEDGE

#define BUTTON_VOLUME_UP        GPIO_NUM_33
#define BUTTON_VOLUME_DOWN      GPIO_NUM_25

#define BUTTON_SKIP_NEXT        GPIO_NUM_26
#define BUTTON_PLAY_PAUSE       GPIO_NUM_32
#define BUTTON_SKIP_PREVIOUS    GPIO_NUM_14

#define BUTTON_REPEAT_MODE      GPIO_NUM_13
#define BUTTON_SHUFFLE          GPIO_NUM_12


#define DISPLAY_SPI_HOST_ID     SPI2_HOST
#define DISPLAY_SCLK            GPIO_NUM_21
#define DISPLAY_MOSI            GPIO_NUM_19
#define DISPLAY_RES             GPIO_NUM_18
#define DISPLAY_DC              GPIO_NUM_5
#define DISPLAY_BLK             GPIO_NUM_4

#define DISPLAY_WIDTH_PX        240
#define DISPLAY_HEIGHT_PX       240
#define DISPLAY_PX_COUNT        DISPLAY_WIDTH_PX * DISPLAY_HEIGHT_PX
#define DISPLAY_PX_CLOCK_HZ     20 * 1000 * 1000


#define WIFI_CONNECTED_BIT      BIT0

static const char *TAG = "spotify";
static EventGroupHandle_t wifi_event_group;


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
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
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


#define DEBOUNCE_TIME_US 300000 // = 300 ms


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

QueueHandle_t xButtonEvtQueue;

void IRAM_ATTR gpio_interrupt_handler(void *arg){

    ButtonInfo_t *info = (ButtonInfo_t*)arg;
    int64_t now_millis = 0;
    if( ((now_millis = esp_timer_get_time()) - info->last_pressed) > DEBOUNCE_TIME_US){
        info->last_pressed = now_millis;
        
        if(info->state + 1 == info->max_states){
            info->state = 0; // loop back to 0
        }else{
            info->state++;
        }
        BaseType_t xHigerPriorityTaskWoken = pdFALSE;
        xQueueSendFromISR(xButtonEvtQueue, (info), &xHigerPriorityTaskWoken);

        if(xHigerPriorityTaskWoken){
            portYIELD_FROM_ISR();
        }
    }
}

void task_buttons_handler(void *pvParams){

    ButtonInfo_t current_button;

    for(;;){
        if(xQueueReceive(xButtonEvtQueue, &current_button, portMAX_DELAY) == pdTRUE){

            switch (current_button.id){
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
                    spotify_action_set_repeat_mode(SPOTIFY_REPEAT_MODE_AUTO);
                    break;
                }
                case BUTTON_ADD_TO_FAVOURITE_ID:{
                    ESP_LOGI(TAG, "\tADD TO FAVOURITE PRESSED");
                    spotify_action_add_currently_playing_to_favourite();
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

//The smallest album art spotify returns is 64x64, but the jpeg can still be
//several KB, the decoder and the http handler are both bounded by this size
#define IMG_BUFF_SIZE (16 * 1024)

char img_buff[IMG_BUFF_SIZE];

//The smallest album art Spotify provides is used as the decode target: the display
//driver upscales it to the screen size while sending it, so only this small
//RGB565 buffer is needed instead of a full resolution one
#define ALBUM_ART_BUFFER_SIZE (SPOTIFY_ALBUM_ART_SIZE_64x64 * SPOTIFY_ALBUM_ART_SIZE_64x64 * 2)

static uint8_t album_art_buffer[ALBUM_ART_BUFFER_SIZE];

void my_song_change(SpotifyInfo_t *info){

    ESP_LOGI(TAG, "SONG CHANGED TO %s", info->item->name);

    if(info->item->album->images != NULL && info->item->album->images_count > 0){
        ESP_LOGI(TAG, "\talbum art(smallest): %s [h:%d, w:%d]",
            info->item->album->images[info->item->album->images_count-1]->url,
            info->item->album->images[info->item->album->images_count-1]->height,
            info->item->album->images[info->item->album->images_count-1]->width
        );
    }

    int img_size = spotify_get_album_cover_art_latest(SPOTIFY_ALBUM_ART_SIZE_SMALLEST, img_buff, (int)sizeof(img_buff));
    if(img_size > 0){
        ESP_LOGI(TAG, "SUCCESS  downloading image [%d bytes]!!", img_size);

        if(jpeg_decode_process((const uint8_t *)img_buff, img_size) == 0){
            st7789_display_push_scaled(album_art_buffer, SPOTIFY_ALBUM_ART_SIZE_64x64, SPOTIFY_ALBUM_ART_SIZE_64x64);
        }

    }else{
        ESP_LOGI(TAG, "ERROR downloading image!!");
    }
}

void app_main()
{

    nvs_flash_init();

#if USE_DISPLAY

    // The driver only keeps a small band buffer (a handful of destination rows),
    // the album art is decoded at its native size and upscaled while transmitted
    st7789_display_init(
        DISPLAY_SPI_HOST_ID,
        DISPLAY_SCLK,
        DISPLAY_MOSI,
        DISPLAY_RES,
        DISPLAY_DC,
        DISPLAY_BLK,
        DISPLAY_WIDTH_PX,
        DISPLAY_HEIGHT_PX,
        DISPLAY_PX_COUNT,
        0,
        0,
        DISPLAY_PX_CLOCK_HZ);

    st7789_display_begin();

    // Decode straight into the small album art buffer, the driver upscales it
    jpeg_decode_init(album_art_buffer, SPOTIFY_ALBUM_ART_SIZE_64x64, SPOTIFY_ALBUM_ART_SIZE_64x64);

#endif

    wifi_init();

#if USE_BUTTONS
    xButtonEvtQueue = xQueueCreate(5, sizeof(ButtonInfo_t));

    buttons_init(7);

    //The task only logs and dispatches, the spotify actions spawn their own tasks
    buttons_add_button( GPIO_MODE_INPUT, GPIO_PULLUP_ENABLE, GPIO_PULLDOWN_DISABLE, BUTTON_INTR_EDGE, BUTTON_SKIP_PREVIOUS_ID, BUTTON_SKIP_PREVIOUS, 0, 2, 0, gpio_interrupt_handler);
    buttons_add_button( GPIO_MODE_INPUT, GPIO_PULLUP_ENABLE, GPIO_PULLDOWN_DISABLE, BUTTON_INTR_EDGE, BUTTON_SKIP_NEXT_ID, BUTTON_SKIP_NEXT, 0, 2, 0, gpio_interrupt_handler);
    buttons_add_button( GPIO_MODE_INPUT, GPIO_PULLUP_ENABLE, GPIO_PULLDOWN_DISABLE, BUTTON_INTR_EDGE, BUTTON_PLAY_PAUSE_ID, BUTTON_PLAY_PAUSE, 0, 2, 0, gpio_interrupt_handler);
    buttons_add_button( GPIO_MODE_INPUT, GPIO_PULLUP_ENABLE, GPIO_PULLDOWN_DISABLE, BUTTON_INTR_EDGE, BUTTON_VOLUME_UP_ID, BUTTON_VOLUME_UP, 0, 2, 0, gpio_interrupt_handler);
    buttons_add_button( GPIO_MODE_INPUT, GPIO_PULLUP_ENABLE, GPIO_PULLDOWN_DISABLE, BUTTON_INTR_EDGE, BUTTON_VOLUME_DOWN_ID, BUTTON_VOLUME_DOWN, 0, 2, 0, gpio_interrupt_handler);
    buttons_add_button( GPIO_MODE_INPUT, GPIO_PULLUP_ENABLE, GPIO_PULLDOWN_DISABLE, BUTTON_INTR_EDGE, BUTTON_SHUFFLE_ID, BUTTON_SHUFFLE, 0, 2, 0, gpio_interrupt_handler);
    buttons_add_button( GPIO_MODE_INPUT, GPIO_PULLUP_ENABLE, GPIO_PULLDOWN_DISABLE, BUTTON_INTR_EDGE, BUTTON_REPEAT_MODE_ID, BUTTON_REPEAT_MODE, 0, 2, 0, gpio_interrupt_handler);

    buttons_start();

    xTaskCreate(task_buttons_handler, "task_button_handler", 2048, NULL, 5, NULL);
#endif

    if(spotify_init(SPOTIFY_CLIENT_ID, SPOTIFY_CLIENT_SECRET, SPOTIFY_REFRESH_TOKEN, SPOTIFY_INFO) != ESP_OK){
        
    }
    spotify_add_on_play_pause_change_cb(&my_pp_change);
    spotify_add_on_song_change_cb(my_song_change);
    spotify_set_update_delay(200);
    spotify_start();


    // int min_total, min_elapsed, seconds_total, seconds_elapsed;

    // while(1){

    //     //SpotifyInfoBase_t *_info = spotify_get_last_info();
    //     SpotifyInfo_t *_info = spotify_get_last_info();

    //     if(_info == NULL){
    //         vTaskDelay(1000 /portTICK_PERIOD_MS);
    //         continue;
    //     }
        
    //     seconds_total = _info->item->duration_ms / 1000;
    //     seconds_elapsed = _info->progress_ms /1000;

    //     min_total = seconds_total / 60;
    //     min_elapsed = seconds_elapsed / 60;

    //     if(_info->item->name == NULL) ESP_LOGI(TAG, "SONG_NAME IS NULL");
    //     else ESP_LOGI(TAG, "\t\t%s %d:%02d/%d:%02d volume: %u%%[stack available %lu]", _info->item->name, min_elapsed, seconds_elapsed%60, min_total, seconds_total%60, _info->device->volume_percent, esp_get_free_heap_size());

    //     vTaskDelay(500/portTICK_PERIOD_MS);

    // }

}