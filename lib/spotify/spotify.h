#ifndef SPOTIFY_H
#define SPOTIFY_H

#include <stdlib.h>


#define SPOTIFY_REQUEST_TOKEN_ENDPOINT                              "https://accounts.spotify.com/api/token"
#define SPOTIFY_REQUEST_REFRESH_TOKEN_POST_TEMPLATE                 "grant_type=refresh_token&refresh_token=%s"

#define SPOTIFY_TOKEN_LEN                                           300     // Can be 219 (in theory XD)
#define SPOTIFY_TOKEN_REFRESH_TIMER_INTERVAL_SEC                    3600    // Maximum amount of time a token is valid
#define SPOTIFY_TOKEN_REFRESH_TIMER_OFFSET_SEC                      100     // Offset applied to retrieve the new token before expiration
#define SPOTIFY_TOKEN_REFRESH_TIMER_PERIOD_TICKS(TICK_PERIOD_MS)    (SPOTIFY_TOKEN_REFRESH_TIMER_INTERVAL_SEC - SPOTIFY_TOKEN_REFRESH_TIMER_OFFSET_SEC) *1000 / TICK_PERIOD_MS


/************************************ SPOTIFY ENDPOINTS TO MAKE REQUESTS *************************/
#define SPOTIFY_API                                     "https://api.spotify.com/v1/me"

#define SPOTIFY_PLAYER_REFERENCE                        "/player"

#define SPOTIFY_CURRENTLY_PLAYING_ENDPOINT              "/currently-playing"
#define SPOTIFY_AVAILABLE_DEVICES_ENDPOINT              "/devices"
#define SPOTIFY_PLAY_ENDPOINT                           "/play"
#define SPOTIFY_PAUSE_ENDPOINT                          "/pause"
#define SPOTIFY_SKIP_TO_NEXT_ENDPOINT                   "/next"
#define SPOTIFY_SKIP_TO_PREVIOUS_ENDPOINT               "/previous"
#define SPOTIFY_SET_PLAYBACK_VOLUME_ENDPOINT            "/volume"
#define SPOTIFY_TOGGLE_PLAYBACK_SHUFFLE_ENDPOINT        "/shuffle"
#define SPOTIFY_SET_REPEAT_MODE_ENDPOINT                "/repeat"

#define SPOTIFY_TRACKS_REFERENCE                        "/tracks"

#define SPOTIFY_GET_PLAY_PAUSE_API_URL(IS_PLAYING)      ((IS_PLAYING) ? SPOTIFY_GET_API_URL(SPOTIFY_PLAYER_REFERENCE, SPOTIFY_PAUSE_ENDPOINT) : SPOTIFY_GET_API_URL(SPOTIFY_PLAYER_REFERENCE, SPOTIFY_PLAY_ENDPOINT))
#define SPOTIFY_GET_API_URL(...)                        SPOTIFY_GET_API_URL_BASE(__VA_ARGS__)
#define SPOTIFY_GET_API_URL_BASE(x, ...)                SPOTIFY_API x __VA_ARGS__
#define SPOTIFY_MAX_API_LEN                             7000

#define SPOTIFY_AUTH_KEY                                "Authorization"
#define SPOTIFY_AUTH_FIELD_BEARER_TEMPLATE              "Bearer %s"
#define SPOTIFY_AUTH_FIELD_BASIC_TEMPLATE               "Basic %s"

#define TOKEN_QUEUE_LEN                                 1
#define TOKEN_QUEUE_ITEM_SIZE                           SPOTIFY_TOKEN_LEN 

#define SPOTIFY_DEFAULT_UPDATE_DELAY_MS                 200

#define SPOTIFY_TAG "SPOTIFY"

#define SPOTIFY_GET_REPEAT_STATE(info_type)             (info_type == SPOTIFY_INFO) ? _spotify_info->repeat_state : ((info_type == SPOTIFY_INFO_BASE) ? _spotify_info_base->repeat_state : "off")  
#define SPOTIFY_GET_SHUFFLE_STATE(info_type)            (info_type == SPOTIFY_INFO) ? _spotify_info->shuffle_state : ((info_type == SPOTIFY_INFO_BASE) ? _spotify_info_base->shuffle_state : 0)  
#define SPOTIFY_GET_DEVICE_VOLUME(info_type)            (info_type == SPOTIFY_INFO) ? _spotify_info->device->volume_percent : ((info_type == SPOTIFY_INFO_BASE) ? _spotify_info_base->volume_percent : 50)  
#define SPOTIFY_GET_TRACK_ID(info_type)                 (info_type == SPOTIFY_INFO) ? _spotify_info->item->id : ((info_type == SPOTIFY_INFO_BASE) ? _spotify_info_base->song_id : NULL)
#define SPOTIFY_GET_IMAGES_PTR(info_type)               (info_type == SPOTIFY_INFO) ? _spotify_info->item->album->images : ((info_type == SPOTIFY_INFO_BASE) ? _spotify_info_base->images : NULL)
#define SPOTIFY_GET_IMAGES_COUNT(info_type)             (info_type == SPOTIFY_INFO) ? _spotify_info->item->album->images_count : ((info_type == SPOTIFY_INFO_BASE) ? _spotify_info_base->images_count : 0)


#define SPOTIFY_GET_REPEAT_MODE_NAME(repeat_mode_id)    (repeat_mode_id == SPOTIFY_REPEAT_MODE_OFF) ? "off" : ((repeat_mode_id == SPOTIFY_REPEAT_MODE_CONTEXT) ? "context" : ((repeat_mode_id == SPOTIFY_REPEAT_MODE_TRACK) ? "track" : "off")) 


/******************* BEGIN: DATA STRUCTURES TO HOLD RESPONSE FROM PLAYER ENDPOINT ****************/

#pragma pack(push, 1)
typedef struct{
    char *id;                           /** @brief The device ID, can be NULL */
    uint8_t is_active;                  /** @brief If this device is the currently active device */
    uint8_t is_private_session;         /** @brief If this device is currently in a private session */
    uint8_t is_restricted;              /** @brief Whether controlling this device is restricted. At present if this is "true" then no Web API commands will be accepted by this device */
    char *name;                         /** @brief A human-readable name for the device */
    char *type;                         /** @brief Device type, such as "computer", "smartphone" or "speaker" */
    uint8_t volume_percent;             /** @brief The current volume in percent. Range 0-100 */
}SpotifyDevice_t;
#pragma pack(pop)


#pragma pack(push, 1)
typedef struct {
    char *url;                          /** @brief The source URL of the image */
    uint16_t height;                    /** @brief The image height in pixels */
    uint16_t width;                     /** @brief The image width in pixels */
}SpotifyImage_t;
#pragma pack(pop)


#pragma pack(push, 1)
typedef struct{
    uint8_t total_tracks;               /** @brief The number of tracks in the album */
    char *id;                           /** @brief The Spotify ID for the album */
    SpotifyImage_t **images;            /** @brief The cover art for the album in various sizes, widest first */
    uint8_t images_count;               /** @brief The number of images */
    char *name;                         /** @brief The name of the album. In case of an album takedown, the value may be an empty string */
    char *release_date;                 /** @brief The date the album was first released*/
    uint8_t popularity;                 /** @brief The popularity of the album. The value will be between 0 and 100, with 100 being most popular */
}SpotifyAlbum_t;
#pragma pack(pop)


#pragma pack(push, 1)
typedef struct{
    SpotifyImage_t **images;            /** @brief Images of the artist in various sizes, widest first */
    uint8_t images_count;               /** @brief The number of images */
    char *name;                         /** @brief The name of the artist */
}SpotifyArtist_t;
#pragma pack(pop)


#pragma pack(push, 1)
typedef struct{
    SpotifyAlbum_t *album;              /** @brief The album on which the track appears. The album object includes a link in href for more detailed information about the artists */
    SpotifyArtist_t **artists;          /** @brief The artists who performed the track. Each artist object includes a link in href to more detailed information about the artist */
    uint8_t artists_count;              /** @brief The number of artist object returned */
    uint64_t duration_ms;               /** @brief The track length in milliseconds */
    uint8_t explicit;                   /** @brief Whether or not the track has explicit lirics */
    char *id;                           /** @brief The spotify ID for the track */
    char *name;                         /** @brief The name of the track */
    uint8_t popularity;                 /** @brief The popularity of the track. Range 0-100 */
    uint8_t track_number;               /** @brief The number of the track. If an album has several discr, the track number is the number on the specified disc */
}SpotifyTrackObject_t;
#pragma pack(pop)


#pragma pack(push, 1)
typedef struct {
    SpotifyDevice_t *device;            /** @brief The device that is currently active */
    char *repeat_state;                 /** @brief Can be one of "off", "track", "context" */
    uint8_t shuffle_state;              /** @brief If shuffle is on or off, true or false */
    uint64_t timestamp;                 /** @brief Unix millisecond timestamp when the data was fetched */
    uint64_t progress_ms;               /** @brief Progress into the currently playing track or episode. Can be NULL */
    uint8_t is_playing;                 /** @brief If something is currently playing, return true */
    SpotifyTrackObject_t *item;         /** @brief The currently playing track or episode */
}SpotifyInfo_t;
#pragma pack(pop)


/**
 * Minimal informations about the currently playing track
*/
typedef struct{
    char *device_id;
    char *device_name;
    uint8_t volume_percent;
    uint64_t progress_ms;
    uint8_t is_playing;
    uint8_t shuffle_state;
    char *repeat_state;
    char *album_name;
    SpotifyImage_t **images;
    uint8_t images_count;
    char **artists_names;
    uint8_t artists_count;
    char *song_name;
    char *song_id;
    uint64_t duration_ms;
} SpotifyInfoBase_t;

/***************** END: DATA STRUCTURES TO HOLD RESPONSE FROM PLAYER ENDPOINT ********************/


typedef enum{
    SPOTIFY_INFO = 0,               /** @brief Retrieve the full informations as per SpotifyInfo_t */
    SPOTIFY_INFO_BASE = 1           /** @brief Retrieve a reduced set of informations as per SpotifyInfoBase_t */
} SpotifyInfoType_t;


typedef void (*OnSongChange)( SpotifyInfo_t *);

typedef void (*OnSongChangeBase)(SpotifyInfoBase_t *);

typedef void (*OnPlayPauseChange)(uint8_t);


typedef struct{
    SpotifyInfoType_t info_type;                /** @brief The type of information the user wants to retrieve */
    OnSongChange on_song_change_cb;             /** @brief The user callback function to be called when the song changes to retrieve SpotifyInfo_t */
    OnSongChangeBase on_song_change_base_cb;    /** @brief The user callback function to be called when the song changes to retrieve SpotifyInfoBase_t */
    OnPlayPauseChange on_play_pause_change_cb;  /** @brief The user callback function to be called when the playback state changes to either PAUSE or PLAY */
    uint32_t update_delay_ms;                   /** @brief The delay to be used between requests to the current playback state from the Spotify APIs */
}SpotifyConfig_t;


/**
 * Initializes the library local representation for the Spotify CLIENT_ID, 
 * CLIENT_SECRET and REFRESH_TOKEN. Additionally sets the type of informations
 * that the user wants to retrieve from SpotifyInfoType_t.
 * 
 * @param spotify_client_id         The spotify client ID. See Spotify's developer console.
 * 
 * @param spotify_client_secret     The spotify client secret. See Spotify's developer console.
 * 
 * @param spotify_refresh_token     The spotify refresh token to be used to retrieve a new token.
 *                                  This token is used to get a new token that lasts for 3600 seconds.
 *                                  On expiration, this token is used to retrieve a new valid token.
 *                                  See GitHub page for additional details on how to retrieve the token
 *                                  the first time.
 * 
 * @param info_type                 The information that the user wants to retrieve as per SpotifyInfoType_t. 
 * 
 * @return esp_err_t                Either ESP_OK on success or ESP_FAIL on error.                           
*/
int spotify_init(const char *spotify_client_id, const char *spotify_client_secret, const char *spotify_refresh_token, uint8_t info_type);


/**
 * Wrapper method for spotify_init to initialize all parameters in a single call.
 * 
 * @param spotify_client_id         The spotify client ID. See Spotify's developer console.
 * 
 * @param spotify_client_secret     The spotify client secret. See Spotify's developer console.
 * 
 * @param spotify_refresh_token     The spotify refresh token to be used to retrieve a new token.
 *                                  This token is used to get a new token that lasts for 3600 seconds.
 *                                  On expiration, this token is used to retrieve a new valid token.
 *                                  See GitHub page for additional details on how to retrieve the token
 *                                  the first time.
 * 
 * @param config                    The SpotifyConfig_t with all the settings to be used.
 * 
 * @return esp_err_t                Either ESP_OK on success or ESP_FAIL on error. 
*/
int spotify_init_config(const char *spotify_client_id, const char *spotify_client_secret, const char *spotify_refresh_token, SpotifyConfig_t *config);


/**
 * Allows to add or edit the OnSongChange callback function to be used
 * when the curerntly playing track changes. This parameter is ignored if
 * SpotifyInfoType_t passed to inizialize the library was set to SPOTIFY_INFO_BASE. 
 * 
 * @param user_on_song_change_cb    The user function callback of type OnSongChange defined as
 *                                  void on_song_change(SpoifyInfo_t *info).
 * 
 * @return esp_err_t                Either ESP_OK on success or ESP_FAIL on error.
*/
int spotify_add_on_song_change_cb(OnSongChange user_on_song_change_cb);


/**
 * Allows to add or edit the OnSongChangeBase callback function to be used
 * when the curerntly playing track changes. This parameter is ignored if
 * SpotifyInfoType_t passed to inizialize the library was set to SPOTIFY_INFO. 
 * 
 * @param user_on_song_change_base_cb   The user function callback of type OnSongChange defined as
 *                                      void on_song_change(SpoifyInfoBase_t *info_base).
 * 
 * @return esp_err_t                    Either ESP_OK on success or ESP_FAIL on error.
*/
int spotify_add_on_song_change_base_cb(OnSongChangeBase user_on_song_change_base_cb);


/**
 * Allows to add or edit the OnPlayPauseChange callback function to be used when
 * the <is_playing> field of the response changes from the one retrieved previously.
 * 
 * @param user_on_play_pause_change_cb  The user function callback of type OnPlayPauseChange defined 
 *                                      as void on_play_pause_change(uint8_t is_playing).
 * 
 * @return esp_err_t                    Either ESP_OK on success or ESP_FAIL on error.
*/
int spotify_add_on_play_pause_change_cb(OnPlayPauseChange user_on_play_pause_change_cb);


/**
 * Set or edit the delay to be used in the loop task that retrieves the latest
 * playback state with all the informations requested.
 * 
 * @param delay_ms      The delay in milliseconds that is used to wait for a new 
 *                      request after the data has been retrieved.
 * 
 * @return esp_err_t    Either ESP_OK on success or ESP_FAIL on error.
*/
int spotify_set_update_delay(uint32_t delay_ms);


/**
 * Starts the execution of the tasks to get the latest playback state and to 
 * retrieve the token for the requests. Allocates the required memory based on the 
 * user choice of SpotifyInfoType_t and initializes all required variables.
 * By default calling this function will start two tasks:
 *      -refresh_token: executed at startup and once every 3500 seconds
 *      
 *      -update_playback_state: executed every time with a delay within
 *                              subsequent calls of <update_delay> set 
 *                              by the user or of 200ms if not set. To get
 *                              the latest information from the spotify APIs
 *                              the caller can either use the callback function
 *                              to retrieve the desired information in a local
 *                              structure of poll using spotify_get_last_info_base
 *                              or spotify_get_last_info based on what the desired
 *                              information is and on what it was configured before
 *                              calling spotify_start.
 * 
 * @return esp_err_t    Either ESP_OK on success or ESP_FAIL on error.
*/
int spotify_start(void);


/**
 * Retrieves a pointer to the latest SpotifyInfoBase_t
 * 
 * @returns SpotifyInfoBase_t* pointer to the latest basic 
 *          information about the currently playing track
*/
SpotifyInfoBase_t* spotify_get_last_info_base(void);


/**
 * Retrieves a pointer to the latest SpotifyInfo_t
 * 
 * @returns SpotifyInfo_t* pointer to the latest basic 
 *          information about the currently playing track
*/
SpotifyInfo_t* spotify_get_last_info(void);


/**
 * Skips to the next track in the queue on the currently playing device
*/
void spotify_action_skip_next(void);


/**
 * Pauses or resumes the playback on the currently playing device
*/
void spotify_action_play_pause(void);


/**
 * Skips to the previously playing track on the currently playing device
*/
void spotify_action_skip_previous(void);


/**
 * Adds the currently playing track to the user's "Your Music" library playlist.
 * 
 * @note Requires permission "user-library-modify".
*/
void spotify_add_currently_playing_to_favourite(void);

/**
 * Sets the playback volume on the currently playing device to the value given
 * 
 * @param volume_percent    The percentage value of the volume to set 
 *                          on the currently playing device. If the number
 *                          is greater than 100 the volume will be set to 100.
*/
void spotify_action_set_playback_volume(uint8_t volume_percent);

/**
 * Increases the playback volume on the currently playing device to the value given,
 * effectively emulating a + volume button
 * 
 * @param volume_increase_amount    The amount to add to the current volume
 *                                  on the playing device. This amount represents
 *                                  a percentage value on the total volume, i.e., 
 *                                  if the current volume is 50%, passing 5 will 
 *                                  set the volume to 55%.
*/
void spotify_action_increase_playback_volume(uint8_t volume_increase_amount);

/** 
 * Decreases the playback volume on the currently playing device to the value given,
 * effectively emulating a - volume button
 * 
 * @param volume_increase_amount    The amount to subtract to the current volume
 *                                  on the playing device. This amount represents
 *                                  a percentage value on the total volume, i.e., 
 *                                  if the current volume is 50%, passing 5 will 
 *                                  set the volume to 45%.
*/
void spotify_action_decrease_playback_volume(uint8_t volume_decrease_amount);

typedef enum{
    SPOTIFY_REPEAT_MODE_OFF = 0,
    SPOTIFY_REPEAT_MODE_CONTEXT = 1,
    SPOTIFY_REPEAT_MODE_TRACK = 2,
    SPOTIFY_REPEAT_MODE_AUTO = 3,
}SpotifyRepeatModeEnum_t;
// static const char *spotify_repeat_mode_name[3] = {"track", "context", "off"};

/**
 * Set the repeat mode for the user's playback. Options are
 * defined in SpotifyRepeatModeEnum_t.
 * 
 * @param repeat_mode   The repeat mode to set for the currently
 *                      active device. Accepted values are as per
 *                      SpotifyRepeatModeEnum_t. If SPOTIFY_SET_REPEAT_MODE_AUTO
 *                      is used the request will automatically set the repeat mode
 *                      to the next based on the current one, e.g., if
 *                      the repeat mode is set to off will go to context,
 *                      if called again on track, subsequent calls will loop through
 *                      these as off->context->track.
*/
void spotify_action_set_repeat_mode(uint8_t repeat_mode);


typedef enum{
    SPOTIFY_TOGGLE_SHUFFLE_ON = 0,
    SPOTIFY_TOGGLE_SHUFFLE_OFF = 1,
    SPOTIFY_TOGGLE_SHUFFLE_AUTO = 2,
}SpotifyToggleShuffleEnum_t;
/**
 * Toggle shuffle on or off for user's playback. If the value
 * is previously off it will be turned on and viceversa.
 * 
 * @param shuffle       If true (1) will enable shuffle for the current
 *                      queue, if false (0) will be disabled. Using 
 *                      SPOTIFY_TOGGLE_SHUFFLE_AUTO will set the shuffle 
 *                      mode opposite to the one currently used, e.g., 
 *                      if shuffle is active will be set to off and 
 *                      viceversa.
*/
void spotify_action_toggle_playback_shuffle(uint8_t shuffle);

#pragma pack(push, 1)
typedef struct{
    SpotifyDevice_t **available_devices;            /** @brief Array containing the currently available devices */
    uint8_t available_devices_count;                /** @brief The number of available devices */
}SpotifyAvailableDevices_t;
#pragma pack(pop)

/**
 * Get information about a user's available devices.
 * 
 * @note requires permission "user-read-playback-state"
 * 
 * @return A pointer to SpotifyAvailableDevices_t with the number of
 * available devices and the the devices information themselves. Note that 
 * the memory allocated to store the available devices is not freed, users
 * that want to reuse the object may consider not calling free on this pointer
 * since subsequenc calls will reuse the memory.
*/
SpotifyAvailableDevices_t* spotify_get_available_devices(void);


typedef enum{
    SPOTIFY_ALBUM_ART_SIZE_SMALLEST = 0,
    SPOTIFY_ALBUM_ART_SIZE_64x64 = 64,
    SPOTIFY_ALBUM_ART_SIZE_300x300 = 300,
    SPOTIFY_ALBUM_ART_SIZE_LARGEST
}SpotifyAlbumArtSize_t;

/**
 * Writes in the user img_buffer the album cover image of the currently playing track 
 * of the specified image size.
 * 
 * @param size              The image size to retrieve as per SpotifyAlbumArtSize_t. 
 *                          Images larger than 300x300 will most likely require too 
 *                          much memory to work. Use at your own risk XD.
 * 
 * @param img_buff          User buffer in which the image data will be written.
 * 
 * @param img_buff_size     The size of the user image buffer.
 * 
 * @return int              The number of bytes written, or -1 on error
*/

int spotify_get_album_cover_art_latest(uint16_t size, char *img_buff, int img_buff_size);

/**
 * Similar functionality to spotify_get_album_cover_art_latest, exept that it retrieves the 
 * album art given an url.
 * 
 * @param url           The image url to be retrieved.
 * 
 * @param img_buff      User buffer in which the image data will be written.
 * 
 * @param img_buff_size The size of the user image buffer.
 * 
 * @return esp_err_t        Either ESP_OK on success or ESP_FAIL on error, e.g., image buffer 
 *                          is not big enough to store image data
*/
int spotify_get_album_cover_art_from_url(char *url, char *img_buff, int img_buff_size);


#endif // SPOTIFY_H