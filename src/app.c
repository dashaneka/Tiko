#define _POSIX_C_SOURCE 200809L

#include "core.h"
#include "inject.h"

#include <adwaita.h>
#include <glib-unix.h>
#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>
#include <webkit/webkit.h>

#define APP_ID "io.github.rajnish.Tiko"
#define TIKTOK_FORYOU "https://www.tiktok.com/foryou"
#define TIKTOK_FOLLOWING "https://www.tiktok.com/following"
#define TIKTOK_LOGIN "https://www.tiktok.com/login"
#define HIDE_DELAY_MS 2200

static const char APP_CSS[] =
    ".tiko-window { background-color: #000000; }"
    ".tiko-header {"
    "  background: linear-gradient(to bottom, alpha(black, 0.78), alpha(black, 0.35) 68%, transparent);"
    "  border-bottom: none;"
    "  box-shadow: none;"
    "  color: white;"
    "  padding-top: 2px;"
    "}"
    ".tiko-header button, .tiko-header togglebutton, .tiko-header menubutton > button {"
    "  color: white;"
    "  padding-left: 6px;"
    "  padding-right: 6px;"
    "  min-width: 28px;"
    "}"
    ".tiko-creator-pill {"
    "  background: alpha(white, 0.12);"
    "  border-radius: 999px;"
    "  padding: 2px 10px;"
    "  min-height: 26px;"
    "  font-weight: 600;"
    "  font-size: 12px;"
    "  color: white;"
    "}"
    ".tiko-creator-pill:hover { background: alpha(white, 0.22); }"
    ".tiko-search-bar {"
    "  background: alpha(#121216, 0.90);"
    "  border: 1px solid alpha(white, 0.12);"
    "  border-radius: 14px;"
    "  padding: 8px 10px;"
    "  margin: 4px 12px 0 12px;"
    "  box-shadow: 0 8px 24px alpha(black, 0.55);"
    "}"
    ".tiko-progress { min-height: 2px; }"
    ".tiko-progress trough { min-height: 2px; background: transparent; }"
    ".tiko-progress progress { min-height: 2px; background: #fe2c55; border-radius: 0; }"
    ".tiko-focus-active { color: #25f4ee; }";

typedef struct {
    AdwApplicationWindow *window;
    AdwToastOverlay *toasts;
    WebKitWebView *web;
    WebKitNetworkSession *session;
    WebKitUserContentManager *ucm;

    GtkRevealer *top_revealer;
    GtkRevealer *search_revealer;
    GtkProgressBar *progress;

    GtkButton *back_btn;
    GtkButton *creator_btn;
    GtkToggleButton *search_btn;
    GtkToggleButton *focus_btn;
    GtkMenuButton *creators_menu_btn;
    GtkListBox *creators_list;
    GtkLabel *creators_empty;
    GtkButton *creators_clear_btn;

    GtkSearchEntry *search_entry;
    GtkToggleButton *search_users_tab;
    GtkToggleButton *search_videos_tab;

    TikoPageState pstate;
    bool focus_mode;
    bool fullscreen;
    bool popover_open;
    bool pointer_over_chrome;
    bool stopped;
    guint hide_timer_id;
    char *creators_path;
} TikoApp;

static TikoApp *g_tiko = NULL;

static void announce(TikoApp *a, const char *text) {
    if (!a || a->stopped || !text || !text[0]) return;
    AdwToast *t = adw_toast_new(text);
    adw_toast_set_use_markup(t, FALSE);
    adw_toast_set_timeout(t, 2);
    adw_toast_overlay_add_toast(a->toasts, t);
}

static void eval_js(TikoApp *a, const char *js) {
    if (!a || a->stopped || !a->web) return;
    webkit_web_view_evaluate_javascript(a->web, js, -1, NULL, NULL, NULL, NULL, NULL);
}

static void navigate_to(TikoApp *a, const char *url) {
    if (!a || a->stopped || !a->web || !url || !url[0]) return;
    webkit_web_view_load_uri(a->web, url);
}

static bool should_keep_chrome_visible(const TikoApp *a) {
    if (!a) return true;
    if (gtk_toggle_button_get_active(a->search_btn)) return true;
    if (a->popover_open || a->pointer_over_chrome) return true;
    if (a->pstate.route == TIKO_ROUTE_PROFILE ||
        a->pstate.route == TIKO_ROUTE_SEARCH ||
        a->pstate.route == TIKO_ROUTE_LOGIN) {
        return true;
    }
    return false;
}

static gboolean on_hide_controls_timeout(gpointer ud) {
    TikoApp *a = ud;
    a->hide_timer_id = 0;
    if (a->stopped) return G_SOURCE_REMOVE;
    if (!should_keep_chrome_visible(a)) {
        gtk_revealer_set_reveal_child(a->top_revealer, FALSE);
    }
    return G_SOURCE_REMOVE;
}

static void schedule_hide_controls(TikoApp *a) {
    if (a->hide_timer_id) {
        g_source_remove(a->hide_timer_id);
        a->hide_timer_id = 0;
    }
    if (!should_keep_chrome_visible(a)) {
        a->hide_timer_id = g_timeout_add(HIDE_DELAY_MS, on_hide_controls_timeout, a);
    }
}

static void show_controls(TikoApp *a) {
    if (!a || a->stopped) return;
    gtk_revealer_set_reveal_child(a->top_revealer, TRUE);
    schedule_hide_controls(a);
}

/* ── Recent Creators Popover ────────────────────────────────────────────── */

typedef struct {
    TikoApp *app;
    char *handle;
} CreatorRowCtx;

static void creator_row_ctx_free(gpointer d, GClosure *cl) {
    (void)cl;
    CreatorRowCtx *c = d;
    g_free(c->handle);
    g_free(c);
}

static void refresh_creators_popover(TikoApp *a);

static void on_creator_open_clicked(GtkButton *b, gpointer ud) {
    (void)b;
    CreatorRowCtx *c = ud;
    if (c->app->stopped) return;
    char *url = tiko_build_target_url(c->handle, true);
    if (url) {
        navigate_to(c->app, url);
        g_free(url);
    }
    gtk_menu_button_popdown(c->app->creators_menu_btn);
}

static void on_creator_del_clicked(GtkButton *b, gpointer ud) {
    (void)b;
    CreatorRowCtx *c = ud;
    if (c->app->stopped) return;
    tiko_creators_remove_file(c->app->creators_path, c->handle);
    refresh_creators_popover(c->app);
}

static void clear_listbox(GtkListBox *lb) {
    GtkWidget *ch;
    while ((ch = gtk_widget_get_first_child(GTK_WIDGET(lb))) != NULL)
        gtk_list_box_remove(lb, ch);
}

static void refresh_creators_popover(TikoApp *a) {
    if (!a || a->stopped) return;
    clear_listbox(a->creators_list);
    GPtrArray *list = tiko_creators_load_file(a->creators_path);
    gtk_widget_set_visible(GTK_WIDGET(a->creators_empty), list->len == 0);
    gtk_widget_set_visible(GTK_WIDGET(a->creators_list), list->len > 0);
    gtk_widget_set_visible(GTK_WIDGET(a->creators_clear_btn), list->len > 0);

    for (guint i = 0; i < list->len; i++) {
        const char *handle = g_ptr_array_index(list, i);
        GtkWidget *open_btn = gtk_button_new_with_label(handle);
        gtk_widget_add_css_class(open_btn, "flat");
        gtk_widget_set_hexpand(open_btn, TRUE);
        gtk_widget_set_halign(open_btn, GTK_ALIGN_FILL);
        GtkWidget *lbl = gtk_button_get_child(GTK_BUTTON(open_btn));
        if (lbl) gtk_widget_set_halign(lbl, GTK_ALIGN_START);

        CreatorRowCtx *oc = g_new(CreatorRowCtx, 1);
        oc->app = a;
        oc->handle = g_strdup(handle);
        g_signal_connect_data(open_btn, "clicked", G_CALLBACK(on_creator_open_clicked), oc, creator_row_ctx_free, 0);

        GtkWidget *del_btn = gtk_button_new_from_icon_name("window-close-symbolic");
        gtk_widget_add_css_class(del_btn, "flat");
        gtk_widget_set_tooltip_text(del_btn, "Remove");
        CreatorRowCtx *dc = g_new(CreatorRowCtx, 1);
        dc->app = a;
        dc->handle = g_strdup(handle);
        g_signal_connect_data(del_btn, "clicked", G_CALLBACK(on_creator_del_clicked), dc, creator_row_ctx_free, 0);

        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
        gtk_box_append(GTK_BOX(row), open_btn);
        gtk_box_append(GTK_BOX(row), del_btn);
        gtk_list_box_append(a->creators_list, row);
    }
    g_ptr_array_unref(list);
}

/* ── Sync UI from Page State & Navigation ───────────────────────────────── */

static void sync_creator_pill(TikoApp *a, const char *handle) {
    if (handle && handle[0]) {
        gtk_button_set_label(a->creator_btn, handle);
        gtk_widget_set_tooltip_text(GTK_WIDGET(a->creator_btn), "Open creator profile");
    } else {
        gtk_button_set_label(a->creator_btn, "Tiko");
        gtk_widget_set_tooltip_text(GTK_WIDGET(a->creator_btn), "For You Feed");
    }
}

static void on_script_message(WebKitUserContentManager *ucm, JSCValue *val, gpointer ud) {
    (void)ucm;
    TikoApp *a = ud;
    if (a->stopped || !val || !jsc_value_is_string(val)) return;
    char *msg = jsc_value_to_string(val);
    if (!msg) return;

    TikoRoute prev_route = a->pstate.route;
    char *prev_creator = g_strdup(a->pstate.creator);
    if (tiko_parse_bridge_message(msg, &a->pstate)) {
        if (a->pstate.route == TIKO_ROUTE_PROFILE && a->pstate.creator &&
            g_strcmp0(prev_creator, a->pstate.creator) != 0) {
            tiko_creators_record_file(a->creators_path, a->pstate.creator);
            refresh_creators_popover(a);
        }
        sync_creator_pill(a, a->pstate.creator);
        if (prev_route != a->pstate.route) {
            if (should_keep_chrome_visible(a)) show_controls(a);
            else schedule_hide_controls(a);
        }
    }
    g_free(prev_creator);
    g_free(msg);
}

static void on_uri_changed(WebKitWebView *web, GParamSpec *ps, gpointer ud) {
    (void)ps;
    TikoApp *a = ud;
    if (a->stopped) return;
    const char *uri = webkit_web_view_get_uri(web);
    gtk_widget_set_sensitive(GTK_WIDGET(a->back_btn), webkit_web_view_can_go_back(web));

    a->pstate.route = tiko_classify_url(uri);
    char *creator = tiko_extract_creator_from_url(uri);
    if (creator) {
        g_free(a->pstate.creator);
        a->pstate.creator = creator;
        if (a->pstate.route == TIKO_ROUTE_PROFILE) {
            tiko_creators_record_file(a->creators_path, creator);
            refresh_creators_popover(a);
        }
    }
    sync_creator_pill(a, a->pstate.creator);
    if (should_keep_chrome_visible(a)) {
        gtk_revealer_set_reveal_child(a->top_revealer, TRUE);
    }
}

static void on_load_progress(WebKitWebView *web, GParamSpec *ps, gpointer ud) {
    (void)ps;
    TikoApp *a = ud;
    if (a->stopped) return;
    double p = webkit_web_view_get_estimated_load_progress(web);
    gtk_progress_bar_set_fraction(a->progress, p);
    gtk_widget_set_visible(GTK_WIDGET(a->progress), p > 0.0 && p < 1.0);
    gtk_widget_set_sensitive(GTK_WIDGET(a->back_btn), webkit_web_view_can_go_back(web));
}

static void on_load_changed(WebKitWebView *web, WebKitLoadEvent ev, gpointer ud) {
    TikoApp *a = ud;
    if (a->stopped) return;
    gtk_widget_set_sensitive(GTK_WIDGET(a->back_btn), webkit_web_view_can_go_back(web));
    if (ev == WEBKIT_LOAD_FINISHED) {
        gtk_widget_set_visible(GTK_WIDGET(a->progress), FALSE);
        eval_js(a, a->focus_mode ? "window.__tiko&&window.__tiko.setFocus(true);"
                                 : "window.__tiko&&window.__tiko.setFocus(false);");
    }
}

/* ── OAuth / Login Popup Window Support ─────────────────────────────────── */

static void on_popup_ready_to_show(WebKitWebView *popup_web, gpointer ud) {
    (void)popup_web;
    gtk_window_present(GTK_WINDOW(ud));
}

static void on_popup_close(WebKitWebView *popup_web, gpointer ud) {
    (void)popup_web;
    gtk_window_close(GTK_WINDOW(ud));
}

static void on_popup_title(WebKitWebView *popup_web, GParamSpec *ps, gpointer ud) {
    (void)ps;
    const char *title = webkit_web_view_get_title(popup_web);
    gtk_window_set_title(GTK_WINDOW(ud), (title && title[0]) ? title : "Sign In — Tiko");
}

static GtkWidget *on_web_create(WebKitWebView *web, WebKitNavigationAction *action, gpointer ud) {
    (void)action;
    TikoApp *a = ud;
    GtkWidget *popup_win = adw_window_new();
    gtk_window_set_application(GTK_WINDOW(popup_win), gtk_window_get_application(GTK_WINDOW(a->window)));
    gtk_window_set_title(GTK_WINDOW(popup_win), "Sign In — Tiko");
    gtk_window_set_default_size(GTK_WINDOW(popup_win), 460, 660);
    gtk_window_set_transient_for(GTK_WINDOW(popup_win), GTK_WINDOW(a->window));

    GtkWidget *popup_web = g_object_new(
        WEBKIT_TYPE_WEB_VIEW,
        "related-view", web,
        "settings", webkit_web_view_get_settings(web),
        NULL);
    GtkWidget *hdr = adw_header_bar_new();
    GtkWidget *tb = adw_toolbar_view_new();
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(tb), hdr);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(tb), popup_web);
    adw_window_set_content(ADW_WINDOW(popup_win), tb);

    g_signal_connect(popup_web, "ready-to-show", G_CALLBACK(on_popup_ready_to_show), popup_win);
    g_signal_connect(popup_web, "close", G_CALLBACK(on_popup_close), popup_win);
    g_signal_connect(popup_web, "notify::title", G_CALLBACK(on_popup_title), popup_win);
    g_signal_connect(popup_web, "create", G_CALLBACK(on_web_create), a);

    gtk_window_present(GTK_WINDOW(popup_win));
    /* WebKitWebView::create is transfer-ownership="full" — add a ref since tb sank the floating ref. */
    return g_object_ref(popup_web);
}

/* ── UI Button & Gesture Handlers ───────────────────────────────────────── */

static void set_focus_mode(TikoApp *a, bool enabled, bool toast) {
    a->focus_mode = enabled;
    if (gtk_toggle_button_get_active(a->focus_btn) != enabled)
        gtk_toggle_button_set_active(a->focus_btn, enabled);
    if (enabled) gtk_widget_add_css_class(GTK_WIDGET(a->focus_btn), "tiko-focus-active");
    else gtk_widget_remove_css_class(GTK_WIDGET(a->focus_btn), "tiko-focus-active");
    eval_js(a, enabled ? "window.__tiko&&window.__tiko.setFocus(true);"
                       : "window.__tiko&&window.__tiko.setFocus(false);");
    if (toast) announce(a, enabled ? "Pure Video Mode" : "Full TikTok UI");
}

static void toggle_fullscreen(TikoApp *a) {
    a->fullscreen = !a->fullscreen;
    if (a->fullscreen) gtk_window_fullscreen(GTK_WINDOW(a->window));
    else gtk_window_unfullscreen(GTK_WINDOW(a->window));
}

static void on_back_clicked(GtkButton *b, gpointer ud) {
    (void)b;
    TikoApp *a = ud;
    if (webkit_web_view_can_go_back(a->web)) webkit_web_view_go_back(a->web);
}
static void on_home_clicked(GtkButton *b, gpointer ud) {
    (void)b;
    navigate_to((TikoApp *)ud, TIKTOK_FORYOU);
}
static void on_following_clicked(GtkButton *b, gpointer ud) {
    (void)b;
    TikoApp *a = ud;
    gtk_menu_button_popdown(a->creators_menu_btn);
    navigate_to(a, TIKTOK_FOLLOWING);
}
static void on_account_clicked(GtkButton *b, gpointer ud) {
    (void)b;
    TikoApp *a = ud;
    if (a->stopped) return;
    webkit_web_view_stop_loading(a->web);
    a->pstate.route = TIKO_ROUTE_LOGIN;
    show_controls(a);
    navigate_to(a, TIKTOK_LOGIN);
    announce(a, "Opening TikTok Login…");
}
static void on_creator_pill_clicked(GtkButton *b, gpointer ud) {
    (void)b;
    TikoApp *a = ud;
    if (a->pstate.creator && a->pstate.creator[0]) {
        char *url = tiko_build_target_url(a->pstate.creator, true);
        if (url) {
            navigate_to(a, url);
            g_free(url);
        }
    } else {
        navigate_to(a, TIKTOK_FORYOU);
    }
}
static void on_focus_toggled(GtkToggleButton *b, gpointer ud) {
    TikoApp *a = ud;
    if (a->stopped) return;
    bool on = gtk_toggle_button_get_active(b);
    if (on != a->focus_mode) set_focus_mode(a, on, true);
}
static void on_search_toggled(GtkToggleButton *b, gpointer ud) {
    TikoApp *a = ud;
    if (a->stopped) return;
    bool open = gtk_toggle_button_get_active(b);
    gtk_revealer_set_reveal_child(a->search_revealer, open);
    if (open) {
        show_controls(a);
        gtk_widget_grab_focus(GTK_WIDGET(a->search_entry));
        gtk_editable_select_region(GTK_EDITABLE(a->search_entry), 0, -1);
    } else {
        gtk_widget_grab_focus(GTK_WIDGET(a->web));
        schedule_hide_controls(a);
    }
}
static void on_search_activate(GtkSearchEntry *e, gpointer ud) {
    TikoApp *a = ud;
    const char *q = gtk_editable_get_text(GTK_EDITABLE(e));
    bool users_tab = gtk_toggle_button_get_active(a->search_users_tab);
    char *url = tiko_build_target_url(q, users_tab);
    if (!url) return;
    if (q && q[0] == '@') {
        tiko_creators_record_file(a->creators_path, q);
        refresh_creators_popover(a);
    }
    navigate_to(a, url);
    g_free(url);
    gtk_toggle_button_set_active(a->search_btn, FALSE);
}
static void on_creators_clear(GtkButton *b, gpointer ud) {
    (void)b;
    TikoApp *a = ud;
    tiko_creators_clear_file(a->creators_path);
    refresh_creators_popover(a);
    announce(a, "Cleared recent creators");
}
static void on_popover_notify(GtkPopover *pop, GParamSpec *ps, gpointer ud) {
    (void)ps;
    TikoApp *a = ud;
    a->popover_open = gtk_widget_get_visible(GTK_WIDGET(pop));
    if (a->popover_open) {
        refresh_creators_popover(a);
        show_controls(a);
    } else {
        schedule_hide_controls(a);
    }
}

static void on_pointer_motion(GtkEventControllerMotion *c, double x, double y, gpointer ud) {
    (void)x; (void)y;
    /* Never trigger header revealer relayouts while the user is dragging inside the webview. */
    GdkModifierType state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(c));
    if (state & (GDK_BUTTON1_MASK | GDK_BUTTON2_MASK | GDK_BUTTON3_MASK)) return;
    show_controls((TikoApp *)ud);
}
static void on_chrome_enter(GtkEventControllerMotion *c, double x, double y, gpointer ud) {
    (void)c; (void)x; (void)y;
    TikoApp *a = ud;
    a->pointer_over_chrome = true;
    show_controls(a);
}
static void on_chrome_leave(GtkEventControllerMotion *c, gpointer ud) {
    (void)c;
    TikoApp *a = ud;
    a->pointer_over_chrome = false;
    schedule_hide_controls(a);
}

/* Capture phase: only modifier / window-level shortcuts so web dialogs & sliders always get raw keys first. */
static gboolean on_app_key_pressed(GtkEventControllerKey *c, guint key, guint kc, GdkModifierType mods, gpointer ud) {
    (void)c; (void)kc;
    TikoApp *a = ud;
    bool ctrl = (mods & GDK_CONTROL_MASK) != 0;
    bool alt = (mods & GDK_ALT_MASK) != 0;

    if (ctrl && gdk_keyval_to_lower(key) == GDK_KEY_f) {
        gtk_toggle_button_set_active(a->search_btn, !gtk_toggle_button_get_active(a->search_btn));
        return TRUE;
    }
    if (ctrl && gdk_keyval_to_lower(key) == GDK_KEY_l) {
        on_account_clicked(NULL, a);
        return TRUE;
    }
    if (ctrl && gdk_keyval_to_lower(key) == GDK_KEY_r) {
        webkit_web_view_reload(a->web);
        return TRUE;
    }
    if (key == GDK_KEY_F5) {
        webkit_web_view_reload(a->web);
        return TRUE;
    }
    if (key == GDK_KEY_F11) {
        toggle_fullscreen(a);
        return TRUE;
    }
    if (alt && key == GDK_KEY_Left) {
        on_back_clicked(NULL, a);
        return TRUE;
    }
    if (key == GDK_KEY_Escape) {
        if (gtk_toggle_button_get_active(a->search_btn)) {
            gtk_toggle_button_set_active(a->search_btn, FALSE);
            return TRUE;
        }
        if (a->fullscreen) {
            toggle_fullscreen(a);
            return TRUE;
        }
        return FALSE;
    }
    return FALSE;
}

/* Bubble phase: single-key shortcuts run only if WebKitWebView didn't handle the key and no input/dialog is active. */
static gboolean on_playback_key_pressed(GtkEventControllerKey *c, guint key, guint kc, GdkModifierType mods, gpointer ud) {
    (void)c; (void)kc;
    TikoApp *a = ud;
    if (gtk_widget_has_focus(GTK_WIDGET(a->search_entry))) return FALSE;
    if (a->pstate.typing || a->pstate.route == TIKO_ROUTE_LOGIN || a->pstate.route == TIKO_ROUTE_SEARCH)
        return FALSE;
    if (mods & (GDK_CONTROL_MASK | GDK_ALT_MASK | GDK_SUPER_MASK)) return FALSE;

    switch (gdk_keyval_to_lower(key)) {
        case GDK_KEY_slash:
            gtk_toggle_button_set_active(a->search_btn, TRUE);
            return TRUE;
        case GDK_KEY_z:
            set_focus_mode(a, !a->focus_mode, true);
            return TRUE;
        case GDK_KEY_f:
            if (a->pstate.route == TIKO_ROUTE_FEED || a->pstate.route == TIKO_ROUTE_VIDEO) {
                toggle_fullscreen(a);
                return TRUE;
            }
            return FALSE;
        case GDK_KEY_m:
            if (a->pstate.route == TIKO_ROUTE_FEED || a->pstate.route == TIKO_ROUTE_VIDEO) {
                eval_js(a, "window.__tiko&&window.__tiko.toggleMute();");
                return TRUE;
            }
            return FALSE;
        case GDK_KEY_space:
            if (a->pstate.route == TIKO_ROUTE_FEED || a->pstate.route == TIKO_ROUTE_VIDEO) {
                eval_js(a, "window.__tiko&&window.__tiko.togglePlay();");
                return TRUE;
            }
            return FALSE;
        case GDK_KEY_Down:
            if (a->pstate.route == TIKO_ROUTE_FEED || a->pstate.route == TIKO_ROUTE_VIDEO) {
                eval_js(a, "window.__tiko&&window.__tiko.next();");
                return TRUE;
            }
            return FALSE;
        case GDK_KEY_Up:
            if (a->pstate.route == TIKO_ROUTE_FEED || a->pstate.route == TIKO_ROUTE_VIDEO) {
                eval_js(a, "window.__tiko&&window.__tiko.prev();");
                return TRUE;
            }
            return FALSE;
        case GDK_KEY_Left:
            if (a->pstate.route == TIKO_ROUTE_FEED || a->pstate.route == TIKO_ROUTE_VIDEO) {
                eval_js(a, "window.__tiko&&window.__tiko.seekBy(-5);");
                return TRUE;
            }
            return FALSE;
        case GDK_KEY_Right:
            if (a->pstate.route == TIKO_ROUTE_FEED || a->pstate.route == TIKO_ROUTE_VIDEO) {
                eval_js(a, "window.__tiko&&window.__tiko.seekBy(5);");
                return TRUE;
            }
            return FALSE;
        default:
            return FALSE;
    }
}

/* ── Window Lifecycle ───────────────────────────────────────────────────── */

static void shutdown_tiko(TikoApp *a) {
    if (!a || a->stopped) return;
    a->stopped = true;
    if (a->hide_timer_id) {
        g_source_remove(a->hide_timer_id);
        a->hide_timer_id = 0;
    }
    if (a->ucm) {
        webkit_user_content_manager_unregister_script_message_handler(a->ucm, "tiko", NULL);
        g_clear_object(&a->ucm);
    }
    g_clear_object(&a->session);
    tiko_page_state_clear(&a->pstate);
    g_clear_pointer(&a->creators_path, g_free);
    if (g_tiko == a) g_tiko = NULL;
}

static void free_tiko(gpointer d) {
    TikoApp *a = d;
    shutdown_tiko(a);
    g_free(a);
}

static gboolean on_close_req(GtkWindow *w, gpointer ud) {
    (void)w;
    shutdown_tiko((TikoApp *)ud);
    return FALSE;
}

static GtkButton *make_icon_btn(const char *icon, const char *tip) {
    GtkWidget *b = gtk_button_new_from_icon_name(icon);
    gtk_widget_set_tooltip_text(b, tip);
    gtk_widget_add_css_class(b, "flat");
    return GTK_BUTTON(b);
}

static TikoApp *ensure_window(AdwApplication *app, const char *initial_url) {
    if (g_tiko && !g_tiko->stopped) {
        if (initial_url && initial_url[0]) navigate_to(g_tiko, initial_url);
        return g_tiko;
    }

    TikoApp *a = g_new0(TikoApp, 1);
    a->focus_mode = true;
    a->creators_path = tiko_creators_file_path();

    /* Persistent WebKitGTK session (keeps TikTok login & cookies across launches) */
    char *data_dir = g_build_filename(g_get_user_data_dir(), "tiko", "webkit", NULL);
    char *cache_dir = g_build_filename(g_get_user_cache_dir(), "tiko", "webkit", NULL);
    char *cookie_db = g_build_filename(data_dir, "cookies.sqlite", NULL);
    g_mkdir_with_parents(data_dir, 0700);
    g_mkdir_with_parents(cache_dir, 0700);

    a->session = webkit_network_session_new(data_dir, cache_dir);
    WebKitCookieManager *cm = webkit_network_session_get_cookie_manager(a->session);
    webkit_cookie_manager_set_persistent_storage(cm, cookie_db, WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE);
    webkit_cookie_manager_set_accept_policy(cm, WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS);
    g_free(data_dir);
    g_free(cache_dir);
    g_free(cookie_db);

    a->ucm = webkit_user_content_manager_new();
    g_signal_connect(a->ucm, "script-message-received::tiko", G_CALLBACK(on_script_message), a);
    webkit_user_content_manager_register_script_message_handler(a->ucm, "tiko", NULL);

    WebKitUserStyleSheet *sheet = webkit_user_style_sheet_new(
        TIKO_CLEAN_CSS, WEBKIT_USER_CONTENT_INJECT_TOP_FRAME,
        WEBKIT_USER_STYLE_LEVEL_USER, NULL, NULL);
    webkit_user_content_manager_add_style_sheet(a->ucm, sheet);
    webkit_user_style_sheet_unref(sheet);

    WebKitUserScript *script = webkit_user_script_new(
        TIKO_BRIDGE_JS, WEBKIT_USER_CONTENT_INJECT_TOP_FRAME,
        WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_END, NULL, NULL);
    webkit_user_content_manager_add_script(a->ucm, script);
    webkit_user_script_unref(script);

    WebKitSettings *settings = webkit_settings_new();
    webkit_settings_set_enable_javascript(settings, TRUE);
    webkit_settings_set_enable_mediasource(settings, TRUE);
    webkit_settings_set_media_playback_requires_user_gesture(settings, FALSE);
    webkit_settings_set_enable_fullscreen(settings, TRUE);
    webkit_settings_set_enable_smooth_scrolling(settings, TRUE);
    webkit_settings_set_enable_media_stream(settings, FALSE);
    webkit_settings_set_enable_webrtc(settings, FALSE);
    webkit_settings_set_javascript_can_open_windows_automatically(settings, TRUE);
    webkit_settings_set_hardware_acceleration_policy(settings, WEBKIT_HARDWARE_ACCELERATION_POLICY_ALWAYS);

    a->web = WEBKIT_WEB_VIEW(g_object_new(
        WEBKIT_TYPE_WEB_VIEW,
        "network-session", a->session,
        "user-content-manager", a->ucm,
        "settings", settings,
        NULL));
    g_object_unref(settings);

    GdkRGBA black = {0.0f, 0.0f, 0.0f, 1.0f};
    webkit_web_view_set_background_color(a->web, &black);
    gtk_widget_set_hexpand(GTK_WIDGET(a->web), TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(a->web), TRUE);

    /* Window */
    GtkWidget *win = adw_application_window_new(GTK_APPLICATION(app));
    a->window = ADW_APPLICATION_WINDOW(win);
    g_object_set_data_full(G_OBJECT(win), "tiko-app", a, free_tiko);
    gtk_window_set_title(GTK_WINDOW(win), "Tiko");
    gtk_window_set_default_size(GTK_WINDOW(win), 480, 800);
    gtk_widget_set_size_request(win, 410, 560);
    gtk_widget_add_css_class(win, "tiko-window");

    /* Floating Auto-Hiding Monet Header Bar */
    a->back_btn = make_icon_btn("go-previous-symbolic", "Back (Alt+←)");
    gtk_widget_set_sensitive(GTK_WIDGET(a->back_btn), FALSE);
    GtkButton *home_btn = make_icon_btn("go-home-symbolic", "For You");

    /* Recent Creators Popover (includes Following shortcut) */
    GtkWidget *follow_btn = gtk_button_new_with_label("Following Feed");
    gtk_widget_add_css_class(follow_btn, "flat");
    GtkWidget *flbl = gtk_button_get_child(GTK_BUTTON(follow_btn));
    if (flbl) gtk_widget_set_halign(flbl, GTK_ALIGN_START);

    a->creators_empty = GTK_LABEL(gtk_label_new("Visit or search a @creator to save them here"));
    gtk_label_set_wrap(a->creators_empty, TRUE);
    gtk_label_set_max_width_chars(a->creators_empty, 26);
    gtk_widget_set_margin_top(GTK_WIDGET(a->creators_empty), 12);
    gtk_widget_set_margin_bottom(GTK_WIDGET(a->creators_empty), 12);
    gtk_widget_add_css_class(GTK_WIDGET(a->creators_empty), "dim-label");

    a->creators_list = GTK_LIST_BOX(gtk_list_box_new());
    gtk_list_box_set_selection_mode(a->creators_list, GTK_SELECTION_NONE);

    GtkWidget *cscroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(cscroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(cscroll), 280);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(cscroll), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(cscroll), GTK_WIDGET(a->creators_list));

    GtkWidget *chdr_lbl = gtk_label_new("Recent Creators");
    gtk_widget_set_halign(chdr_lbl, GTK_ALIGN_START);
    gtk_widget_set_hexpand(chdr_lbl, TRUE);
    gtk_widget_add_css_class(chdr_lbl, "heading");
    a->creators_clear_btn = GTK_BUTTON(gtk_button_new_with_label("Clear"));
    gtk_widget_add_css_class(GTK_WIDGET(a->creators_clear_btn), "flat");
    GtkWidget *chdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_append(GTK_BOX(chdr), chdr_lbl);
    gtk_box_append(GTK_BOX(chdr), GTK_WIDGET(a->creators_clear_btn));

    GtkWidget *cbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_size_request(cbox, 230, -1);
    gtk_widget_set_margin_start(cbox, 8);
    gtk_widget_set_margin_end(cbox, 8);
    gtk_widget_set_margin_top(cbox, 6);
    gtk_widget_set_margin_bottom(cbox, 6);
    gtk_box_append(GTK_BOX(cbox), follow_btn);
    gtk_box_append(GTK_BOX(cbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL));
    gtk_box_append(GTK_BOX(cbox), chdr);
    gtk_box_append(GTK_BOX(cbox), GTK_WIDGET(a->creators_empty));
    gtk_box_append(GTK_BOX(cbox), cscroll);

    GtkWidget *cpop = gtk_popover_new();
    gtk_popover_set_child(GTK_POPOVER(cpop), cbox);
    a->creators_menu_btn = GTK_MENU_BUTTON(gtk_menu_button_new());
    gtk_menu_button_set_icon_name(a->creators_menu_btn, "starred-symbolic");
    gtk_widget_set_tooltip_text(GTK_WIDGET(a->creators_menu_btn), "Following & Recent Creators");
    gtk_menu_button_set_popover(a->creators_menu_btn, cpop);

    a->creator_btn = GTK_BUTTON(gtk_button_new_with_label("Tiko"));
    gtk_widget_add_css_class(GTK_WIDGET(a->creator_btn), "flat");
    gtk_widget_add_css_class(GTK_WIDGET(a->creator_btn), "tiko-creator-pill");
    GtkWidget *clbl = gtk_button_get_child(a->creator_btn);
    if (GTK_IS_LABEL(clbl)) {
        gtk_label_set_ellipsize(GTK_LABEL(clbl), PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(clbl), 11);
    }

    a->search_btn = GTK_TOGGLE_BUTTON(gtk_toggle_button_new());
    gtk_button_set_icon_name(GTK_BUTTON(a->search_btn), "system-search-symbolic");
    gtk_widget_set_tooltip_text(GTK_WIDGET(a->search_btn), "Search Creators (Ctrl+F or /)");

    a->focus_btn = GTK_TOGGLE_BUTTON(gtk_toggle_button_new());
    gtk_button_set_icon_name(GTK_BUTTON(a->focus_btn), "camera-video-symbolic");
    gtk_widget_set_tooltip_text(GTK_WIDGET(a->focus_btn), "Pure Video Mode (Z)");
    gtk_toggle_button_set_active(a->focus_btn, TRUE);
    gtk_widget_add_css_class(GTK_WIDGET(a->focus_btn), "tiko-focus-active");

    GtkButton *acct_btn = make_icon_btn("avatar-default-symbolic", "Sign in to TikTok (Ctrl+L)");

    GtkWidget *hdr = adw_header_bar_new();
    gtk_widget_add_css_class(hdr, "tiko-header");
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(hdr), GTK_WIDGET(a->creator_btn));
    adw_header_bar_pack_start(ADW_HEADER_BAR(hdr), GTK_WIDGET(a->back_btn));
    adw_header_bar_pack_start(ADW_HEADER_BAR(hdr), GTK_WIDGET(home_btn));
    adw_header_bar_pack_start(ADW_HEADER_BAR(hdr), GTK_WIDGET(a->creators_menu_btn));
    adw_header_bar_pack_end(ADW_HEADER_BAR(hdr), GTK_WIDGET(acct_btn));
    adw_header_bar_pack_end(ADW_HEADER_BAR(hdr), GTK_WIDGET(a->focus_btn));
    adw_header_bar_pack_end(ADW_HEADER_BAR(hdr), GTK_WIDGET(a->search_btn));

    /* Collapsible Search Bar */
    a->search_entry = GTK_SEARCH_ENTRY(gtk_search_entry_new());
    gtk_search_entry_set_placeholder_text(a->search_entry, "Search creator or @username…");
    gtk_widget_set_hexpand(GTK_WIDGET(a->search_entry), TRUE);

    a->search_users_tab = GTK_TOGGLE_BUTTON(gtk_toggle_button_new_with_label("Creators"));
    gtk_toggle_button_set_active(a->search_users_tab, TRUE);
    a->search_videos_tab = GTK_TOGGLE_BUTTON(gtk_toggle_button_new_with_label("Videos"));
    gtk_toggle_button_set_group(a->search_videos_tab, a->search_users_tab);
    GtkWidget *stabs = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(stabs, "linked");
    gtk_box_append(GTK_BOX(stabs), GTK_WIDGET(a->search_users_tab));
    gtk_box_append(GTK_BOX(stabs), GTK_WIDGET(a->search_videos_tab));

    GtkWidget *sbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(sbar, "tiko-search-bar");
    gtk_box_append(GTK_BOX(sbar), GTK_WIDGET(a->search_entry));
    gtk_box_append(GTK_BOX(sbar), stabs);

    a->search_revealer = GTK_REVEALER(gtk_revealer_new());
    gtk_revealer_set_transition_type(a->search_revealer, GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
    gtk_revealer_set_transition_duration(a->search_revealer, 180);
    gtk_revealer_set_reveal_child(a->search_revealer, FALSE);
    gtk_revealer_set_child(a->search_revealer, sbar);

    GtkWidget *top_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(top_box), hdr);
    gtk_box_append(GTK_BOX(top_box), GTK_WIDGET(a->search_revealer));

    a->top_revealer = GTK_REVEALER(gtk_revealer_new());
    gtk_revealer_set_transition_type(a->top_revealer, GTK_REVEALER_TRANSITION_TYPE_CROSSFADE);
    gtk_revealer_set_transition_duration(a->top_revealer, 220);
    gtk_revealer_set_reveal_child(a->top_revealer, TRUE);
    gtk_widget_set_valign(GTK_WIDGET(a->top_revealer), GTK_ALIGN_START);
    gtk_revealer_set_child(a->top_revealer, top_box);

    /* Thin Loading Progress Bar */
    a->progress = GTK_PROGRESS_BAR(gtk_progress_bar_new());
    gtk_widget_add_css_class(GTK_WIDGET(a->progress), "tiko-progress");
    gtk_widget_set_valign(GTK_WIDGET(a->progress), GTK_ALIGN_START);
    gtk_widget_set_visible(GTK_WIDGET(a->progress), FALSE);

    /* Overlay & Window Content */
    GtkWidget *overlay = gtk_overlay_new();
    gtk_overlay_set_child(GTK_OVERLAY(overlay), GTK_WIDGET(a->web));
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), GTK_WIDGET(a->top_revealer));
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), GTK_WIDGET(a->progress));

    a->toasts = ADW_TOAST_OVERLAY(adw_toast_overlay_new());
    adw_toast_overlay_set_child(a->toasts, overlay);
    adw_application_window_set_content(a->window, GTK_WIDGET(a->toasts));

    /* Signals */
    g_signal_connect(a->web, "notify::uri", G_CALLBACK(on_uri_changed), a);
    g_signal_connect(a->web, "notify::estimated-load-progress", G_CALLBACK(on_load_progress), a);
    g_signal_connect(a->web, "load-changed", G_CALLBACK(on_load_changed), a);
    g_signal_connect(a->web, "create", G_CALLBACK(on_web_create), a);

    g_signal_connect(a->back_btn, "clicked", G_CALLBACK(on_back_clicked), a);
    g_signal_connect(home_btn, "clicked", G_CALLBACK(on_home_clicked), a);
    g_signal_connect(follow_btn, "clicked", G_CALLBACK(on_following_clicked), a);
    g_signal_connect(acct_btn, "clicked", G_CALLBACK(on_account_clicked), a);
    g_signal_connect(a->creator_btn, "clicked", G_CALLBACK(on_creator_pill_clicked), a);
    g_signal_connect(a->search_btn, "toggled", G_CALLBACK(on_search_toggled), a);
    g_signal_connect(a->focus_btn, "toggled", G_CALLBACK(on_focus_toggled), a);
    g_signal_connect(a->search_entry, "activate", G_CALLBACK(on_search_activate), a);
    g_signal_connect(a->creators_clear_btn, "clicked", G_CALLBACK(on_creators_clear), a);
    g_signal_connect(cpop, "notify::visible", G_CALLBACK(on_popover_notify), a);

    GtkEventController *motion = gtk_event_controller_motion_new();
    g_signal_connect(motion, "motion", G_CALLBACK(on_pointer_motion), a);
    gtk_widget_add_controller(overlay, motion);

    GtkEventController *top_hover = gtk_event_controller_motion_new();
    g_signal_connect(top_hover, "enter", G_CALLBACK(on_chrome_enter), a);
    g_signal_connect(top_hover, "leave", G_CALLBACK(on_chrome_leave), a);
    gtk_widget_add_controller(top_box, top_hover);

    GtkEventController *app_keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(app_keys, GTK_PHASE_CAPTURE);
    g_signal_connect(app_keys, "key-pressed", G_CALLBACK(on_app_key_pressed), a);
    gtk_widget_add_controller(win, app_keys);

    GtkEventController *play_keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(play_keys, GTK_PHASE_BUBBLE);
    g_signal_connect(play_keys, "key-pressed", G_CALLBACK(on_playback_key_pressed), a);
    gtk_widget_add_controller(win, play_keys);

    g_signal_connect(win, "close-request", G_CALLBACK(on_close_req), a);

    refresh_creators_popover(a);
    g_tiko = a;
    navigate_to(a, (initial_url && initial_url[0]) ? initial_url : TIKTOK_FORYOU);
    schedule_hide_controls(a);
    gtk_window_present(GTK_WINDOW(win));
    return a;
}

static void on_startup(GApplication *app, gpointer ud) {
    (void)app; (void)ud;
    adw_style_manager_set_color_scheme(adw_style_manager_get_default(), ADW_COLOR_SCHEME_FORCE_DARK);
    GtkCssProvider *prov = gtk_css_provider_new();
    gtk_css_provider_load_from_string(prov, APP_CSS);
    GdkDisplay *d = gdk_display_get_default();
    if (d) {
        gtk_style_context_add_provider_for_display(
            d, GTK_STYLE_PROVIDER(prov), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
    g_object_unref(prov);
}

static void on_activate(GApplication *app, gpointer ud) {
    (void)ud;
    TikoApp *a = ensure_window(ADW_APPLICATION(app), NULL);
    gtk_window_present(GTK_WINDOW(a->window));
}

static void on_open(GApplication *app, GFile **files, gint n_files, const char *hint, gpointer ud) {
    (void)hint; (void)ud;
    char *target = NULL;
    if (n_files > 0 && files[0]) {
        char *uri = g_file_get_uri(files[0]);
        if (uri && (g_str_has_prefix(uri, "http://") || g_str_has_prefix(uri, "https://"))) {
            target = uri;
        } else {
            g_free(uri);
            char *base = g_file_get_basename(files[0]);
            target = tiko_build_target_url(base, true);
            g_free(base);
        }
    }
    TikoApp *a = ensure_window(ADW_APPLICATION(app), target);
    g_free(target);
    gtk_window_present(GTK_WINDOW(a->window));
}

static gboolean on_unix_signal(gpointer ud) {
    GApplication *app = ud;
    GList *wins = g_list_copy(gtk_application_get_windows(GTK_APPLICATION(app)));
    if (!wins) g_application_quit(app);
    for (GList *l = wins; l; l = l->next) {
        GtkWindow *w = l->data;
        if (gtk_widget_get_visible(GTK_WIDGET(w))) gtk_window_close(w);
        else gtk_window_destroy(w);
    }
    g_list_free(wins);
    return G_SOURCE_CONTINUE;
}

int main(int argc, char **argv) {
    AdwApplication *app = adw_application_new(APP_ID, G_APPLICATION_HANDLES_OPEN);
    g_signal_connect(app, "startup", G_CALLBACK(on_startup), NULL);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    g_signal_connect(app, "open", G_CALLBACK(on_open), NULL);
    g_unix_signal_add(SIGINT, on_unix_signal, app);
    g_unix_signal_add(SIGTERM, on_unix_signal, app);
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
