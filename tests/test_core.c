#include "../src/core.h"

#include <assert.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

static void test_classify_url(void) {
    assert(tiko_classify_url("https://www.tiktok.com/") == TIKO_ROUTE_FEED);
    assert(tiko_classify_url("https://www.tiktok.com/foryou") == TIKO_ROUTE_FEED);
    assert(tiko_classify_url("https://www.tiktok.com/following") == TIKO_ROUTE_FEED);
    assert(tiko_classify_url("https://www.tiktok.com/@khaby.lame") == TIKO_ROUTE_PROFILE);
    assert(tiko_classify_url("https://www.tiktok.com/@khaby.lame?lang=en") == TIKO_ROUTE_PROFILE);
    assert(tiko_classify_url("https://www.tiktok.com/@khaby.lame/video/7123456789") == TIKO_ROUTE_VIDEO);
    assert(tiko_classify_url("https://www.tiktok.com/search/user?q=cooking") == TIKO_ROUTE_SEARCH);
    assert(tiko_classify_url("https://www.tiktok.com/search?q=cats") == TIKO_ROUTE_SEARCH);
    assert(tiko_classify_url("https://www.tiktok.com/login") == TIKO_ROUTE_LOGIN);
    assert(tiko_classify_url("https://accounts.google.com/o/oauth2/auth") == TIKO_ROUTE_LOGIN);
    assert(tiko_classify_url("https://example.com/") == TIKO_ROUTE_OTHER);
}

static void test_handles_and_targets(void) {
    char *h1 = tiko_normalize_handle("  @khaby.lame  ");
    assert(h1 && strcmp(h1, "@khaby.lame") == 0);
    g_free(h1);

    char *h2 = tiko_normalize_handle("mrbeast_1");
    assert(h2 && strcmp(h2, "@mrbeast_1") == 0);
    g_free(h2);

    assert(tiko_normalize_handle("has space") == NULL);
    assert(tiko_normalize_handle("") == NULL);

    char *c1 = tiko_extract_creator_from_url("https://www.tiktok.com/@charlidamelio/video/12345?is_from_webapp=1");
    assert(c1 && strcmp(c1, "@charlidamelio") == 0);
    g_free(c1);

    assert(tiko_extract_creator_from_url("https://www.tiktok.com/foryou") == NULL);

    char *u1 = tiko_build_target_url("@khaby.lame", true);
    assert(u1 && strcmp(u1, "https://www.tiktok.com/@khaby.lame") == 0);
    g_free(u1);

    char *u2 = tiko_build_target_url("street food", true);
    assert(u2 && strcmp(u2, "https://www.tiktok.com/search/user?q=street%20food") == 0);
    g_free(u2);

    char *u3 = tiko_build_target_url("street food", false);
    assert(u3 && strcmp(u3, "https://www.tiktok.com/search?q=street%20food") == 0);
    g_free(u3);

    char *u4 = tiko_build_target_url("https://www.tiktok.com/@nasa/video/99", true);
    assert(u4 && strcmp(u4, "https://www.tiktok.com/@nasa/video/99") == 0);
    g_free(u4);

    assert(tiko_build_target_url("   ", true) == NULL);
}

static void test_bridge(void) {
    TikoPageState st = {0};
    assert(tiko_parse_bridge_message("@nasa\tfeed\t0", &st));
    assert(st.route == TIKO_ROUTE_FEED);
    assert(st.creator && strcmp(st.creator, "@nasa") == 0);
    assert(st.typing == false);
    tiko_page_state_clear(&st);

    assert(tiko_parse_bridge_message("\tlogin\t1", &st));
    assert(st.route == TIKO_ROUTE_LOGIN);
    assert(st.creator == NULL);
    assert(st.typing == true);
    tiko_page_state_clear(&st);

    assert(!tiko_parse_bridge_message("bad\tmsg", &st));
}

static void test_creators_persistence(void) {
    char *dir = g_dir_make_tmp("tiko-test-XXXXXX", NULL);
    assert(dir != NULL);
    char *path = g_build_filename(dir, "creators.tsv", NULL);

    tiko_creators_record_file(path, "nasa");
    tiko_creators_record_file(path, "@khaby.lame");
    tiko_creators_record_file(path, "@NASA"); /* dedup case-insensitive, moves to top */

    GPtrArray *list = tiko_creators_load_file(path);
    assert(list->len == 2);
    assert(strcmp(g_ptr_array_index(list, 0), "@NASA") == 0);
    assert(strcmp(g_ptr_array_index(list, 1), "@khaby.lame") == 0);
    g_ptr_array_unref(list);

    tiko_creators_remove_file(path, "@khaby.lame");
    list = tiko_creators_load_file(path);
    assert(list->len == 1);
    g_ptr_array_unref(list);

    tiko_creators_clear_file(path);
    list = tiko_creators_load_file(path);
    assert(list->len == 0);
    g_ptr_array_unref(list);

    g_free(path);
    g_rmdir(dir);
    g_free(dir);
}

int main(void) {
    test_classify_url();
    test_handles_and_targets();
    test_bridge();
    test_creators_persistence();
    printf("All Tiko unit tests passed!\n");
    return 0;
}
