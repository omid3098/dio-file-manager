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
#define HEADER_H 46.0f               /* both header rows: they are the top of the window */
#define NARROW_BELOW 683.0f          /* GNOME Files 47 collapses at max-width 682sp: the sidebar leaves, Back, Forward and the view controls drop to a bottom bar */
#define BAR_H 46.0f                  /* that bottom bar, with its top line */

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
    uint64_t sel_bytes;              /* file sizes plus the folder sizes known so far */
    bool sel_pending;                /* a selected folder's size is still being measured */

    /* view settings */
    int view, zoom, last_icon;       /* zoom: the stop the continuous value rounds to (mode and layout style); view keeps the header shortcuts */
    float zv, last_icon_v;           /* the continuous view size, 0 (Details) to 5 (Extra large); last_icon_v is 2..5 */
    double size_t, fade_t;           /* when the icon size last changed; when the view mode last switched */
    bool thumb_settled;              /* the size has stayed still, so sharper thumbnails may be requested */
    unsigned frame_no;
    void *frame_log;                 /* test mode only: one line per frame */
    int probe_idx;                   /* test mode: the last built item and where it is drawn (document px) */
    float probe_x, probe_y;
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
void app_set_zoom(int stop);                /* jump to a stop (no glide) */
void app_set_zoomv(float v, bool glide);   /* the continuous value; glide eases there in 150 ms */
float app_icon_size(void);                 /* the icon edge in px for the current value */
bool app_animations(void);                 /* Windows' client-area animation setting */
float app_view_fade(void);                 /* opacity of the file view while a mode switch fades in */
bool app_zoom_busy(void);
double app_glide_seconds(void);            /* 0.15; a test can slow it down to take a frame strip */
cui_rect ui_grid_item_rect(int i, float width);   /* the item's drawn rectangle in document px, for hit tests */
bool ui_grid_gliding(void);                  /* a glide is running */
void ui_scroll_get(const char *id, float *y, float *ty, float *view_h);   /* a scroll area's state from the last frame */
void ui_scroll_jump(const char *id, float y);        /* set offset and target at once, no tween */
bool ui_wheel_scrolling(void);                        /* wheel input is scrolling this frame */
void ui_push_alpha(float a);                          /* draw everything until ui_pop_alpha at this opacity */
void ui_pop_alpha(void);
void ui_node_opacity(float o);                        /* the open container's opacity */
void ui_overlay_set(void (*fn)(cui_ctx *ui, cui_rect view, cui_vec2 scroll));   /* paint over the file view's scroll area, this frame */
void app_load_view(void);
void app_flush_view(void);
void ui_zoom_keys(void);
void ui_zoom_tick(void);                   /* once per frame, before the layout */
float app_zoom_wheel(void);
bool ui_view_options_open(void);
void ui_popup_tooltips(cui_ctx *ui);
void ui_item_tooltip(cui_ctx *ui, cui_id id, const char *name, cui_rect label);
void ui_text_clipboard(cui_ctx *ui);        /* text fields copy and paste through clip_open (retries while another program holds the clipboard) */
void ui_popup_snapshot(FILE *f);
bool ui_popup_open_any(cui_ctx *ui);
void app_path_of(int vis_index, char *out, size_t cap);
bool app_is_narrow(void);
void app_set_theme(int pref);              /* 0 system, 1 light, 2 dark */
void app_set_search_open(bool open);
bool app_at_place(int which);              /* is the sidebar place 0..6 (Home..Videos) the current folder */

/* ui_actions.c */
void act_jobs_snapshot(FILE *f);
bool ui_dialog_begin(cui_ctx *ui, const char *title, bool *open, const char *description, bool persistent, bool no_close_button); /* every dialog: names shortened by dlg_short_name, long words broken to fit */
void act_shutdown(void);
void ui_sidebar_snapshot(FILE *f);
void ui_sidebar_shutdown(void);
void act_pre_frame(void);
void act_keys(void);                        /* F2, Ctrl+Shift+N, Ctrl+C/X/V, Delete, Shift+Delete, Alt+Enter */
bool act_jobs_active(void);                 /* a file job is running: the header shows its ring */
void act_dismiss_popups(void);              /* a side-button press closed the popups: the operations popover must not reopen */
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
void ui_menu_eat_activation(void);   /* Enter and Space belong to the control that took them: the menu's highlighted row must not take them too */
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
unsigned theme_hairline(bool dark);        /* 0xRRGGBB of the pane hairline, also the window border */

/* chrome_win.c: the header is the window's title bar */
void chrome_init(void *hwnd, int client_w, int client_h, int min_w, int min_h);
bool chrome_message(void *hwnd, unsigned msg, uintptr_t w, intptr_t l, intptr_t *result);   /* true: handled, *result is the answer */
void chrome_begin_frame(void);
void chrome_nodrag(cui_rect r);             /* a header control: it takes clicks, the empty space around it drags the window */
void chrome_nodrag_last(void);              /* the component just built */
void chrome_window_buttons(void);           /* minimize, maximize and close at the end of the content header */
void chrome_veil(void);                     /* inside a 36 px stack: dims an icon without a colour option when the window is inactive */
cui_color chrome_icon_color(void);          /* .color of a header icon button: none when active, muted when not */
void chrome_set_active(bool active);
bool chrome_is_active(void);
void chrome_set_theme(bool dark);           /* the window border and the system menu follow the theme */
void chrome_snapshot(FILE *f);
void theme_load_fonts(cui_ctx *ui);

/* ui_icons.c */
void ui_draw_folder(cui_ctx *ui, float cx, float cy, float s);
void ui_draw_file(cui_ctx *ui, float cx, float cy, float s, const char *name, int kind);
const char *ui_kind_icon(int kind);
cui_color ui_kind_color(int kind);
cui_color ui_icon_ink(cui_ctx *ui, cui_color c, float min);   /* c, mixed toward the text colour until it reaches `min`:1 on every row background */
void ui_set_dim(bool dim);                                       /* draw the next file icons and names dimmed (incoming files) */
cui_color ui_dimmed(cui_ctx *ui, cui_color c);

/* ui_ctxmenu.c: keyboard in a scrolling menu */
void menu_frame_begin(void);                   /* start of each frame: no highlighted row yet */
bool popover_menu_begin(cui_ctx *ui, const char *id, int placement, float width, float pad, float gap);   /* cui_popover_begin as a menu popup: the arrow keys highlight its cui_menu_items */
void menu_rows_begin(const char *scroll_id);   /* before the scrolling column of a menu: its id and the first row */
bool menu_row(const char *label, bool chosen); /* around each cui_menu_item (same label): scrolls the column to the row the keyboard highlights */
void menu_snapshot(FILE *f);                   /* test state: the highlighted menu row and the rows' view */                    /* around each cui_menu_item: scrolls the column to the row the keyboard highlights */

/* ui_chrome.c */
void ui_sidebar_header(void);
void ui_sidebar_places(void);
void ui_default_fm_snapshot(FILE *f);       /* the main menu button and its switch, for real-input checks */
void ui_view_mode_snapshot(FILE *f);       /* the grid/list toggles of the open view-options menu, for real-input checks */
void app_toggle_default_fm(void);           /* main.c: the "Use as Default File Manager" switch */
bool app_default_fm(void);
void ui_content_header(void);
void ui_bottom_bar(void);          /* narrow windows: Back, Forward and the view controls */
void ui_search_bar(void);
void ui_state_view(const char *icon, const char *title, const char *detail);
void ui_status_pill(void);
const char *ui_pill_text(void);             /* test state: the pill's text of the last frame */

/* ui_sizes.c: folder sizes (engine: foldersize.c) */
void sizes_frame(void);                                       /* once per frame: ask for the shown folders, one re-sort when all are in */
void sizes_fill(void);                                        /* before a size sort: the known folder sizes into the entries */
int sizes_cell(const fs_entry *e, const char *name, char *buf, size_t cap);   /* 0 nothing to show, 1 text in buf, 2 still measuring */
void sizes_selection(uint64_t *bytes, bool *pending);         /* the selected folders' total */
bool sizes_settled(void);
int sizes_open(void);
void app_resort_keep(void);                                   /* main.c: sort again, keeping the selection and the scroll anchored */

/* ui_grid.c, ui_list.c */
void ui_grid_view(void);
#define UI_NAME_BYTES 1028   /* a file name is at most 1023 bytes (fs.c add_entry); a label line adds an ellipsis and the end 0 */
int ui_grid_label_lines(int idx, char out[][UI_NAME_BYTES]);   /* the lines of an item's grid label as they are drawn (test probe) */
void ui_column_view(void);
void ui_grid_keys(void);
void ui_list_view(void);
void ui_handle_open_keys(void);

#endif
