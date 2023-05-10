#include "spotify.h"
#include <esp_http_client.h>
#include <cJSON.h>
#include <mbedtls/base64.h>
#include <esp_crt_bundle.h>

#include <esp_tls.h>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <esp_log.h>



SpotifyInfoBase_t *_spotify_info_base = NULL;
SpotifyInfo_t *_spotify_info = NULL;
SpotifyConfig_t _spotify_config;


TimerHandle_t xRefreshTokenTimer;
QueueHandle_t xTokenQueue;
SemaphoreHandle_t xRefreshTokenSemaphore;


// TODO: have task handle for actions to avoid calling task multiple time on user call 
SemaphoreHandle_t xPlayPauseSemaphre;
SemaphoreHandle_t xSkipNextSemaphore;
SemaphoreHandle_t xSkipPreviousSemaphre;


const char *_spotify_client_id;
const char *_spotify_client_secret;
const char *_spotify_refresh_token;


char _spotify_token[SPOTIFY_TOKEN_LEN];


char _spotify_bearer_auth[SPOTIFY_TOKEN_LEN + 7]; // allocate enough memory to hold "Bearer <token>"

char api_response[SPOTIFY_MAX_API_LEN];
uint16_t api_response_pos;


SpotifyInfoBase_t* spotify_get_last_info_base(){ return _spotify_info_base;}
SpotifyInfo_t* spotify_get_last_info(){ return _spotify_info;}


void IRAM_ATTR token_refresh_timer_cb(TimerHandle_t xTimer){

    BaseType_t xHigerPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(xRefreshTokenSemaphore, &xHigerPriorityTaskWoken);

    if(xHigerPriorityTaskWoken){
        portYIELD_FROM_ISR();
    }
}


void get_new_token(void *pvParam){

    esp_http_client_config_t config = {
        .url = SPOTIFY_REQUEST_TOKEN_ENDPOINT,
        .method = HTTP_METHOD_POST,
        //.event_handler = my_http_event_handler,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 2048,
    };

    for(;;){

        if(xSemaphoreTake(xRefreshTokenSemaphore, portMAX_DELAY)){

            esp_http_client_handle_t client = esp_http_client_init(&config);

            esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");

            char *basic_auth = malloc(512);
            int written = sprintf(basic_auth, "%s:%s", _spotify_client_id, _spotify_client_secret);

            char *basic_auth_base64 = malloc(512);
            size_t written_base64 = 0;
            mbedtls_base64_encode((unsigned char*)basic_auth_base64, 512, &written_base64, (unsigned char*)basic_auth, written);

            sprintf(basic_auth, SPOTIFY_AUTH_FIELD_BASIC_TEMPLATE, basic_auth_base64);
            esp_http_client_set_header(client, SPOTIFY_AUTH_KEY, basic_auth);

            char *post_data = malloc(512);
            written = sprintf(post_data, SPOTIFY_REQUEST_REFRESH_TOKEN_POST_TEMPLATE, _spotify_refresh_token);

            esp_http_client_set_post_field(client, post_data, written);

            if(esp_http_client_perform(client) == ESP_OK){

                esp_http_client_fetch_headers(client);
                int cl = esp_http_client_get_content_length(client);

                if(cl > 0){
                    char *response_data = malloc(1024);
                    int eff_cl = esp_http_client_read_response(client, response_data, 1024);
                    response_data[eff_cl] = 0;

                    cJSON *root = cJSON_Parse(response_data);
                    cJSON *access_token = cJSON_GetObjectItem(root, "access_token");
                    if(access_token != NULL){

                        ESP_LOGI(SPOTIFY_TAG, "Token received");

                        //strcpy(_spotify_token, access_token->valuestring);
                        
                        sprintf(_spotify_bearer_auth, SPOTIFY_AUTH_FIELD_BEARER_TEMPLATE, access_token->valuestring);

                        if(xQueueSend(xTokenQueue, access_token->valuestring, 0) != pdTRUE){
                            //reschedule task in 1 sec
                            xTimerReset(xRefreshTokenTimer, 1000/ portTICK_PERIOD_MS);
                        }
                    }
                    free(response_data);
                    cJSON_Delete(root);
                }
            }

            free(post_data);
            free(basic_auth_base64);
            free(basic_auth);

            esp_http_client_cleanup(client);
        }
    }
}


static esp_err_t my_http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id)
    {
    case HTTP_EVENT_ERROR:
        ESP_LOGE(SPOTIFY_TAG, "HTTP_EVENT_ERROR");
        break;

    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(SPOTIFY_TAG, "HTTP_EVENT_ON_CONNECTED");
        break;

    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(SPOTIFY_TAG, "HTTP_EVENT_HEADER_SENT");
        break;

    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(SPOTIFY_TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;

    case HTTP_EVENT_ON_DATA:
        ESP_LOGD(SPOTIFY_TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);

        if(api_response_pos + evt->data_len < SPOTIFY_MAX_API_LEN){
            memcpy(api_response + api_response_pos, evt->data, evt->data_len);
            api_response_pos += evt->data_len;
        }
        
        break;

    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(SPOTIFY_TAG, "HTTP_EVENT_ON_FINISH");

        api_response[api_response_pos] = '\0';
        api_response_pos = 0;
        
        break;

    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGD(SPOTIFY_TAG, "HTTP_EVENT_DISCONNECTED");

        api_response_pos = 0;
        break;

    default:
        break;
    }

    return ESP_OK;
}


void task_skip_next(void *pvParam){

    esp_http_client_config_t config = {
        .url = SPOTIFY_GET_API_URL(SPOTIFY_PLAYER_REFERENCE, SPOTIFY_SKIP_TO_NEXT_ENDPOINT),
        .method = HTTP_METHOD_POST,
        //.event_handler = my_http_event_handler,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size_tx = 1024,
    };

    //spotify_info *_info = (spotify_info*)pvParam;

    esp_http_client_handle_t client = esp_http_client_init(&config);

    // char *auth_value = malloc(SPOTIFY_TOKEN_LEN + strlen("Bearer "));
    // sprintf(auth_value, "Bearer %s", _spotify_token);

    esp_http_client_set_header(client, SPOTIFY_AUTH_KEY, _spotify_bearer_auth);

    // char *post_data = malloc(512);
    // int written = sprintf(post_data, "device_id=%s", _info->device_id);
    // esp_http_client_set_post_field(client, post_data, written);

    if(esp_http_client_perform(client) == ESP_OK){
        //OK
    }else{
        ESP_LOGE(SPOTIFY_TAG, "Error sending skip to next request");
    }

    //free(auth_value);
    esp_http_client_cleanup(client);

    vTaskDelete(NULL);
} 


void spotify_action_skip_next(void){

    // if(xSemaphoreTake(xSkipNextSemaphore, 0) == pdFALSE){
    //     //task already running
    //     ESP_LOGE(SPOTIFY_TAG, "TASK ALREADY RUNNING");
    //     return;
    // }
    // xSemaphoreGive(xSkipNextSemaphore);
    xTaskCreate(task_skip_next, "skip_next", 4096, NULL, 4, NULL);
}


void task_play_pause(void *pvParam){

    uint8_t is_playing = *((uint8_t*)pvParam);

    esp_http_client_config_t config = {
        .url = SPOTIFY_GET_PLAY_PAUSE_API_URL(is_playing),
        .method = HTTP_METHOD_PUT,
        //.event_handler = my_http_event_handler,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size_tx = 1024,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    // char *auth_value = malloc(SPOTIFY_TOKEN_LEN + strlen("Bearer "));
    // sprintf(auth_value, "Bearer %s", _spotify_token);

    esp_http_client_set_header(client, SPOTIFY_AUTH_KEY, _spotify_bearer_auth);

    if(esp_http_client_perform(client) == ESP_OK){
        //OK
    }else{
        ESP_LOGE(SPOTIFY_TAG, "Error asking for %s", (is_playing) ? "PAUSE" : "PLAY");
    }

    //free(auth_value);
    esp_http_client_cleanup(client);

    vTaskDelete(NULL);
}


void spotify_action_play_pause(void){
    xTaskCreate(task_play_pause, "play_pause", 4096, &(_spotify_info->is_playing), 4, NULL);
}


void task_skip_previous(void *pvParam){
    esp_http_client_config_t config = {
        .url = SPOTIFY_GET_API_URL(SPOTIFY_PLAYER_REFERENCE, SPOTIFY_SKIP_TO_PREVIOUS_ENDPOINT),
        .method = HTTP_METHOD_POST,
        //.event_handler = my_http_event_handler,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size_tx = 1024,
    };

    //spotify_info *_info = (spotify_info*)pvParam;

    esp_http_client_handle_t client = esp_http_client_init(&config);

    // char *auth_value = malloc(SPOTIFY_TOKEN_LEN + strlen("Bearer "));
    // sprintf(auth_value, "Bearer %s", _spotify_token);

    esp_http_client_set_header(client, SPOTIFY_AUTH_KEY, _spotify_bearer_auth);

    // char *post_data = malloc(512);
    // int written = sprintf(post_data, "device_id=%s", _info->device_id);
    // esp_http_client_set_post_field(client, post_data, written);

    if(esp_http_client_perform(client) == ESP_OK){
        //OK
    }else{
        ESP_LOGE(SPOTIFY_TAG, "Error sending skip to previous request");
    }

    //free(auth_value);
    esp_http_client_cleanup(client);

    vTaskDelete(NULL);
}


void spotify_action_skip_previous(void){
    xTaskCreate(task_skip_previous, "skip_previous", 4096, NULL, 4, NULL);
}


void task_add_currently_playing_to_favourite(void *pvParam){
    esp_http_client_config_t config = {
        .url = SPOTIFY_GET_API_URL( SPOTIFY_TRACKS_REFERENCE),
        .method = HTTP_METHOD_PUT,
        //.event_handler = my_http_event_handler,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size_tx = 1024,
    };

    //spotify_info *_info = (spotify_info*)pvParam;

    esp_http_client_handle_t client = esp_http_client_init(&config);

    // char *auth_value = malloc(SPOTIFY_TOKEN_LEN + strlen("Bearer "));
    // sprintf(auth_value, "Bearer %s", _spotify_token);

    esp_http_client_set_header(client, SPOTIFY_AUTH_KEY, _spotify_bearer_auth);

    esp_http_client_set_header(client, "Content-Type", "application/json");

    cJSON *root = cJSON_CreateObject();
    cJSON *ids_array = cJSON_AddArrayToObject(root, "ids");

    char *track_id = NULL;
    switch(_spotify_config.info_type){
        case SPOTIFY_INFO:{
            track_id = _spotify_info->item->id;
            break;
        }
        case SPOTIFY_INFO_BASE:{
            track_id = _spotify_info_base->song_id;
            break;
        }
        default:
            break;
    }

    cJSON_AddItemToArray(ids_array, cJSON_CreateString(track_id));
    char *json_body = cJSON_Print(root);
    int json_body_len = strlen(json_body);
    
    esp_http_client_write(client, json_body, json_body_len);

    // char *post_data = malloc(512);
    // int written = sprintf(post_data, "device_id=%s", _info->device_id);
    // esp_http_client_set_post_field(client, post_data, written);

    if(esp_http_client_perform(client) == ESP_OK){
        //OK
    }else{
        ESP_LOGE(SPOTIFY_TAG, "Error sending skip to previous request");
    }

    free(json_body);
    cJSON_Delete(root);
    esp_http_client_cleanup(client);

    vTaskDelete(NULL);
}


void spotify_add_currently_playing_to_favourite(void){

    char *item_id = NULL;
    switch (_spotify_config.info_type){
        case SPOTIFY_INFO:{
            item_id = _spotify_info->item->id;
            break;
        }
        
        case SPOTIFY_INFO_BASE:{
            item_id = _spotify_info_base->song_id;
            break;
        }
        default:
            break;
    }

    xTaskCreate(task_add_currently_playing_to_favourite, "add_to_favourite", 4096, item_id, 4, NULL);
}


void task_set_playback_volume(void *pvParam){
    uint8_t volume_value = *((uint8_t*) pvParam);

    esp_http_client_config_t config = {
        .url = SPOTIFY_GET_API_URL( SPOTIFY_PLAYER_REFERENCE, SPOTIFY_SET_PLAYBACK_VOLUME_ENDPOINT),
        .method = HTTP_METHOD_PUT,
        //.event_handler = my_http_event_handler,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size_tx = 1024,
    };

    //spotify_info *_info = (spotify_info*)pvParam;

    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_http_client_set_header(client, SPOTIFY_AUTH_KEY, _spotify_bearer_auth);

    char *url_request = calloc(1, 512);

    sprintf(url_request, "%s?volume_percent=%u", SPOTIFY_GET_API_URL(SPOTIFY_PLAYER_REFERENCE, SPOTIFY_SET_PLAYBACK_VOLUME_ENDPOINT), volume_value);

    esp_http_client_set_url(client, url_request);

    if(esp_http_client_perform(client) == ESP_OK){
        //OK
    }else{
        ESP_LOGE(SPOTIFY_TAG, "Error sending volume request");
    }

    free(url_request);
    esp_http_client_cleanup(client);
    free(pvParam);      // The parameter passed was stored in memoty through a calloc in order to be accessible inside this task. Free the memory it used

    vTaskDelete(NULL);
}


void spotify_action_set_playback_volume(uint8_t volume_percent){
    uint8_t new_volume_value = volume_percent;
    if(volume_percent > 100) new_volume_value = 100;

    uint8_t *volume_mem = calloc(1, sizeof(uint8_t));
    memcpy(volume_mem, &new_volume_value, 1);

    xTaskCreate(task_set_playback_volume, "set_playback_volume", 4096, volume_mem, 4, NULL);
}


void spotify_action_increase_playback_volume(uint8_t volume_increase_amount){

    //get current volume value
    uint8_t current_volume_val = 0;
    switch(_spotify_config.info_type){
        case SPOTIFY_INFO:{
            current_volume_val = _spotify_info->device->volume_percent;
            break;
        }
        case SPOTIFY_INFO_BASE:{
            //CURRENTLY NOT SUPPORTED WITH BASE INFO, TODO: ADD!!
            break;
        }
        default:
            break;
    }

    uint8_t new_volume_value = current_volume_val;
    if(new_volume_value + volume_increase_amount <= 100) new_volume_value += volume_increase_amount;
    else new_volume_value = 100;

    uint8_t *volume_mem = calloc(1, sizeof(uint8_t));
    memcpy(volume_mem, &new_volume_value, 1);

    xTaskCreate( task_set_playback_volume, "set_playback_volume", 4096, volume_mem, 4, NULL);
}


void spotify_action_decrease_playback_volume(uint8_t volume_decrease_amount){
    //get current volume value
    uint8_t current_volume_val = 0;
    switch(_spotify_config.info_type){
        case SPOTIFY_INFO:{
            current_volume_val = _spotify_info->device->volume_percent;
            break;
        }
        case SPOTIFY_INFO_BASE:{
            //CURRENTLY NOT SUPPORTED WITH BASE INFO, TODO: ADD!!
            break;
        }
        default:
            break;
    }

    uint8_t new_volume_value = current_volume_val;
    if(new_volume_value - volume_decrease_amount >= 0) new_volume_value -= volume_decrease_amount;
    else new_volume_value = 0;

    uint8_t *volume_mem = calloc(1, sizeof(uint8_t));
    memcpy(volume_mem, &new_volume_value, 1);

    xTaskCreate( task_set_playback_volume, "set_playback_volume", 4096, volume_mem, 4, NULL);
}

void task_set_repeat_mode(void *pvParam){

    uint8_t repeat_mode_id = *((uint8_t*) pvParam);

    esp_http_client_config_t config = {
        .url = SPOTIFY_GET_API_URL(SPOTIFY_PLAYER_REFERENCE, SPOTIFY_SET_REPEAT_MODE_ENDPOINT),
        .method = HTTP_METHOD_PUT,
        //.event_handler = my_http_event_handler,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size_tx = 1024,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_http_client_set_header(client, SPOTIFY_AUTH_KEY, _spotify_bearer_auth);

    char *url_request = calloc(1, 512);
    sprintf(url_request, "%s?state=%s",
        SPOTIFY_GET_API_URL(SPOTIFY_PLAYER_REFERENCE, SPOTIFY_SET_REPEAT_MODE_ENDPOINT),
        SPOTIFY_GET_REPEAT_MODE_NAME(repeat_mode_id)
    );
    esp_http_client_set_url(client, url_request);

    if(esp_http_client_perform(client) == ESP_OK){
        //OK
    }else{
        ESP_LOGE(SPOTIFY_TAG, "Failed asking for set repeat mode");
    }

    free(url_request);
    free(pvParam);

    esp_http_client_cleanup(client);

    vTaskDelete(NULL);
}

void spotify_action_set_repeat_mode(uint8_t repeat_mode){
    
    uint8_t *repeat_mode_mem = calloc(1, sizeof(uint8_t));
    if(repeat_mode == SPOTIFY_SET_REPEAT_MODE_AUTO){

        // find what is the id on the current repeat mode and set it to the next one
        if(strcmp( _spotify_info->repeat_state, "off") == 0) *repeat_mode_mem = 1;              //mode off corresponds to id 0, so set it to the next that is mode 1 = context
        else if(strcmp(_spotify_info->repeat_state, "context") == 0) *repeat_mode_mem = 2;
        else if(strcmp(_spotify_info->repeat_state, "track") == 0) *repeat_mode_mem = 0;
        else *repeat_mode_mem = 0;

    }else{
        *(repeat_mode_mem) = repeat_mode;
    }

    xTaskCreate(
        task_set_repeat_mode,
        "set_repeat_mode",
        4096,
        repeat_mode_mem,
        4,
        NULL
    );
}

void task_toggle_shuffle(void *pvParam){
    
    uint8_t shuffle_state = *((uint8_t*)pvParam);

    esp_http_client_config_t config = {
        .url = SPOTIFY_GET_API_URL(SPOTIFY_PLAYER_REFERENCE, SPOTIFY_TOGGLE_PLAYBACK_SHUFFLE_ENDPOINT),
        .method = HTTP_METHOD_PUT,
        //.event_handler = my_http_event_handler,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size_tx = 1024,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    char *url_request = calloc(1, 512);
    sprintf(url_request, "%s?state=%s", SPOTIFY_GET_API_URL(SPOTIFY_PLAYER_REFERENCE, SPOTIFY_TOGGLE_PLAYBACK_SHUFFLE_ENDPOINT), (shuffle_state) ? "true" : "false");
    esp_http_client_set_url(client, url_request);

    esp_http_client_set_header(client, SPOTIFY_AUTH_KEY, _spotify_bearer_auth);

    if(esp_http_client_perform(client) == ESP_OK){
        //OK
    }else{
        ESP_LOGE(SPOTIFY_TAG, "Failed asking shuffle toggle");
    }

    free(url_request);
    free(pvParam);
    esp_http_client_cleanup(client);
    vTaskDelete(NULL);
}

void spotify_action_toggle_playback_shuffle(uint8_t shuffle){

    uint8_t *shuffle_state_mem = calloc(1, sizeof(uint8_t));
    if(shuffle == SPOTIFY_TOGGLE_SHUFFLE_AUTO){
        *(shuffle_state_mem) = !(_spotify_info->shuffle_state);
    }else{
        *(shuffle_state_mem) = shuffle;
    }

    xTaskCreate(
        task_toggle_shuffle,
        "toggle_shuffle", 
        4096,
        shuffle_state_mem,
        4, 
        NULL
    );
}


void allocate_string(char **location, char *value_to_copy){
    //if location is not NULL
    if(*location != NULL ) {
        //if the value to copy is different from the one already present
        if(strcmp(*location, value_to_copy) != 0) free(*location);
        else return; //the value to copy is the same, no need to copy again 
    }
    int len = strlen(value_to_copy);
    *location = calloc( 1, len+1);
    if(*location == NULL) ESP_LOGE(SPOTIFY_TAG, "ERROR ALLOCATING MEMORY FOR STRING %s", value_to_copy);
    strncpy(*location, value_to_copy, len+1);
}


void parse_image_array(SpotifyImage_t **images, uint8_t images_count, cJSON *image_array){

    for(uint8_t i=0; i<images_count; i++){

        cJSON *image_json = cJSON_GetArrayItem(image_array, i);
        cJSON *url = cJSON_GetObjectItem(image_json, "url");
        cJSON *height = cJSON_GetObjectItem(image_json, "height");
        cJSON *width = cJSON_GetObjectItem(image_json, "width");

        if(url != NULL) allocate_string(&(images[i]->url), url->valuestring);
        if(height != NULL) images[i]->height = height->valueint;
        if(width != NULL) images[i]->width = width->valueint;
    }
}


void parse_playback_state_response(char *response){

    if(response == NULL){
        ESP_LOGE(SPOTIFY_TAG, "Response is empty!!");
        return;
    }

    cJSON *root = cJSON_Parse(response);

    if(root == NULL) {
        cJSON_Delete(root);
        return;
    }
    cJSON *device = cJSON_GetObjectItem(root, "device");
    cJSON *item = cJSON_GetObjectItem(root, "item");


    cJSON *repeat_state = cJSON_GetObjectItem(root, "repeat_state");
    cJSON *shuffle_state = cJSON_GetObjectItem(root, "shuffle_state");
    cJSON *timestamp = cJSON_GetObjectItem(root, "timestamp");
    cJSON *progress_ms = cJSON_GetObjectItem(root, "progress_ms");
    cJSON *is_playing = cJSON_GetObjectItem(root, "is_playing");
    
    if(repeat_state != NULL) allocate_string(&(_spotify_info->repeat_state), repeat_state->valuestring);
    if(shuffle_state != NULL) _spotify_info->shuffle_state = cJSON_IsTrue(shuffle_state) ? 1 : 0;
    if(timestamp != NULL) _spotify_info->timestamp = timestamp->valueint;
    if(progress_ms != NULL) _spotify_info->progress_ms = progress_ms->valueint;
    if(is_playing != NULL) _spotify_info->is_playing = cJSON_IsTrue(is_playing) ? 1 : 0;

    //fast check to see if item being played is same or not to save time
    if(item != NULL){
        cJSON *_tmp_item_id = cJSON_GetObjectItem(item, "id");
        if(_tmp_item_id != NULL){
            // the song being played is the same, there is no need to update all the fields
            if(_spotify_info->item->id != NULL){
                if(strcmp(_spotify_info->item->id, _tmp_item_id->valuestring) == 0) {

                    /// TODO: CHECK FIELDS THAT NEED TO UPDATED ALWAYS, CAN BE FIELDS THAT CAN BE CHANGED THROUGH ACTIONS
                    if(device != NULL){
                        cJSON *_tmp_volume_percent = cJSON_GetObjectItem(device, "volume_percent");
                        if(_tmp_volume_percent != NULL) _spotify_info->device->volume_percent = _tmp_volume_percent->valueint;
                    }
                    cJSON_Delete(root);
                    return;
                }
            }
        }
    }

    if(device != NULL){
        cJSON *id = cJSON_GetObjectItem(device, "id");
        cJSON *is_active = cJSON_GetObjectItem(device, "is_active");
        cJSON *is_private_session = cJSON_GetObjectItem(device, "is_private_session");
        cJSON *is_restricted = cJSON_GetObjectItem(device, "is_restricted");
        cJSON *name = cJSON_GetObjectItem(device, "name");
        cJSON *type = cJSON_GetObjectItem(device, "type");
        cJSON *volume_percent = cJSON_GetObjectItem(device, "volume_percent");

        if(id != NULL) allocate_string(&(_spotify_info->device->id), id->valuestring);
        if(is_active != NULL) _spotify_info->device->is_active = cJSON_IsTrue(is_active) ? 1 : 0;
        if(is_private_session != NULL) _spotify_info->device->is_private_session = cJSON_IsTrue(is_private_session) ? 1 : 0;
        if(is_restricted != NULL) _spotify_info->device->is_restricted = cJSON_IsTrue(is_restricted) ? 1 : 0;
        if(name != NULL) allocate_string(&(_spotify_info->device->name), name->valuestring);
        if(type != NULL) allocate_string(&(_spotify_info->device->type), type->valuestring);
        if(volume_percent != NULL) _spotify_info->device->volume_percent = volume_percent->valueint;
    }

    if(item != NULL){
        cJSON *album = cJSON_GetObjectItem(item, "album");
        cJSON *artists = cJSON_GetObjectItem(item, "artists");
        cJSON *duration_ms = cJSON_GetObjectItem(item, "duration_ms");
        cJSON *explicit = cJSON_GetObjectItem(item, "explicit");
        cJSON *id = cJSON_GetObjectItem(item, "id");
        cJSON *name = cJSON_GetObjectItem(item, "name");
        cJSON *popularity = cJSON_GetObjectItem(item, "popularity");
        cJSON *track_number = cJSON_GetObjectItem(item, "track_number");

        if(album != NULL){

            cJSON *total_tracks = cJSON_GetObjectItem(album, "total_tracks");
            cJSON *album_id = cJSON_GetObjectItem(album, "id");
            cJSON *images = cJSON_GetObjectItem(album, "images");
            cJSON *album_name = cJSON_GetObjectItem(album, "name");
            cJSON *release_date = cJSON_GetObjectItem(album, "release_date");
            cJSON *album_popularity = cJSON_GetObjectItem(album, "popularity");

            if( total_tracks != NULL) _spotify_info->item->album->total_tracks = total_tracks->valueint;
            if( album_id != NULL) allocate_string(&(_spotify_info->item->album->id), album_id->valuestring);
            if( images != NULL){

                int image_count = cJSON_GetArraySize(images);
                if(_spotify_info->item->album->images_count != image_count){
                    //memory needs to be allocated
                    if(_spotify_info->item->album->images != NULL){
                        for(uint8_t i = 0; i<_spotify_info->item->album->images_count; i++){
                            free(_spotify_info->item->album->images[i]->url);
                            free(_spotify_info->item->album->images[i]);
                        }
                        free(_spotify_info->item->album->images);
                    }
                    
                    _spotify_info->item->album->images = calloc( image_count, sizeof(SpotifyImage_t*));

                    for(uint8_t i = 0; i<image_count; i++){
                        _spotify_info->item->album->images[i] = calloc(1, sizeof(SpotifyImage_t));
                    }
                    
                    _spotify_info->item->album->images_count = image_count;
                }

                parse_image_array(_spotify_info->item->album->images, image_count, images);
            }

            if(album_name != NULL) allocate_string(&(_spotify_info->item->album->name), album_name->valuestring);
            if(release_date != NULL) allocate_string(&(_spotify_info->item->album->release_date), release_date->valuestring);
            if(album_popularity != NULL) _spotify_info->item->album->popularity = album_popularity->valueint;
        }

        if(artists != NULL){

            int artist_count = cJSON_GetArraySize(artists);

            //free only if size is different, with subsequent calloc
            if (_spotify_info->item->artists_count != artist_count){
            
                if(_spotify_info->item->artists != NULL) {
                    for( uint8_t i = 0; i<_spotify_info->item->artists_count; i++){

                        //free artist images
                        for(uint8_t j = 0; i<_spotify_info->item->artists[i]->images_count; j++){
                            free(_spotify_info->item->artists[i]->images[j]->url);
                            free(_spotify_info->item->artists[i]->images[j]);
                        }
                        free(_spotify_info->item->artists[i]->images);
                        free(_spotify_info->item->artists[i]->name);
                        free(_spotify_info->item->artists[i]);
                    }
                    
                    free(_spotify_info->item->artists);
                }
                _spotify_info->item->artists = calloc( artist_count, sizeof(SpotifyArtist_t*));

                for(uint8_t i = 0; i<artist_count; i++){
                    _spotify_info->item->artists[i] = calloc(1, sizeof(SpotifyArtist_t));
                    _spotify_info->item->artists[i]->images_count = 0;
                }
                _spotify_info->item->artists_count = artist_count;
            }
            
            for(uint8_t i = 0; i<artist_count; i++){

                cJSON *artist = cJSON_GetArrayItem(artists, i);

                cJSON *artist_images = cJSON_GetObjectItem(artist, "images");
                cJSON *artist_name = cJSON_GetObjectItem(artist, "name");

                if(artist_images != NULL) {
                    
                    int image_count = cJSON_GetArraySize(artist_images);
                    
                    if(_spotify_info->item->artists[i]->images_count != image_count){

                        if(_spotify_info->item->artists[i]->images != NULL){
                            
                            for(uint8_t j = 0; j<_spotify_info->item->artists[i]->images_count; j++){
                                free(_spotify_info->item->artists[i]->images[j]->url);
                                free(_spotify_info->item->artists[i]->images[j]);
                            }
                            free(_spotify_info->item->artists[i]->images);
                        }

                        _spotify_info->item->artists[i]->images = calloc( image_count, sizeof(SpotifyImage_t*));

                        for(uint8_t j = 0; j<image_count; j++){
                            _spotify_info->item->artists[i]->images[j] = calloc(1, sizeof(SpotifyImage_t));
                        }
                        _spotify_info->item->artists[i]->images_count = image_count;
                    }

                    parse_image_array(_spotify_info->item->artists[i]->images, image_count, artist_images);
                }
                
                if(artist_name != NULL) allocate_string(&(_spotify_info->item->artists[i]->name), artist_name->valuestring);
            }
        }

        if(duration_ms != NULL) _spotify_info->item->duration_ms = duration_ms->valueint;
        if(explicit != NULL) _spotify_info->item->explicit = cJSON_IsTrue(explicit) ? 1 : 0;
        if(id != NULL) allocate_string(&(_spotify_info->item->id), id->valuestring);
        if(name != NULL) allocate_string(&(_spotify_info->item->name), name->valuestring);
        if(popularity != NULL) _spotify_info->item->popularity = popularity->valueint;
        if(track_number != NULL) _spotify_info->item->track_number = track_number->valueint;
    }

    cJSON_Delete(root);
}


void parse_playback_state_response_base(char *response){

    if(response == NULL) return;

    cJSON *root = cJSON_Parse(response);
    if(root == NULL) return;

    cJSON *item = cJSON_GetObjectItem(root, "item");
    cJSON *device = cJSON_GetObjectItem(root, "device");
    cJSON *progress_ms = cJSON_GetObjectItem(root, "progress_ms");
    cJSON *is_playing = cJSON_GetObjectItem(root, "is_playing");

    if(progress_ms != NULL) _spotify_info_base->progress_ms = progress_ms->valueint;
    if(is_playing != NULL) _spotify_info_base->is_playing = cJSON_IsTrue(is_playing) ? 1 : 0;

    if(item != NULL){
        cJSON *tmp_song_id = cJSON_GetObjectItem(item, "id");

        if(tmp_song_id != NULL){
            if(_spotify_info_base->song_id != NULL){
                if(strcmp(_spotify_info_base->song_id, tmp_song_id->valuestring) == 0){
                    //track being played is the same, no need to update all the data
                    cJSON_Delete(root);
                    return;
                }
            }
        }
    }

    if(device != NULL){
        cJSON *device_id = cJSON_GetObjectItem(device, "id");
        cJSON *device_name = cJSON_GetObjectItem(device, "name");

        if(device_id != NULL) allocate_string(&(_spotify_info_base->device_id), device_id->valuestring);
        if(device_name != NULL) allocate_string(&(_spotify_info_base->device_name), device_name->valuestring);
    }

    if(item != NULL){
        cJSON *album = cJSON_GetObjectItem(item, "album");
        cJSON *song_id = cJSON_GetObjectItem(item, "id");
        cJSON *song_name = cJSON_GetObjectItem(item, "name");
        cJSON *duration_ms = cJSON_GetObjectItem(item, "duration_ms");
        cJSON *artists = cJSON_GetObjectItem(item, "artists");

        if(song_id != NULL) allocate_string(&(_spotify_info_base->song_id), song_id->valuestring);
        if(song_name != NULL) allocate_string(&(_spotify_info_base->song_name), song_name->valuestring);
        if(duration_ms != NULL) _spotify_info_base->duration_ms = duration_ms->valueint;

        if(album != NULL){

            cJSON *album_name = cJSON_GetObjectItem(album, "name");
            cJSON *album_images = cJSON_GetObjectItem(album, "images");

            if(album_name != NULL) allocate_string(&(_spotify_info_base->album_name), album_name->valuestring);

            if(album_images != NULL){

                //get the last three since the image are sent as biggest first
                int image_count = cJSON_GetArraySize(album_images);

                if(_spotify_info_base->images_count != image_count){

                    if(_spotify_info_base != NULL){

                        for(uint8_t i = 0; i< _spotify_info_base->images_count; i++){
                            free(_spotify_info_base->images[i]->url);
                            free(_spotify_info_base->images[i]);
                        }

                        free(_spotify_info_base->images);
                    }

                    _spotify_info_base->images = calloc(image_count, sizeof(SpotifyImage_t*));

                    for(uint8_t i = 0; i<image_count; i++){
                        _spotify_info_base->images[i] = calloc(1, sizeof(SpotifyImage_t));
                    }

                    _spotify_info_base->images_count = image_count;
                }

                parse_image_array(_spotify_info_base->images, image_count, album_images);
            }
        }
        if(artists != NULL){

            int artists_count = cJSON_GetArraySize(artists);

            if(_spotify_info_base->artists_count != artists_count){

                if(_spotify_info_base->artists_names != NULL){

                    for(uint8_t i = 0; i<_spotify_info_base->artists_count; i++){
                        free(_spotify_info_base->artists_names[i]);
                    }
                    free(_spotify_info_base->artists_names);
                }

                _spotify_info_base->artists_names = calloc(artists_count, sizeof(char*));

                _spotify_info_base->artists_count = artists_count;
            }

            for(uint8_t i=0; i<artists_count; i++){
                allocate_string(&(_spotify_info_base->artists_names[i]), cJSON_GetArrayItem(artists, i)->valuestring);
            }
        }
    }

    cJSON_Delete(root);

}


void update_playback_state(void *pvParam){

    esp_http_client_config_t config = {
        .url = SPOTIFY_GET_API_URL(SPOTIFY_PLAYER_REFERENCE),
        .method = HTTP_METHOD_GET,
        .event_handler = my_http_event_handler,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 2048,
    };

    //Wait "forever" the first time we wait for the token MAYBE A SEMPAHORE WOULD BE MORE APPROPRIATE NOW?
    while(xQueueReceive(xTokenQueue, _spotify_token, 1000 / portTICK_PERIOD_MS) != ESP_OK){
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }

    int prev_is_playing = 0;
    char prev_track_id[20]; //the ID is much shorter than 20, but allow for some overhead

    for(;;){

        esp_http_client_handle_t client = esp_http_client_init(&config);

        if(xQueueReceive(xTokenQueue, _spotify_token, 0) == pdTRUE){
            //new token available, upadte local representation of authentication header
            //sprintf(auth_value, "Bearer %s", _spotify_token);
        }

        esp_http_client_set_header(client, SPOTIFY_AUTH_KEY, _spotify_bearer_auth);

        if(esp_http_client_perform(client) == ESP_OK){

            switch (_spotify_config.info_type){
                case SPOTIFY_INFO:{

                    parse_playback_state_response(api_response);

                    if( _spotify_config.on_song_change_cb != NULL && _spotify_info->item->id != NULL && strcmp(prev_track_id, _spotify_info->item->id) != 0){
                        _spotify_config.on_song_change_cb(_spotify_info);
                        strcpy(prev_track_id, _spotify_info->item->id);
                    }

                    if( _spotify_config.on_play_pause_change_cb != NULL && prev_is_playing != _spotify_info->is_playing){
                        prev_is_playing = !prev_is_playing; 
                        _spotify_config.on_play_pause_change_cb(prev_is_playing);
                    }

                    break;
                }

                case SPOTIFY_INFO_BASE:{

                    parse_playback_state_response_base(api_response);

                    if(_spotify_config.on_song_change_base_cb != NULL && strcmp(prev_track_id, _spotify_info_base->song_id) != 0){
                        _spotify_config.on_song_change_base_cb(_spotify_info_base);
                        strcpy(prev_track_id, _spotify_info_base->song_id);
                    }

                    if(_spotify_config.on_play_pause_change_cb != NULL && prev_is_playing != _spotify_info_base->is_playing){
                        prev_is_playing = !prev_is_playing;
                        _spotify_config.on_play_pause_change_cb(prev_is_playing);
                    }
                    
                    break;
                }

                default:
                    break;
            }

            //this condition, takes approximately 16 milliseconds to process, the outer loop is much slower than the parsing
            /// TODO: find fix!!
            
        }
        esp_http_client_cleanup(client);
        
        vTaskDelay( _spotify_config.update_delay_ms / portTICK_PERIOD_MS);
    }

    //free(auth_value);
}


esp_err_t spotify_init(const char *spotify_client_id, const char *spotify_client_secret, const char *spotify_refresh_token, uint8_t info_type){

    if(spotify_client_id == NULL || spotify_client_secret == NULL || spotify_refresh_token == NULL){
        ESP_LOGE(SPOTIFY_TAG, "NULL POINTER REFERENCE TO PARAMETERS");
        return ESP_FAIL;
    }

    _spotify_client_id = spotify_client_id;
    _spotify_client_secret = spotify_client_secret;
    _spotify_refresh_token = spotify_refresh_token;

    _spotify_config.info_type = info_type;
    
    return ESP_OK;
}


esp_err_t spotify_init_config(const char *spotify_client_id, const char *spotify_client_secret, const char *spotify_refresh_token, SpotifyConfig_t *config){

    if(config == NULL) return ESP_FAIL;

    _spotify_config.on_song_change_cb = config->on_song_change_cb;
    _spotify_config.on_song_change_base_cb = config->on_song_change_base_cb;
    _spotify_config.on_play_pause_change_cb = config->on_play_pause_change_cb;
    _spotify_config.update_delay_ms = config->update_delay_ms;

    return spotify_init(spotify_client_id, spotify_client_secret, spotify_refresh_token, config->info_type);
}


esp_err_t spotify_add_on_song_change_cb(OnSongChange user_on_song_change_cb){
    _spotify_config.on_song_change_cb = user_on_song_change_cb;
    return ESP_OK;
}


esp_err_t spotify_add_on_song_change_base_cb(OnSongChangeBase user_on_song_change_base_cb){
    _spotify_config.on_song_change_base_cb = user_on_song_change_base_cb;
    return ESP_OK;
}


esp_err_t spotify_add_on_play_pause_change_cb(OnPlayPauseChange user_on_play_pause_change_cb){
    _spotify_config.on_play_pause_change_cb = user_on_play_pause_change_cb;
    return ESP_OK;
}


esp_err_t spotify_set_update_delay(uint32_t delay_ms){
    _spotify_config.update_delay_ms = delay_ms;
    return ESP_OK;
}


esp_err_t spotify_start(void){
    switch (_spotify_config.info_type){
        case SPOTIFY_INFO:{
            _spotify_info = calloc( 1, sizeof( SpotifyInfo_t));
            if(_spotify_info == NULL){
                ESP_LOGE(SPOTIFY_TAG, "Failed allocating memory to SPOTIFY_INFO");
                return ESP_FAIL;
            }

            _spotify_info->device = calloc( 1, sizeof( SpotifyDevice_t));
            _spotify_info->item = calloc( 1, sizeof( SpotifyTrackObject_t));
            if(_spotify_info->item == NULL || _spotify_info->device == NULL){
                ESP_LOGE(SPOTIFY_TAG, "Failed allocating memory to SPOTIFY_INFO (DEVICE|ITEM)");
                return ESP_FAIL;
            }
            _spotify_info->item->album = calloc( 1, sizeof(SpotifyAlbum_t));
            if(_spotify_info->item->album == NULL){
                ESP_LOGE(SPOTIFY_TAG, "Failed allocating memory for SPOTIFY_INFO (ALBUM)");
                return ESP_FAIL;
            }
            break;
        }
        case SPOTIFY_INFO_BASE:{
            /// TODO: ALLOCATE MEMORY FOR SpotifyInfoBase_t
            _spotify_info_base = calloc(1, sizeof(SpotifyInfoBase_t));
            if(_spotify_info_base == NULL){
                ESP_LOGE(SPOTIFY_TAG, "Failed allocating memory to SPOTIFY_INFO_BASE");
                return ESP_FAIL;
            }
            break;
        }
        default:
            return ESP_FAIL;
            break;
    }

    if(_spotify_config.update_delay_ms == 0) _spotify_config.update_delay_ms = SPOTIFY_DEFAULT_UPDATE_DELAY_MS;

    xTokenQueue = xQueueCreate(TOKEN_QUEUE_LEN, TOKEN_QUEUE_ITEM_SIZE);
    xRefreshTokenSemaphore = xSemaphoreCreateBinary();
    xSemaphoreGive(xRefreshTokenSemaphore);

    xRefreshTokenTimer = xTimerCreate(
        "timer", 
        SPOTIFY_TOKEN_REFRESH_TIMER_PERIOD_TICKS(portTICK_PERIOD_MS), 
        pdTRUE, 
        0,
        token_refresh_timer_cb
    );

    if(xRefreshTokenTimer == NULL){
        ESP_LOGE(SPOTIFY_TAG, "Timer creation failed");
        return ESP_FAIL;
    }
    if(xTimerStart(xRefreshTokenTimer, 0) != pdPASS){
        ESP_LOGE(SPOTIFY_TAG, "Timer start failed");
        return ESP_FAIL;
    }

    xTaskCreate(get_new_token, "get new token", 4096, NULL, 2, NULL);
    xTaskCreate(update_playback_state, "now_playing", 8192, NULL, 1, NULL);
    
    return ESP_OK;
}