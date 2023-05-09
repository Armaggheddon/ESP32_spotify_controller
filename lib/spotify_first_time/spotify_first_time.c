// #include "spotify_first_time.h"

// #include <esp_http_client.h>

// #include <mbedtls/base64.h>

// #include <cJSON.h>

// #include <esp_http_server.h>
// #include <mdns.h>               // REQUIRED FOR DNS, SEE ESP-IDF COMPONENTS ON GITHUB

// //required to make the requests
// char _client_id[128];
// char _client_secret[128];

// char server_redirect_url[256];
// char server_redirect_url_encoded[256];

// char spotify_code_for_token[SPOTIFY_REQUEST_CODE_LEN];

// httpd_uri_t server_index_url;
// httpd_uri_t server_callback_url;

// httpd_handle_t spotify_first_time_server = NULL;

// QueueHandle_t xAuthCodeQueue;

// int url_encode(char* src, int src_size, char*dst, int dst_size){

//     const char *hex = "0123456789ABCDEF";
//     int dst_pos = 0;
//     int curr = 0;

//     while(src[curr] != '\0'){
//         if(
//             ('a' <= src[curr] && src[curr] <= 'z') ||
//             ('A' <= src[curr] && src[curr] <= 'Z') ||
//             ('0' <= src[curr] && src[curr] <= '9') ||
//             src[curr] == '-' ||
//             src[curr] == '_' ||
//             src[curr] == '.' ||
//             src[curr] == '~'
//         ){
//             dst[dst_pos++] = src[curr];
//         } else{
//             dst[dst_pos++] = '%';
//             dst[dst_pos++] = hex[(unsigned char)src[curr] >> 4];
//             dst[dst_pos++] = hex[src[curr] & 0xF];
//         }
//         curr++;
//     }
//     return dst_pos; //written bytes
// }

// esp_err_t spotify_first_time_auth_url_handler(httpd_req_t *req){
//     esp_err_t result = ESP_OK;
//     char *query = NULL;
//     size_t query_len = httpd_req_get_url_query_len(req);
//     if(query_len > 0){

//         query = malloc(query_len);
//         result = httpd_req_get_url_query_str(req, query, query_len);

//         if((result = httpd_query_key_value(query, SPOTIFY_REQUEST_CODE_QUERY_KEY, spotify_code_for_token, SPOTIFY_REQUEST_CODE_LEN)) == ESP_OK){
            
//             // CODE RECEIVED, instantiate request to get refresh token
//             xQueueSend(xAuthCodeQueue, spotify_code_for_token, portMAX_DELAY);
//             if(httpd_resp_send(spotify_first_time_server, spotify_code_for_token, SPOTIFY_REQUEST_CODE_LEN)==ESP_OK){
//                 if(spotify_first_time_server!=NULL) httpd_stop(spotify_first_time_server);
//             }
//         }
//         free(query);
//     }
//     return result;
// }

// void spotify_retrieve_refresh_token(void *pvParam){

//     char* auth_code = malloc(SPOTIFY_REQUEST_CODE_LEN);
//     char* refresh_token = malloc(300);

//     if(xQueueReceive(xAuthCodeQueue, auth_code, portMAX_DELAY) != ESP_OK){
//         ESP_LOGE(SPOTIFY_FIRST_TIME_TAG, "Error receiving authentication code in task to retrieve refresh token");
//     }

//     esp_http_client_config_t config = {
//         .url = SPOTIFY_REQUEST_TOKEN_ENDPOINT,
//         .method = HTTP_METHOD_POST,
//         .transport_type = HTTP_TRANSPORT_OVER_SSL,
//         .crt_bundle_attach = esp_crt_bundle_attach,
//         .keep_alive_enable = true,
//         .buffer_size = 2048,
//     };

//     esp_http_client_handle_t client = esp_http_client_init(&config);
//     esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");

//     unsigned char *buff = malloc(2048);
//     int written = sprintf((char *)buff, "%s:%s", _client_id, _client_secret);
    
//     unsigned char *base64_auth = malloc(2048);
//     size_t base64_auth_len = 0;
//     mbedtls_base64_encode(base64_auth, 2048, &base64_auth_len, buff, written);
//     sprintf((char*) buff, "Basic %s", (char*) base64_auth);
//     esp_http_client_set_header(client, "Authorization", (char*) buff);

//     written = sprintf((char *)buff, SPOTIFY_REQUEST_TOKEN_POST_TEMPLATE, auth_code, server_redirect_url);

//     esp_http_client_set_post_field(client, (char *)buff, written);

//     if (esp_http_client_perform(client) == ESP_OK)
//     {
//         //ESP_LOGI(TAG, "CLIENT_PERFORM_TOKEN_REQUEST");
//         esp_http_client_fetch_headers(client);
//         int cl = esp_http_client_get_content_length(client);
//         //ESP_LOGI(TAG, "HTTP_RESPONSE_CODE: %d cl = %d", esp_http_client_get_status_code(client), cl);
//         if (cl > 0)
//         {
//             int eff_cl = esp_http_client_read_response(client, (char*)buff, 2048);
//             buff[eff_cl] = 0;

//             //ESP_LOGI(TAG, "effective read: %d, data:\n%s", eff_cl, buff);

//             cJSON *root = cJSON_Parse((char*)buff);
//             cJSON *access_token_json = cJSON_GetObjectItem(root, "refresh_token");
//             //cJSON *token_type_json = cJSON_GetObjectItem(root, "token_type");
//             if (access_token_json && access_token_json->valuestring /*&& token_type_json && token_type_json->valuestring*/)
//             {
//                 strncpy(refresh_token, access_token_json->valuestring, SPOTIFY_TOKEN_LEN);
//                 //strncpy(token_type, token_type_json->valuestring, token_type);
//             }
//             cJSON_free(access_token_json);
//             //cJSON_free(token_type_json);
//             cJSON_Delete(root);
//             //TOKEN_RECV = 1;
//         }

//         //ESP_LOGI(TAG, "Access token: %s", SPOTIFY_TOKEN);
//         //ESP_LOGI(TAG, "Token type: %s", token_type);

        
//     }
//     free(buff);
//     free(base64_auth);
//     free(auth_code);
//     esp_http_client_cleanup(client);

//     for(;;){

//         //run indefinetly displaying the refresh token
//         ESP_LOGI(SPOTIFY_TAG, "\t%s", refresh_token);

//         vTaskDelay( 2000 / portTICK_PERIOD_MS);

//     }

// }

// esp_err_t spotify_first_time_index_url_handler(httpd_req_t *req){

//     char *webpage = malloc(1024);
//     int written = sprintf(webpage, SPOTIFY_AUTH_WEBPAGE_TEMPLATE, SPOTIFY_CLIENT_ID, server_redirect_url_encoded, SPOTIFY_REQUEST_SCOPE);
    
//     esp_err_t result = httpd_resp_send(req, webpage, written);
//     free(webpage);

//     return result;
// }

// void spotify_start_first_time_ws(char *redirect_url, int redirect_url_port){

//     server_index_url.uri = "/";
//     server_index_url.method = HTTP_GET;
//     server_index_url.handler = spotify_first_time_index_url_handler;
//     server_index_url.user_ctx = NULL;

//     server_callback_url.uri = redirect_url;
//     server_callback_url.method = HTTP_GET;
//     server_callback_url.handler = spotify_first_time_auth_url_handler;
//     server_callback_url.user_ctx = NULL;

//     httpd_config_t config = HTTPD_DEFAULT_CONFIG();
//     config.server_port = redirect_url_port;

//     if(httpd_start(&spotify_first_time_server, &config) == ESP_OK){
//         if(spotify_first_time_server != NULL){
//             httpd_register_uri_handler(spotify_first_time_server, &server_index_url);
//             httpd_register_uri_handler(spotify_first_time_server, &server_callback_url);
//         }
//     }
// }

// /**
//  * @param dns_host_name name to use for the dns server to allow redirects 
//  *                      without using ESP's IP address, e.g., "esp32", the 
//  *                      webserver advertising the page will then be named
//  *                      as "esp32.local:server_port/" for the homepage
//  * 
//  * @param server_port   the server port number to use
//  * 
//  * @param callback_url  the callback_path to use for the server redirect, 
//  *                      the function automatically bulds the complete path
//  *                      based on the dns_host_name supplied, must be in the
//  *                      form "/<something>...". it must have a "/" as the 
//  *                      first character
//  *                      
// */
// void spotify_first_time_start(const char *dns_host_name, int server_port, const char *callback_url){

//     mdns_init();
//     mdns_hostname_set(dns_host_name);

//     int written = sprintf(server_redirect_url, "http://%s.local:%d%s", dns_host_name, server_port, callback_url);
//     url_encode(server_redirect_url, written, server_redirect_url_encoded, 256);
//     spotify_start_first_time_ws( callback_url, server_port);

//     xAuthCodeQueue = xQueueCreate( 1, SPOTIFY_REQUEST_CODE_LEN); 

//     xTaskCreate(spotify_retrieve_refresh_token, "retrieve refresh token", 4096, NULL, 2, NULL);

// }

// void spotify_first_time_init(const char *spotify_client_id, const char *spotify_client_secret){
//     if(spotify_client_id == NULL || spotify_client_secret == NULL){
//         return;
//     }

//     memcpy(_client_id, spotify_client_id, sizeof(_client_id));
//     memcpy(_client_secret, spotify_client_secret, sizeof(_client_secret));
// }


