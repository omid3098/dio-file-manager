/* Shared state and calls of the window. One app, one window: the state is a single global. */
#ifndef DFM_UI_H
#define DFM_UI_H
#include <cui/cui.h>
#include <cui/cui_app.h>
#include "fs.h"

enum { LOC_FOLDER = 0, LOC_RECENT, LOC_STARRED, LOC_TRASH, LOC_OTHER };
enum { VIEW_GRID = 0, VIEW_LIST = 1 };
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
    int view, zoom;                  /* VIEW_*, 0..3 */
    int sort_key;
    bool sort_desc, show_hidden;
    bool search_open;
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
    int hover;
    double last_click_t;
    int last_click_idx;
    bool sidebar_open;               /* overlay in narrow windows */
    bool reset_scroll;
    cui_rect first_rect;             /* first built grid row, for the input smoke test */
    bool ctx_item;                   /* the last right click was on an item */
    void *hwnd;                      /* the native window, for shell calls */
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
void app_set_view(int view);
void app_path_of(int vis_index, char *out, size_t cap);
bool app_is_narrow(void);
void app_copy_text(const char *utf8);       /* to the Windows clipboard */
void app_set_theme(int pref);              /* 0 system, 1 light, 2 dark */
void app_set_search_open(bool open);
bool app_at_place(int which);              /* is the sidebar place 0..6 (Home..Videos) the current folder */

/* ui_actions.c */
void act_pre_frame(void);
void act_keys(void);                        /* F2, Ctrl+Shift+N, Ctrl+C/X/V, Delete, Shift+Delete, Alt+Enter */
void act_context_menu(void);                /* call once after a view; right clicks on items set A.ctx_item first */
void act_background(void);                  /* an empty-space canvas with the background menu; put it under the view */
void act_job_button(void);                  /* header: running-job ring and its popover */
void act_frame(void);                       /* inside the content stack: popovers, dialogs, job polling */
void act_list_ready(void);
void act_rename(void);
void act_new_folder(void);
void act_open_terminal(void);
bool act_modal_open(void);
int act_state(void);                        /* bit 0 popover, 1 job, 2 delete dialog, 3 conflict dialog, 4 undo */

/* theme.c */
void theme_apply(cui_ctx *ui, bool dark);
void theme_load_fonts(cui_ctx *ui);

/* ui_icons.c */
void ui_draw_folder(cui_ctx *ui, float cx, float cy, float s);
void ui_draw_file(cui_ctx *ui, float cx, float cy, float s, const char *name, int kind);
const char *ui_kind_icon(int kind);
cui_color ui_kind_color(int kind);

/* ui_chrome.c */
void ui_sidebar_header(void);
void ui_sidebar_places(void);
void ui_content_header(void);
void ui_search_bar(void);
void ui_state_view(const char *icon, const char *title, const char *detail);
void ui_status_pill(void);

/* ui_grid.c, ui_list.c */
void ui_grid_view(void);
void ui_grid_keys(void);
void ui_list_view(void);
void ui_handle_open_keys(void);

#endif
