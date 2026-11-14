/* Shared state and calls of the window. One app, one window: the state is a single global. */
#ifndef DFM_UI_H
#define DFM_UI_H
#include <cui/cui.h>
#include <cui/cui_app.h>
#include "fs.h"
#include <stdio.h>

enum { LOC_FOLDER = 0, LOC_RECENT, LOC_STARRED, LOC_TRASH, LOC_OTHER };
enum { VIEW_GRID = 0, VIEW_LIST = 1 };
enum { Z_DETAILS, Z_LIST, Z_SMALL, Z_MEDIUM, Z_LARGE, Z_EXTRA, Z_COUNT };
enum { HIST_MAX = 48 };

typedef struct app_t {
    cui_ctx *ui;

    /* where we are */
    int loc;                         /* LOC_* */
    char path[FS_PATH_MAX];          /* the folder when loc == LOC_FOLDER */
    char hist[HIST_MAX][FS_PATH_MAX]; /* path, or "@n" for LOC_n */
    int hist_n, hist_pos;

    /* what is in it */
    fs_list *list;                   /* the current listing; may still be loading */
    bool list_seen;                  /* the window has built its view of a finished listing */
    int *vis;                        /* indices into list->items after the filters */
    bool *sel;                       /* parallel to vis */
    int vis_n, vis_cap, sel_n;
    int cursor, anchor;              /* vis indices, -1 for none */
    uint64_t sel_bytes;

    /* view settings */
    int view, zoom, last_icon;       /* zoom is the six-stop scale; view keeps the header shortcuts */
    int sort_key;
    bool sort_desc, show_hidden;
    bool search_open, searching, search_focus; /* focus again when reopening the cached field */
    char search[256], search_applied[256];
    int theme_pref;                  /* 0 system, 1 light, 2 dark */
    bool dark;

    /* places */
    char places[7][FS_PATH_MAX];
    fs_drive drives[26];
    int drive_n;

    /* layout facts from the last frame */
    float content_w, content_h;
    int cols, first_row, last_row;
    float cell_h;
    int column_rows;                /* List fills each column from top to bottom */
    cui_rect zoom_rect;             /* slider track, for real-input checks */
    int hover;
    double last_click_t;
    int last_click_idx;
    bool sidebar_open;               /* overlay in narrow windows */
    bool reset_scroll;
    cui_rect first_rect;             /* first built grid row, for the input smoke test */
    bool ctx_item;                   /* the last right click was on an item */
    void *hwnd;                      /* the native window, for shell calls */
    int trash_n;                     /* items in the Recycle Bin, for the sidebar */
    double trash_check_t;
    bool sort_swapped, saved_desc;   /* Recent and Trash open sorted by date; the folders' sort comes back after */
    int saved_key;
} app_t;

extern app_t A;

/* main.c */
void app_navigate(const char *target);      /* a folder path, or "@1".."@4" for a special place */
void app_back(void);
void app_forward(void);
void app_up(void);
void app_reload(void);
void app_rebuild_view(void);                /* after sort, filter or search changes */
void app_resort(void);
void app_open_vis(int vis_index);
const fs_entry *app_item(int vis_index);
const char *app_item_name(int vis_index);
void app_select_only(int vis_index);
void app_select_range(int from, int to);
void app_select_all(void);
void app_clear_selection(void);
void app_count_selection(void);
void app_reload_quiet(void);   /* relist the folder, keeping selection and scroll; used while a copy writes into it */
void app_set_view(int view);
void app_set_zoom(int stop);
void app_load_view(void);
void app_flush_view(void);
void ui_zoom_keys(void);
float app_zoom_wheel(void);
bool ui_view_options_open(void);
void ui_popup_tooltips(cui_ctx *ui);
void ui_text_clipboard(cui_ctx *ui);        /* text fields copy and paste through clip_open (retries while another program holds the clipboard) */
void ui_popup_snapshot(FILE *f);
void app_path_of(int vis_index, char *out, size_t cap);
bool app_is_narrow(void);
void app_set_theme(int pref);              /* 0 system, 1 light, 2 dark */
void app_set_search_open(bool open);
bool app_at_place(int which);              /* is the sidebar place 0..6 (Home..Videos) the current folder */

/* ui_actions.c */
void act_jobs_snapshot(FILE *f);
void act_shutdown(void);
void ui_sidebar_snapshot(FILE *f);
void ui_sidebar_shutdown(void);
void act_pre_frame(void);
void act_keys(void);                        /* F2, Ctrl+Shift+N, Ctrl+C/X/V, Delete, Shift+Delete, Alt+Enter */
void act_job_button(void);                  /* header: running-job ring and its popover */
void act_frame(void);                       /* inside the content stack: popovers, dialogs, job polling */
void act_list_ready(void);
void act_rename(void);
void act_new_folder(void);
void act_open_terminal(void);
bool act_modal_open(void);
int act_state(void);                        /* bit 0 popover, 1 job, 2 delete dialog, 3 conflict dialog, 4 undo */
void act_test_snapshot(FILE *f);             /* observation only, for real-input checks */

/* ui_views.c */
void ui_draw_star(cui_ctx *ui, float cx, float cy, float r, cui_color c);
bool app_is_starred(int vis_index);
void app_star_toggle_row(int vis_index);
void app_star_toggle_selection(void);
bool app_selection_all_starred(void);
void view_location_text(int vis_index, char *out, size_t cap);
void views_open_location(void);
void views_restore_selection(void);
void views_ask_delete(void);
void views_ask_empty(void);
void views_trash_menu(void);
void views_frame(void);
bool views_modal_open(void);
int views_state(void);
void ui_trash_bar(void);
void ui_other_locations(void);
void act_reselect(const char *name);

/* ui_ctxmenu.c */
void ui_anchor_passthrough(cui_ctx *ui);
void ui_window_callback(cui_app *app, void (*frame)(cui_ctx *, void *), void *user);
bool ui_window_screenshot(cui_app *app, const char *path);
cui_rect ctxmenu_row_rect(cui_ctx *ui);
bool ctxmenu_row_hovered(cui_ctx *ui);       /* call immediately after cui_data_grid_row */
bool ctxmenu_begin(cui_ctx *ui, const char *id, bool open_here);
void ctxmenu_end(cui_ctx *ui);
bool ctxmenu_is_open(cui_ctx *ui);
void ui_search_focus(void);
void ui_view_options_surface(void);
void ui_columns_begin(int *rows, int *first, int *last);
void ui_columns_end(void);
void ui_columns_reveal(int column);
void ui_band_begin(void);                  /* inside the file view's scroll container, before its rows */
void ui_band_cancel(void);
bool ui_band_dragging(void);

/* ui_search.c: the Location column text of a search result */
void search_location_text(int vis, char *out, size_t cap);

/* ui_search.c */
void fsearch_changed(void);                 /* the query text changed */
void fsearch_frame(void);
void fsearch_end(void);                     /* back to the folder's own list; the caller rebuilds the view */
void fsearch_refresh(void);                 /* drop missing results and refresh the saved folder */
bool fsearch_running(void);
void fsearch_empty_view(void);

/* ui_thumbs.c */
typedef struct ui_thumb { cui_texture tex; int w, h; } ui_thumb;
bool ui_thumb_wanted(const fs_entry *e, const char *name);
int ui_thumb_px(float icon);
void ui_thumb_begin(void);
bool ui_thumb_lookup(const char *path, uint64_t mtime, int px, ui_thumb *out);
void ui_thumb_end(void);

/* theme.c */
void theme_apply(cui_ctx *ui, bool dark);
void theme_load_fonts(cui_ctx *ui);

/* ui_icons.c */
void ui_draw_folder(cui_ctx *ui, float cx, float cy, float s);
void ui_draw_file(cui_ctx *ui, float cx, float cy, float s, const char *name, int kind);
const char *ui_kind_icon(int kind);
cui_color ui_kind_color(int kind);
cui_color ui_icon_ink(cui_ctx *ui, cui_color c, float min);   /* c, mixed toward the text colour until it reaches `min`:1 on every row background */
void ui_set_dim(bool dim);                                       /* draw the next file icons and names dimmed (incoming files) */
cui_color ui_dimmed(cui_ctx *ui, cui_color c);

/* ui_chrome.c */
void ui_sidebar_header(void);
void ui_sidebar_places(void);
void ui_default_fm_snapshot(FILE *f);       /* the main menu button and its switch, for real-input checks */
void app_toggle_default_fm(void);           /* main.c: the "Use as Default File Manager" switch */
bool app_default_fm(void);
void ui_content_header(void);
void ui_search_bar(void);
void ui_state_view(const char *icon, const char *title, const char *detail);
void ui_status_pill(void);

/* ui_grid.c, ui_list.c */
void ui_grid_view(void);
void ui_column_view(void);
void ui_grid_keys(void);
void ui_list_view(void);
void ui_handle_open_keys(void);

#endif
