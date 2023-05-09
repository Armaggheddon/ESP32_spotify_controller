// #ifndef SPOTIFY_FIRST_TIME_H
// #define SPOTIFY_FIRST_TIME_H

// /******************************** REQUIRED TO RETRIEVE THE TOKEN ************************************/

// #define SPOTIFY_REQUEST_SCOPE           "user-read-playback-state%20user-modify-playback-state"
// #define SPOTIFY_REQUEST_CODE_ENDPOINT   "https://accounts.spotify.com/authorize"
// #define SPOTIFY_REQUEST_CODE_QUERY_KEY  "code"

// #define SPOTIFY_REQUEST_CODE_LEN 260

// #define SPOTIFY_REQUEST_TOKEN_POST_TEMPLATE "grant_type=authorization_code&code=%s&redirect_uri=%s"
// #define SPOTIFY_AUTH_WEBPAGE_TEMPLATE "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><meta http-equiv=\"X-UA-Compatible\" content=\"IE=edge\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no\" /></head><body><div><a href=\"https://accounts.spotify.com/authorize?client_id=%s&response_type=code&redirect_uri=%s&scope=%s\">spotify Auth</a></div></body></html>"

// #define SPOTIFY_FIRST_TIME_TAG "SPOTIFY_FIRST_TIME"

// void spotify_first_time_init(const char *spotify_client_id, const char *spotify_client_secret);

// void spotify_first_time_start(char *dns_name, int server_port, char *callback_url);

// #endif // SPOTIFY_FIRST_TIME_H

