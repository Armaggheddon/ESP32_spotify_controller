
#include <stdlib.h>

typedef enum {
    SPOTIFY_REPEAT_STATE_OFF = 0,
    SPOTIFY_REPEAT_STATE_TRACK = 1,
    SPOTIFY_REPEAT_STATE_CONTEXT = 2,
}SpotifyRepeatStateEnum_t;
static const char *spotify_repeat_state_name[] = { "off", "track", "context"};

typedef enum {
    SPOTIFY_CURRENTLY_PLAYING_TYPE_TRACK = 0,
    SPOTIFY_CURRENTLY_PLAYING_TYPE_EPISODE = 1,
    SPOTIFY_CURRENTLY_PLAYING_TYPE_AD = 2,
    SPOTIFY_CURRENTLY_PLAYING_TYPE_UNKNOWN = 3,
}SpotifyCurrentlyPlayingEnum_t;
static const char *spotify_currently_playing_name[] = { "track", "episode", "ad", "unknown"};

typedef enum {
    SPOTIFY_DEVICE_TYPE_COMPUTER = 0,
    SPOTIFY_DEVICE_TYPE_SMARTPHONE = 1,
    SPOTIFY_DEVICE_TYPE_SPEAKER = 2,
}SpotifyDeviceTypeEnum_t;
static const char *spotify_device_type_name[] = {"computer", "smarthphone", "speaker"};

typedef enum {
    SPOTIFY_CONTEXT_TYPE_ARTIST = 0,
    SPOTIFY_CONTEXT_TYPE_PLAYLIST = 1,
    SPOTIFY_CONTEXT_TYPE_ALBUM = 2,
    SPOTIFY_CONTEXT_TYPE_SHOW = 3,
}SpotifyContextTypeEnum_t;
static const char *spotify_context_type_name[] = {"artist", "playlist", "album", "show"};

typedef enum {
    SPOTIFY_ITEM_ALBUM_TYPE_ALBUM = 0,
    SPOTIFY_ITEM_ALBUM_TYPE_SINGLE = 1,
    SPOTIFY_ITEM_ALBUM_TYPE_COMPILATION = 2,
}SpotifyItemAlbumTypeEnum_t;
static const char *spotify_item_album_type_name[] = {"album", "single", "compilation"};

typedef enum {
    SPOTIFY_ITEM_ALBUM_GROUP_ALBUM = 0,
    SPOTIFY_ITEM_ALBUM_GROUP_SINGLE = 1,
    SPOTIFY_ITEM_ALBUM_GROUP_COMPILATION = 2,
    SPOTIFY_ITEM_ALBUM_GROUP_APPEARS_ON = 3,
}SpotifyItemAlbumGroupTypeEnum_t;
static const char *spotify_item_album_type_name[] = {"album", "single", "compilation", "appears_on"};

typedef struct{

    char *id;
    uint8_t is_active;
    uint8_t is_private_session;
    uint8_t is_restricted;
    char *name;
    SpotifyDeviceTypeEnum_t device_type;
    uint8_t volume_percent;
}SpotifyDevice_t;

typedef struct{
    char *spotify;
}SpotifyExternalUrls_t;

typedef struct{

    SpotifyContextTypeEnum_t type;
    char *href;
    SpotifyExternalUrls_t *external_urls;
    char *uri;
}SpotifyContext_t;

typedef struct{
    char *url;
    uint16_t height;
    uint16_t width;
}SpotifyImage_t; // TODO: if used only onece maybe chance in SpotifyItemAlbumImages_t

typedef struct{
    char *text;
    char *type;
}SpotifyCopyrights_t; // TODO: if used only once maybe change in SpotifyItemAlbumCopyrights_t

typedef struct{

    SpotifyExternalUrls_t *external_urls;
    char *href;
    char *id;
    char *name;
    char *type;
    char *uri;

}SpotifyItemAlbumArtists_t;

typedef struct{
    SpotifyItemAlbumTypeEnum_t album_type;
    uint8_t total_tracks;
    // available_markets IGNORED!!
    SpotifyExternalUrls_t *external_urls;
    char *href;
    char* id;
    SpotifyImage_t *images;
    char *name;
    char *release_date;
    char *release_date_precision;
    //restrictions_reason IGNORED!!
    char *type;
    char *uri;
    SpotifyCopyrights_t copyrights;
    //external_ids IGNORED!!
    //genres IGNORED!!
    char *label;
    uint8_t popularity;
    SpotifyItemAlbumGroupTypeEnum_t album_group;
    SpotifyItemAlbumArtists_t *artists;

}SpotifyItemAlbum_t;

typedef struct{

}SpotifyItemArtists_t;

/**
 * @brief This struct represents a TrackObject
*/
typedef struct {
    SpotifyItemAlbum_t *album;
    SpotifyItemArtists_t *artists;
    //available_markets IGNORED!!
    uint8_t disc_number;
    uint64_t duration_ms;
    uint8_t explixit;
    //external_ids IGNORED!!
    char *href;
    char *id;
    uint8_t is_playable;
    //linked_form IGNORED!!
    //restrictions IGNORED!!
    char *name;
    uint8_t popularity;
    //preview_url IGNORED!!
    uint8_t track_number;
    //type IGNORED!!
    char *uri;
    uint8_t is_local;
}SpotifyItem_t;

typedef struct {

    uint8_t interrupt_playback;
    uint8_t pausing;
    uint8_t resuming;
    uint8_t seeking;
    uint8_t skipping_next;
    uint8_t skipping_prev;
    uint8_t toggling_repeat_context;
    uint8_t toggling_shuffle;
    uint8_t toggling_repeat_track;
    uint8_t transferring_playback;
}SpotifyActions_t;

typedef struct {

    SpotifyDevice_t *device;
    SpotifyRepeatStateEnum_t repeat_state;
    uint8_t shuffle_state;
    SpotifyContext_t *context;
    uint64_t timestamp;
    uint64_t progress_ms;
    uint8_t is_playing;
    SpotifyItem_t *item;
    SpotifyCurrentlyPlayingEnum_t currently_playing_type;
    SpotifyActions_t *actions;
}SpotifyInfo_t;