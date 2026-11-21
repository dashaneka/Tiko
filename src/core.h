#ifndef TIKO_CORE_H
#define TIKO_CORE_H

#include <glib.h>
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    TIKO_ROUTE_FEED = 0,
    TIKO_ROUTE_VIDEO,
    TIKO_ROUTE_PROFILE,
    TIKO_ROUTE_SEARCH,
    TIKO_ROUTE_LOGIN,
    TIKO_ROUTE_OTHER
} TikoRoute;

typedef struct {
    char *creator;
    TikoRoute route;
    bool typing;
} TikoPageState;

void tiko_page_state_clear(TikoPageState *st);

TikoRoute tiko_classify_url(const char *url);
const char *tiko_route_name(TikoRoute route);
TikoRoute tiko_route_from_name(const char *name);

/* Extracts normalized "@handle" from a TikTok URL, or NULL if not present. */
char *tiko_extract_creator_from_url(const char *url);

/* Normalizes a creator handle ("khaby.lame" or "@khaby.lame") -> "@khaby.lame", or NULL if invalid. */
char *tiko_normalize_handle(const char *raw);

/* Converts user search input (@handle, TikTok URL, or keyword) into a target TikTok URL. */
char *tiko_build_target_url(const char *input, bool users_tab);

/* Parses a tab-separated state string ("creator\troute\ttyping") from the WebKit JS bridge. */
bool tiko_parse_bridge_message(const char *msg, TikoPageState *out);

/* Recent / saved creators persistence (newline-separated "@handle" list, newest first). */
char *tiko_creators_file_path(void);
GPtrArray *tiko_creators_load_file(const char *path);
void tiko_creators_record_file(const char *path, const char *handle);
void tiko_creators_remove_file(const char *path, const char *handle);
void tiko_creators_clear_file(const char *path);

#endif
