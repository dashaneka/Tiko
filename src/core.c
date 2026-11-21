#include "core.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define TIKO_MAX_CREATORS 30

void tiko_page_state_clear(TikoPageState *st) {
    if (!st) return;
    g_clear_pointer(&st->creator, g_free);
    memset(st, 0, sizeof(*st));
}

static bool is_valid_handle_char(unsigned char c) {
    return isalnum(c) || c == '_' || c == '.';
}

char *tiko_normalize_handle(const char *raw) {
    if (!raw) return NULL;
    const char *p = raw;
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p == '@') p++;
    const char *start = p;
    while (*p && !isspace((unsigned char)*p) && *p != '/' && *p != '?' && *p != '#') {
        if (!is_valid_handle_char((unsigned char)*p)) return NULL;
        p++;
    }
    size_t len = (size_t)(p - start);
    if (len == 0 || len > 40) return NULL;
    /* Trailing whitespace only */
    while (*p) {
        if (!isspace((unsigned char)*p)) return NULL;
        p++;
    }
    char *out = g_malloc(len + 2);
    out[0] = '@';
    memcpy(out + 1, start, len);
    out[len + 1] = '\0';
    return out;
}

static const char *url_path_start(const char *url, const char **out_host_start, size_t *out_host_len) {
    if (out_host_start) *out_host_start = NULL;
    if (out_host_len) *out_host_len = 0;
    if (!url) return NULL;
    const char *scheme = strstr(url, "://");
    const char *host = scheme ? scheme + 3 : url;
    const char *slash = strchr(host, '/');
    const char *q = strchr(host, '?');
    const char *h = strchr(host, '#');
    const char *end = host + strlen(host);
    if (slash && slash < end) end = slash;
    if (q && q < end) end = q;
    if (h && h < end) end = h;
    if (out_host_start) *out_host_start = host;
    if (out_host_len) *out_host_len = (size_t)(end - host);
    return slash;
}

static bool host_contains_ci(const char *host, size_t len, const char *needle) {
    if (!host || len == 0 || !needle) return false;
    char *h = g_ascii_strdown(host, (gssize)len);
    bool found = strstr(h, needle) != NULL;
    g_free(h);
    return found;
}

TikoRoute tiko_classify_url(const char *url) {
    if (!url || !url[0]) return TIKO_ROUTE_OTHER;
    const char *host = NULL;
    size_t host_len = 0;
    const char *path = url_path_start(url, &host, &host_len);

    if (host_contains_ci(host, host_len, "accounts.google.") ||
        host_contains_ci(host, host_len, "appleid.apple.") ||
        host_contains_ci(host, host_len, "facebook.com") ||
        host_contains_ci(host, host_len, "passport")) {
        return TIKO_ROUTE_LOGIN;
    }
    if (!host_contains_ci(host, host_len, "tiktok.com")) {
        return TIKO_ROUTE_OTHER;
    }
    if (!path || strcmp(path, "/") == 0) return TIKO_ROUTE_FEED;

    /* Strip query/fragment into a small lowercase copy of path */
    const char *end = path;
    while (*end && *end != '?' && *end != '#') end++;
    char *p = g_ascii_strdown(path, (gssize)(end - path));
    TikoRoute route = TIKO_ROUTE_OTHER;

    if (g_str_has_prefix(p, "/login") || g_str_has_prefix(p, "/signup") ||
        g_str_has_prefix(p, "/passport") || g_str_has_prefix(p, "/auth")) {
        route = TIKO_ROUTE_LOGIN;
    } else if (g_str_has_prefix(p, "/search")) {
        route = TIKO_ROUTE_SEARCH;
    } else if (strcmp(p, "/") == 0 || g_str_has_prefix(p, "/foryou") ||
               g_str_has_prefix(p, "/following") || g_str_has_prefix(p, "/friends") ||
               g_str_has_prefix(p, "/explore")) {
        route = TIKO_ROUTE_FEED;
    } else if (g_str_has_prefix(p, "/@")) {
        if (strstr(p, "/video/") || strstr(p, "/photo/")) {
            route = TIKO_ROUTE_VIDEO;
        } else {
            route = TIKO_ROUTE_PROFILE;
        }
    } else if (g_str_has_prefix(p, "/t/") || g_str_has_prefix(p, "/video/")) {
        route = TIKO_ROUTE_VIDEO;
    }

    g_free(p);
    return route;
}

const char *tiko_route_name(TikoRoute route) {
    switch (route) {
        case TIKO_ROUTE_FEED: return "feed";
        case TIKO_ROUTE_VIDEO: return "video";
        case TIKO_ROUTE_PROFILE: return "profile";
        case TIKO_ROUTE_SEARCH: return "search";
        case TIKO_ROUTE_LOGIN: return "login";
        default: return "other";
    }
}

TikoRoute tiko_route_from_name(const char *name) {
    if (!name) return TIKO_ROUTE_OTHER;
    if (strcmp(name, "feed") == 0) return TIKO_ROUTE_FEED;
    if (strcmp(name, "video") == 0) return TIKO_ROUTE_VIDEO;
    if (strcmp(name, "profile") == 0) return TIKO_ROUTE_PROFILE;
    if (strcmp(name, "search") == 0) return TIKO_ROUTE_SEARCH;
    if (strcmp(name, "login") == 0) return TIKO_ROUTE_LOGIN;
    return TIKO_ROUTE_OTHER;
}

char *tiko_extract_creator_from_url(const char *url) {
    if (!url) return NULL;
    const char *at = strstr(url, "/@");
    if (!at) return NULL;
    at += 2;
    const char *end = at;
    while (*end && *end != '/' && *end != '?' && *end != '#') end++;
    if (end == at) return NULL;
    char *slice = g_strndup(at, (gsize)(end - at));
    char *norm = tiko_normalize_handle(slice);
    g_free(slice);
    return norm;
}

char *tiko_build_target_url(const char *input, bool users_tab) {
    if (!input) return NULL;
    char *trimmed = g_strstrip(g_strdup(input));
    if (!trimmed[0]) {
        g_free(trimmed);
        return NULL;
    }

    /* Full URL */
    if (g_str_has_prefix(trimmed, "https://") || g_str_has_prefix(trimmed, "http://")) {
        return trimmed;
    }
    if (g_str_has_prefix(trimmed, "www.tiktok.com/") || g_str_has_prefix(trimmed, "tiktok.com/") ||
        g_str_has_prefix(trimmed, "vm.tiktok.com/") || g_str_has_prefix(trimmed, "vt.tiktok.com/")) {
        char *url = g_strdup_printf("https://%s", trimmed);
        g_free(trimmed);
        return url;
    }

    /* Direct @creator handle jump */
    if (trimmed[0] == '@') {
        char *norm = tiko_normalize_handle(trimmed);
        if (norm) {
            char *url = g_strdup_printf("https://www.tiktok.com/%s", norm);
            g_free(norm);
            g_free(trimmed);
            return url;
        }
    }

    /* Search TikTok (Creators tab or Top/Videos) */
    char *encoded = g_uri_escape_string(trimmed, NULL, FALSE);
    char *url = g_strdup_printf("https://www.tiktok.com/%s?q=%s",
                                users_tab ? "search/user" : "search", encoded);
    g_free(encoded);
    g_free(trimmed);
    return url;
}

bool tiko_parse_bridge_message(const char *msg, TikoPageState *out) {
    if (!msg || !out) return false;
    char **parts = g_strsplit(msg, "\t", 4);
    guint n = g_strv_length(parts);
    if (n < 3) {
        g_strfreev(parts);
        return false;
    }
    tiko_page_state_clear(out);
    if (parts[0][0]) out->creator = tiko_normalize_handle(parts[0]);
    out->route = tiko_route_from_name(parts[1]);
    out->typing = strcmp(parts[2], "1") == 0;
    g_strfreev(parts);
    return true;
}

char *tiko_creators_file_path(void) {
    return g_build_filename(g_get_user_data_dir(), "tiko", "creators.tsv", NULL);
}

GPtrArray *tiko_creators_load_file(const char *path) {
    GPtrArray *arr = g_ptr_array_new_with_free_func(g_free);
    if (!path) return arr;
    char *contents = NULL;
    if (!g_file_get_contents(path, &contents, NULL, NULL) || !contents) return arr;
    char **lines = g_strsplit(contents, "\n", -1);
    for (int i = 0; lines[i]; i++) {
        char *norm = tiko_normalize_handle(lines[i]);
        if (norm) g_ptr_array_add(arr, norm);
    }
    g_strfreev(lines);
    g_free(contents);
    return arr;
}

static void write_creators_file(const char *path, GPtrArray *arr) {
    char *dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0755);
    g_free(dir);
    GString *s = g_string_new(NULL);
    for (guint i = 0; i < arr->len; i++) {
        g_string_append(s, g_ptr_array_index(arr, i));
        g_string_append_c(s, '\n');
    }
    g_file_set_contents(path, s->str, (gssize)s->len, NULL);
    g_string_free(s, TRUE);
}

void tiko_creators_record_file(const char *path, const char *handle) {
    if (!path || !handle) return;
    char *norm = tiko_normalize_handle(handle);
    if (!norm) return;
    GPtrArray *arr = tiko_creators_load_file(path);
    for (guint i = 0; i < arr->len; ) {
        if (g_ascii_strcasecmp(g_ptr_array_index(arr, i), norm) == 0)
            g_ptr_array_remove_index(arr, i);
        else
            i++;
    }
    g_ptr_array_insert(arr, 0, norm);
    while (arr->len > TIKO_MAX_CREATORS) g_ptr_array_remove_index(arr, arr->len - 1);
    write_creators_file(path, arr);
    g_ptr_array_unref(arr);
}

void tiko_creators_remove_file(const char *path, const char *handle) {
    if (!path || !handle) return;
    char *norm = tiko_normalize_handle(handle);
    if (!norm) return;
    GPtrArray *arr = tiko_creators_load_file(path);
    for (guint i = 0; i < arr->len; ) {
        if (g_ascii_strcasecmp(g_ptr_array_index(arr, i), norm) == 0)
            g_ptr_array_remove_index(arr, i);
        else
            i++;
    }
    g_free(norm);
    write_creators_file(path, arr);
    g_ptr_array_unref(arr);
}

void tiko_creators_clear_file(const char *path) {
    if (path) remove(path);
}
