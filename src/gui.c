#include "gui.h"

#include <commctrl.h>
#include <windowsx.h>
#include <stdarg.h>
#include <stdlib.h>
#include <strsafe.h>
#include <uxtheme.h>
#include "detection.h"
#include "event_queue.h"
#include "event_store.h"
#include "export.h"
#include "logger.h"
#include "resource.h"
#include "telemetry.h"
#include "traceglass.h"
#include "utils.h"
#include "version.h"

#define IDC_NAVIGATION 1001
#define IDC_SEARCH_EDIT 1002
#define IDC_EVENT_FILTER 1003
#define IDC_PAUSE_BUTTON 1004
#define IDC_CLEAR_BUTTON 1005
#define IDC_EXPORT_BUTTON 1006
#define IDC_ABOUT_BUTTON 1007
#define IDC_PROCESS_TREE 1010
#define IDC_TIMELINE_LIST 1011
#define IDC_NETWORK_LIST 1012
#define IDC_ALERT_LIST 1013
#define IDC_RECENT_LIST 1014
#define IDC_RECENT_ALERT_LIST 1015
#define IDC_STATUS_BAR 1016

#define IDC_DETAILS_EDIT 1101
#define IDC_DETAILS_COPY 1102
#define IDC_DETAILS_CLOSE 1103

#define IDM_COPY_ROW 2001
#define IDM_COPY_PROCESS 2002
#define IDM_COPY_PID 2003
#define IDM_COPY_REMOTE 2004

#define TIMER_STATUS 1
#define TIMER_FILTER 2
#define FILTER_DELAY_MS 250U
#define STATUS_INTERVAL_MS 1000U
#define RECENT_EVENT_LIMIT 8
#define RECENT_ALERT_LIMIT 5

typedef enum GuiView {
    VIEW_OVERVIEW = 0,
    VIEW_PROCESSES,
    VIEW_NETWORK,
    VIEW_TIMELINE,
    VIEW_ALERTS,
    VIEW_COUNT
} GuiView;

typedef struct SortState {
    int column;
    BOOL ascending;
    BOOL active;
    GuiView view;
} SortState;

typedef struct DetailsState {
    TraceGlassEvent event;
    HWND edit;
    HWND copy_button;
    HWND close_button;
    HFONT font;
} DetailsState;

typedef struct GuiState {
    HINSTANCE instance;
    HWND window;
    HWND title_label;
    HWND subtitle_label;
    HWND monitoring_label;
    HWND navigation;
    HWND search_label;
    HWND search_edit;
    HWND event_label;
    HWND event_filter;
    HWND pause_button;
    HWND clear_button;
    HWND export_button;
    HWND about_button;
    HWND tooltip;

    HWND overview_status_group;
    HWND overview_activity_group;
    HWND overview_alert_group;
    HWND metric_names[6];
    HWND metric_values[6];
    HWND telemetry_note;
    HWND recent_list;
    HWND recent_alert_list;

    HWND process_tree;
    HWND timeline_list;
    HWND network_list;
    HWND alert_list;
    HWND status_bar;

    HFONT font;
    HFONT small_font;
    HFONT heading_font;
    HFONT title_font;
    UINT dpi;

    EventQueue queue;
    EventStore store;
    TelemetryEngine telemetry;
    SortState sort_states[VIEW_COUNT];
    size_t view_floor[VIEW_COUNT];
    ULONGLONG session_started_tick;
    ULONGLONG transient_until;
    WCHAR transient_status[128];

    BOOL queue_ready;
    BOOL store_ready;
    BOOL telemetry_ready;
    BOOL rendering_paused;
    BOOL render_dirty;
    BOOL filter_timer_active;
    BOOL shutting_down;
} GuiState;

static GuiState g_gui;

static int scale_value(int value) {
    return MulDiv(value, (int)(g_gui.dpi == 0 ? 96 : g_gui.dpi), 96);
}

static HFONT create_ui_font(int point_size, int weight) {
    return CreateFontW(
        -MulDiv(point_size, (int)(g_gui.dpi == 0 ? 96 : g_gui.dpi), 72),
        0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );
}

static void set_control_font(HWND control, HFONT font) {
    if (control != NULL && font != NULL) {
        SendMessageW(control, WM_SETFONT, (WPARAM)font, TRUE);
    }
}

static void apply_fonts(void) {
    size_t index;
    HWND regular_controls[] = {
        g_gui.monitoring_label, g_gui.navigation, g_gui.search_label,
        g_gui.search_edit, g_gui.event_label, g_gui.event_filter,
        g_gui.pause_button, g_gui.clear_button, g_gui.export_button,
        g_gui.about_button, g_gui.process_tree, g_gui.timeline_list,
        g_gui.network_list, g_gui.alert_list, g_gui.recent_list,
        g_gui.recent_alert_list, g_gui.status_bar, g_gui.telemetry_note
    };
    HWND heading_controls[] = {
        g_gui.overview_status_group,
        g_gui.overview_activity_group,
        g_gui.overview_alert_group
    };
    set_control_font(g_gui.title_label, g_gui.title_font);
    set_control_font(g_gui.subtitle_label, g_gui.small_font);
    for (index = 0; index < ARRAYSIZE(regular_controls); ++index) {
        set_control_font(regular_controls[index], g_gui.font);
    }
    for (index = 0; index < ARRAYSIZE(heading_controls); ++index) {
        set_control_font(heading_controls[index], g_gui.heading_font);
    }
    for (index = 0; index < ARRAYSIZE(g_gui.metric_names); ++index) {
        set_control_font(g_gui.metric_names[index], g_gui.font);
        set_control_font(g_gui.metric_values[index], g_gui.heading_font);
    }
}

static BOOL recreate_fonts(void) {
    HFONT old_font = g_gui.font;
    HFONT old_small_font = g_gui.small_font;
    HFONT old_heading_font = g_gui.heading_font;
    HFONT old_title_font = g_gui.title_font;
    HFONT font = create_ui_font(10, FW_NORMAL);
    HFONT small_font = create_ui_font(9, FW_NORMAL);
    HFONT heading_font = create_ui_font(10, FW_SEMIBOLD);
    HFONT title_font = create_ui_font(20, FW_SEMIBOLD);
    if (font == NULL || small_font == NULL || heading_font == NULL || title_font == NULL) {
        if (font != NULL) DeleteObject(font);
        if (small_font != NULL) DeleteObject(small_font);
        if (heading_font != NULL) DeleteObject(heading_font);
        if (title_font != NULL) DeleteObject(title_font);
        return FALSE;
    }
    g_gui.font = font;
    g_gui.small_font = small_font;
    g_gui.heading_font = heading_font;
    g_gui.title_font = title_font;
    apply_fonts();
    if (old_font != NULL) DeleteObject(old_font);
    if (old_small_font != NULL) DeleteObject(old_small_font);
    if (old_heading_font != NULL) DeleteObject(old_heading_font);
    if (old_title_font != NULL) DeleteObject(old_title_font);
    return TRUE;
}

static void destroy_fonts(void) {
    if (g_gui.font != NULL) DeleteObject(g_gui.font);
    if (g_gui.small_font != NULL) DeleteObject(g_gui.small_font);
    if (g_gui.heading_font != NULL) DeleteObject(g_gui.heading_font);
    if (g_gui.title_font != NULL) DeleteObject(g_gui.title_font);
    g_gui.font = NULL;
    g_gui.small_font = NULL;
    g_gui.heading_font = NULL;
    g_gui.title_font = NULL;
}

static HWND create_static_control(HWND parent, const WCHAR *text, DWORD style) {
    return CreateWindowExW(
        0, L"STATIC", text, WS_CHILD | WS_VISIBLE | style,
        0, 0, 0, 0, parent, NULL, g_gui.instance, NULL
    );
}

static HWND create_button_control(HWND parent, int control_id, const WCHAR *text) {
    return CreateWindowExW(
        0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        0, 0, 0, 0, parent, (HMENU)(INT_PTR)control_id, g_gui.instance, NULL
    );
}

static void add_list_column(HWND list, int index, int width, const WCHAR *text) {
    LVCOLUMNW column;
    ZeroMemory(&column, sizeof(column));
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    column.pszText = (LPWSTR)text;
    column.cx = width;
    column.iSubItem = index;
    ListView_InsertColumn(list, index, &column);
}

static HWND create_report_list(HWND parent, int control_id) {
    HWND list = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS,
        0, 0, 0, 0, parent, (HMENU)(INT_PTR)control_id, g_gui.instance, NULL
    );
    if (list != NULL) {
        ListView_SetExtendedListViewStyle(
            list,
            LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP |
                LVS_EX_GRIDLINES
        );
        SetWindowTheme(list, L"Explorer", NULL);
    }
    return list;
}

static void add_tooltip(HWND control, const WCHAR *text) {
    TOOLINFOW info;
    if (g_gui.tooltip == NULL || control == NULL) {
        return;
    }
    ZeroMemory(&info, sizeof(info));
    info.cbSize = sizeof(info);
    info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    info.hwnd = g_gui.window;
    info.uId = (UINT_PTR)control;
    info.lpszText = (LPWSTR)text;
    SendMessageW(g_gui.tooltip, TTM_ADDTOOLW, 0, (LPARAM)&info);
}

static BOOL create_controls(HWND window) {
    static const WCHAR *metric_labels[] = {
        L"Telemetry", L"Events", L"Processes",
        L"TCP Connections", L"Alerts", L"Uptime"
    };
    static const WCHAR *tabs[] = {
        L"Overview", L"Processes", L"Network", L"Timeline", L"Alerts"
    };
    TCITEMW tab_item;
    size_t index;

    g_gui.title_label = create_static_control(window, L"TraceGlass", SS_LEFT);
    g_gui.subtitle_label = create_static_control(
        window, L"Windows Behavioral Telemetry Viewer", SS_LEFT
    );
    g_gui.monitoring_label = create_static_control(
        window, L"\x25CF Live Monitoring", SS_RIGHT
    );

    g_gui.navigation = CreateWindowExW(
        0, WC_TABCONTROLW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_CLIPSIBLINGS | TCS_FOCUSNEVER,
        0, 0, 0, 0, window, (HMENU)(INT_PTR)IDC_NAVIGATION, g_gui.instance, NULL
    );
    ZeroMemory(&tab_item, sizeof(tab_item));
    tab_item.mask = TCIF_TEXT;
    for (index = 0; index < ARRAYSIZE(tabs); ++index) {
        tab_item.pszText = (LPWSTR)tabs[index];
        TabCtrl_InsertItem(g_gui.navigation, (int)index, &tab_item);
    }

    g_gui.search_label = create_static_control(window, L"Search:", SS_LEFT | SS_CENTERIMAGE);
    g_gui.search_edit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        0, 0, 0, 0, window, (HMENU)(INT_PTR)IDC_SEARCH_EDIT, g_gui.instance, NULL
    );
    SendMessageW(
        g_gui.search_edit,
        EM_SETCUEBANNER,
        TRUE,
        (LPARAM)L"Process, PID, IP, port or details"
    );
    g_gui.event_label = create_static_control(window, L"Event:", SS_LEFT | SS_CENTERIMAGE);
    g_gui.event_filter = CreateWindowExW(
        0, WC_COMBOBOXW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        0, 0, 0, 0, window, (HMENU)(INT_PTR)IDC_EVENT_FILTER, g_gui.instance, NULL
    );
    SendMessageW(g_gui.event_filter, CB_ADDSTRING, 0, (LPARAM)L"All");
    SendMessageW(g_gui.event_filter, CB_ADDSTRING, 0, (LPARAM)L"Process");
    SendMessageW(g_gui.event_filter, CB_ADDSTRING, 0, (LPARAM)L"Network");
    SendMessageW(g_gui.event_filter, CB_ADDSTRING, 0, (LPARAM)L"Alert");
    SendMessageW(g_gui.event_filter, CB_SETCURSEL, 0, 0);

    g_gui.pause_button = create_button_control(window, IDC_PAUSE_BUTTON, L"Pause");
    g_gui.clear_button = create_button_control(window, IDC_CLEAR_BUTTON, L"Clear View");
    g_gui.export_button = create_button_control(window, IDC_EXPORT_BUTTON, L"Export");
    g_gui.about_button = create_button_control(window, IDC_ABOUT_BUTTON, L"About");

    g_gui.overview_status_group = CreateWindowExW(
        0, L"BUTTON", L"Monitoring Status",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        0, 0, 0, 0, window, NULL, g_gui.instance, NULL
    );
    g_gui.overview_activity_group = CreateWindowExW(
        0, L"BUTTON", L"Recent Activity",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        0, 0, 0, 0, window, NULL, g_gui.instance, NULL
    );
    g_gui.overview_alert_group = CreateWindowExW(
        0, L"BUTTON", L"Recent Alerts",
        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        0, 0, 0, 0, window, NULL, g_gui.instance, NULL
    );
    for (index = 0; index < ARRAYSIZE(metric_labels); ++index) {
        g_gui.metric_names[index] = create_static_control(
            window, metric_labels[index], SS_LEFT | SS_CENTERIMAGE
        );
        g_gui.metric_values[index] = create_static_control(
            window, index == 0 ? L"Starting" : L"0", SS_RIGHT | SS_CENTERIMAGE
        );
    }
    g_gui.telemetry_note = create_static_control(
        window, L"Starting telemetry...", SS_LEFT
    );

    g_gui.recent_list = create_report_list(window, IDC_RECENT_LIST);
    add_list_column(g_gui.recent_list, 0, 92, L"Time");
    add_list_column(g_gui.recent_list, 1, 500, L"Activity");
    g_gui.recent_alert_list = create_report_list(window, IDC_RECENT_ALERT_LIST);
    add_list_column(g_gui.recent_alert_list, 0, 88, L"Time");
    add_list_column(g_gui.recent_alert_list, 1, 75, L"Severity");
    add_list_column(g_gui.recent_alert_list, 2, 220, L"Rule");
    add_list_column(g_gui.recent_alert_list, 3, 180, L"Process");

    g_gui.process_tree = CreateWindowExW(
        WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASBUTTONS | TVS_HASLINES |
            TVS_LINESATROOT | TVS_SHOWSELALWAYS,
        0, 0, 0, 0, window, (HMENU)(INT_PTR)IDC_PROCESS_TREE,
        g_gui.instance, NULL
    );
    SetWindowTheme(g_gui.process_tree, L"Explorer", NULL);
    TreeView_SetExtendedStyle(g_gui.process_tree, TVS_EX_DOUBLEBUFFER, TVS_EX_DOUBLEBUFFER);

    g_gui.network_list = create_report_list(window, IDC_NETWORK_LIST);
    add_list_column(g_gui.network_list, 0, 92, L"Time");
    add_list_column(g_gui.network_list, 1, 170, L"Process");
    add_list_column(g_gui.network_list, 2, 75, L"PID");
    add_list_column(g_gui.network_list, 3, 80, L"Protocol");
    add_list_column(g_gui.network_list, 4, 200, L"Local Address");
    add_list_column(g_gui.network_list, 5, 200, L"Remote Address");
    add_list_column(g_gui.network_list, 6, 120, L"State");

    g_gui.timeline_list = create_report_list(window, IDC_TIMELINE_LIST);
    add_list_column(g_gui.timeline_list, 0, 92, L"Time");
    add_list_column(g_gui.timeline_list, 1, 95, L"Type");
    add_list_column(g_gui.timeline_list, 2, 180, L"Process");
    add_list_column(g_gui.timeline_list, 3, 75, L"PID");
    add_list_column(g_gui.timeline_list, 4, 600, L"Details");

    g_gui.alert_list = create_report_list(window, IDC_ALERT_LIST);
    add_list_column(g_gui.alert_list, 0, 92, L"Time");
    add_list_column(g_gui.alert_list, 1, 85, L"Severity");
    add_list_column(g_gui.alert_list, 2, 230, L"Rule");
    add_list_column(g_gui.alert_list, 3, 180, L"Process");
    add_list_column(g_gui.alert_list, 4, 550, L"Details");

    g_gui.status_bar = CreateWindowExW(
        0, STATUSCLASSNAMEW, L"Starting telemetry...",
        WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0, window, (HMENU)(INT_PTR)IDC_STATUS_BAR,
        g_gui.instance, NULL
    );
    SendMessageW(g_gui.status_bar, SB_SETMINHEIGHT, (WPARAM)scale_value(24), 0);

    g_gui.tooltip = CreateWindowExW(
        WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        window, NULL, g_gui.instance, NULL
    );
    add_tooltip(g_gui.pause_button, L"Pause GUI rendering. Telemetry collection continues.");
    add_tooltip(g_gui.clear_button, L"Clear only the selected view. Collection and stored events continue.");
    add_tooltip(g_gui.export_button, L"Export all events collected during this session.");
    add_tooltip(g_gui.about_button, L"Show TraceGlass version and build information.");

    apply_fonts();
    return g_gui.title_label != NULL && g_gui.subtitle_label != NULL &&
        g_gui.monitoring_label != NULL && g_gui.navigation != NULL &&
        g_gui.search_edit != NULL && g_gui.event_filter != NULL &&
        g_gui.pause_button != NULL && g_gui.clear_button != NULL &&
        g_gui.export_button != NULL && g_gui.about_button != NULL &&
        g_gui.overview_status_group != NULL && g_gui.recent_list != NULL &&
        g_gui.recent_alert_list != NULL && g_gui.process_tree != NULL &&
        g_gui.network_list != NULL && g_gui.timeline_list != NULL &&
        g_gui.alert_list != NULL && g_gui.status_bar != NULL;
}

static void set_list_column_widths(HWND list, const int *widths, int count) {
    int index;
    for (index = 0; index < count; ++index) {
        ListView_SetColumnWidth(list, index, widths[index]);
    }
}

static void resize_list_columns(void) {
    RECT rectangle;
    int width;
    int timeline_widths[5];
    int network_widths[7];
    int alert_widths[5];
    int activity_widths[2];
    int recent_alert_widths[4];

    if (GetClientRect(g_gui.timeline_list, &rectangle)) {
        width = rectangle.right - rectangle.left - scale_value(4);
        timeline_widths[0] = scale_value(92);
        timeline_widths[1] = scale_value(95);
        timeline_widths[2] = scale_value(180);
        timeline_widths[3] = scale_value(75);
        timeline_widths[4] = width - timeline_widths[0] - timeline_widths[1] -
            timeline_widths[2] - timeline_widths[3];
        if (timeline_widths[4] < scale_value(260)) timeline_widths[4] = scale_value(260);
        set_list_column_widths(g_gui.timeline_list, timeline_widths, ARRAYSIZE(timeline_widths));
    }
    if (GetClientRect(g_gui.network_list, &rectangle)) {
        width = rectangle.right - rectangle.left - scale_value(4);
        network_widths[0] = scale_value(92);
        network_widths[1] = scale_value(170);
        network_widths[2] = scale_value(75);
        network_widths[3] = scale_value(80);
        network_widths[4] = scale_value(205);
        network_widths[5] = scale_value(205);
        network_widths[6] = width - network_widths[0] - network_widths[1] -
            network_widths[2] - network_widths[3] - network_widths[4] - network_widths[5];
        if (network_widths[6] < scale_value(130)) network_widths[6] = scale_value(130);
        set_list_column_widths(g_gui.network_list, network_widths, ARRAYSIZE(network_widths));
    }
    if (GetClientRect(g_gui.alert_list, &rectangle)) {
        width = rectangle.right - rectangle.left - scale_value(4);
        alert_widths[0] = scale_value(92);
        alert_widths[1] = scale_value(85);
        alert_widths[2] = scale_value(230);
        alert_widths[3] = scale_value(180);
        alert_widths[4] = width - alert_widths[0] - alert_widths[1] -
            alert_widths[2] - alert_widths[3];
        if (alert_widths[4] < scale_value(260)) alert_widths[4] = scale_value(260);
        set_list_column_widths(g_gui.alert_list, alert_widths, ARRAYSIZE(alert_widths));
    }
    if (GetClientRect(g_gui.recent_list, &rectangle)) {
        width = rectangle.right - rectangle.left - scale_value(4);
        activity_widths[0] = scale_value(92);
        activity_widths[1] = width - activity_widths[0];
        if (activity_widths[1] < scale_value(180)) activity_widths[1] = scale_value(180);
        set_list_column_widths(g_gui.recent_list, activity_widths, ARRAYSIZE(activity_widths));
    }
    if (GetClientRect(g_gui.recent_alert_list, &rectangle)) {
        width = rectangle.right - rectangle.left - scale_value(4);
        recent_alert_widths[0] = scale_value(88);
        recent_alert_widths[1] = scale_value(75);
        recent_alert_widths[2] = scale_value(230);
        recent_alert_widths[3] = width - recent_alert_widths[0] -
            recent_alert_widths[1] - recent_alert_widths[2];
        if (recent_alert_widths[3] < scale_value(150)) recent_alert_widths[3] = scale_value(150);
        set_list_column_widths(
            g_gui.recent_alert_list,
            recent_alert_widths,
            ARRAYSIZE(recent_alert_widths)
        );
    }
}

static void layout_controls(HWND window) {
    RECT client;
    RECT status_rectangle;
    RECT page;
    int width;
    int height;
    int padding = scale_value(16);
    int gap = scale_value(12);
    int header_height = scale_value(78);
    int status_height = scale_value(26);
    int navigation_top;
    int navigation_height;
    int toolbar_top;
    int toolbar_height = scale_value(32);
    int content_top;
    int content_height;
    int control_height = scale_value(30);
    int right;
    int about_width = scale_value(68);
    int export_width = scale_value(76);
    int clear_width = scale_value(94);
    int pause_width = scale_value(78);
    int combo_width = scale_value(126);
    int event_label_width = scale_value(50);
    int search_label_width = scale_value(58);
    int search_edit_right;
    int status_group_width;
    int right_group_x;
    int right_group_width;
    int activity_height;
    int group_inset = scale_value(14);
    int metric_row_height = scale_value(39);
    int metric_top;
    int metric_index;
    int status_parts[5];

    GetClientRect(window, &client);
    width = client.right - client.left;
    height = client.bottom - client.top;
    SendMessageW(g_gui.status_bar, WM_SIZE, 0, 0);
    if (GetWindowRect(g_gui.status_bar, &status_rectangle)) {
        status_height = status_rectangle.bottom - status_rectangle.top;
    }

    MoveWindow(g_gui.title_label, padding, scale_value(12), scale_value(360), scale_value(34), TRUE);
    MoveWindow(g_gui.subtitle_label, padding + scale_value(2), scale_value(48),
        scale_value(420), scale_value(20), TRUE);
    MoveWindow(g_gui.monitoring_label, width - padding - scale_value(300),
        scale_value(25), scale_value(300), scale_value(24), TRUE);

    navigation_top = header_height;
    navigation_height = height - navigation_top - status_height - scale_value(4);
    if (navigation_height < scale_value(300)) navigation_height = scale_value(300);
    MoveWindow(g_gui.navigation, padding, navigation_top, width - 2 * padding,
        navigation_height, TRUE);

    GetClientRect(g_gui.navigation, &page);
    TabCtrl_AdjustRect(g_gui.navigation, FALSE, &page);
    OffsetRect(&page, padding, navigation_top);
    toolbar_top = page.top + scale_value(9);
    right = page.right - scale_value(10);

    MoveWindow(g_gui.about_button, right - about_width, toolbar_top,
        about_width, control_height, TRUE);
    right -= about_width + scale_value(8);
    MoveWindow(g_gui.export_button, right - export_width, toolbar_top,
        export_width, control_height, TRUE);
    right -= export_width + scale_value(8);
    MoveWindow(g_gui.clear_button, right - clear_width, toolbar_top,
        clear_width, control_height, TRUE);
    right -= clear_width + scale_value(8);
    MoveWindow(g_gui.pause_button, right - pause_width, toolbar_top,
        pause_width, control_height, TRUE);
    right -= pause_width + gap;
    MoveWindow(g_gui.event_filter, right - combo_width, toolbar_top,
        combo_width, scale_value(180), TRUE);
    right -= combo_width + scale_value(5);
    MoveWindow(g_gui.event_label, right - event_label_width, toolbar_top,
        event_label_width, toolbar_height, TRUE);
    right -= event_label_width + gap;
    MoveWindow(g_gui.search_label, page.left + scale_value(10), toolbar_top,
        search_label_width, toolbar_height, TRUE);
    search_edit_right = right;
    if (search_edit_right - (page.left + scale_value(10) + search_label_width) < scale_value(170)) {
        search_edit_right = page.left + scale_value(10) + search_label_width + scale_value(170);
    }
    MoveWindow(g_gui.search_edit, page.left + scale_value(10) + search_label_width,
        toolbar_top, search_edit_right - page.left - scale_value(10) - search_label_width,
        control_height, TRUE);

    content_top = toolbar_top + toolbar_height + scale_value(10);
    content_height = page.bottom - content_top - scale_value(10);
    if (content_height < scale_value(180)) content_height = scale_value(180);

    MoveWindow(g_gui.process_tree, page.left + scale_value(10), content_top,
        page.right - page.left - scale_value(20), content_height, TRUE);
    MoveWindow(g_gui.network_list, page.left + scale_value(10), content_top,
        page.right - page.left - scale_value(20), content_height, TRUE);
    MoveWindow(g_gui.timeline_list, page.left + scale_value(10), content_top,
        page.right - page.left - scale_value(20), content_height, TRUE);
    MoveWindow(g_gui.alert_list, page.left + scale_value(10), content_top,
        page.right - page.left - scale_value(20), content_height, TRUE);

    status_group_width = (page.right - page.left - scale_value(30)) * 36 / 100;
    if (status_group_width < scale_value(310)) status_group_width = scale_value(310);
    right_group_x = page.left + scale_value(10) + status_group_width + gap;
    right_group_width = page.right - scale_value(10) - right_group_x;
    activity_height = (content_height - gap) * 58 / 100;

    MoveWindow(g_gui.overview_status_group, page.left + scale_value(10), content_top,
        status_group_width, content_height, TRUE);
    MoveWindow(g_gui.overview_activity_group, right_group_x, content_top,
        right_group_width, activity_height, TRUE);
    MoveWindow(g_gui.overview_alert_group, right_group_x,
        content_top + activity_height + gap, right_group_width,
        content_height - activity_height - gap, TRUE);

    metric_top = content_top + scale_value(30);
    for (metric_index = 0; metric_index < 6; ++metric_index) {
        MoveWindow(g_gui.metric_names[metric_index],
            page.left + scale_value(10) + group_inset,
            metric_top + metric_index * metric_row_height,
            status_group_width / 2 - group_inset,
            scale_value(26), TRUE);
        MoveWindow(g_gui.metric_values[metric_index],
            page.left + scale_value(10) + status_group_width / 2,
            metric_top + metric_index * metric_row_height,
            status_group_width / 2 - group_inset,
            scale_value(26), TRUE);
    }
    MoveWindow(g_gui.telemetry_note,
        page.left + scale_value(10) + group_inset,
        metric_top + 6 * metric_row_height + scale_value(12),
        status_group_width - 2 * group_inset,
        content_top + content_height -
            (metric_top + 6 * metric_row_height + scale_value(20)), TRUE);

    MoveWindow(g_gui.recent_list, right_group_x + group_inset,
        content_top + scale_value(27), right_group_width - 2 * group_inset,
        activity_height - scale_value(40), TRUE);
    MoveWindow(g_gui.recent_alert_list, right_group_x + group_inset,
        content_top + activity_height + gap + scale_value(27),
        right_group_width - 2 * group_inset,
        content_height - activity_height - gap - scale_value(40), TRUE);

    status_parts[0] = scale_value(170);
    status_parts[1] = scale_value(350);
    status_parts[2] = scale_value(500);
    status_parts[3] = scale_value(635);
    status_parts[4] = -1;
    SendMessageW(g_gui.status_bar, SB_SETPARTS, ARRAYSIZE(status_parts), (LPARAM)status_parts);
    resize_list_columns();
}

static void format_event_time(const SYSTEMTIME *timestamp, WCHAR *buffer, size_t buffer_count) {
    StringCchPrintfW(
        buffer, buffer_count, L"%02u:%02u:%02u",
        (unsigned int)timestamp->wHour,
        (unsigned int)timestamp->wMinute,
        (unsigned int)timestamp->wSecond
    );
}

static BOOL event_matches_search(const TraceGlassEvent *event) {
    WCHAR filter[256];
    WCHAR numeric_fields[96];
    GetWindowTextW(g_gui.search_edit, filter, ARRAYSIZE(filter));
    if (filter[0] == L'\0') {
        return TRUE;
    }
    StringCchPrintfW(
        numeric_fields, ARRAYSIZE(numeric_fields), L"%lu %lu %u %u",
        event->process.pid, event->parent.pid,
        (unsigned int)event->local_port, (unsigned int)event->remote_port
    );
    return string_contains_insensitive(event->process_name, filter) ||
        string_contains_insensitive(event->parent_name, filter) ||
        string_contains_insensitive(event->executable_path, filter) ||
        string_contains_insensitive(event->details, filter) ||
        string_contains_insensitive(event->remote_address, filter) ||
        string_contains_insensitive(event->local_address, filter) ||
        string_contains_insensitive(event->protocol, filter) ||
        string_contains_insensitive(event->network_state, filter) ||
        string_contains_insensitive(event->rule_name, filter) ||
        string_contains_insensitive(event_type_name(event->type), filter) ||
        string_contains_insensitive(event_category_name(event->type), filter) ||
        string_contains_insensitive(numeric_fields, filter);
}

static BOOL event_matches_type_filter(const TraceGlassEvent *event) {
    LRESULT selected = SendMessageW(g_gui.event_filter, CB_GETCURSEL, 0, 0);
    if (selected <= 0 || selected == CB_ERR) return TRUE;
    if (selected == 1) {
        return event->type == EVENT_PROCESS_START || event->type == EVENT_PROCESS_STOP;
    }
    if (selected == 2) return event->type == EVENT_NETWORK;
    if (selected == 3) return event->type == EVENT_ALERT;
    return TRUE;
}

static int insert_list_item(HWND list, const WCHAR *first_text, size_t event_index) {
    LVITEMW item;
    ZeroMemory(&item, sizeof(item));
    item.mask = LVIF_TEXT | LVIF_PARAM;
    item.iItem = ListView_GetItemCount(list);
    item.pszText = (LPWSTR)first_text;
    item.lParam = (LPARAM)event_index;
    return ListView_InsertItem(list, &item);
}

static void add_timeline_row(const TraceGlassEvent *event, size_t event_index) {
    WCHAR time_text[32];
    WCHAR pid_text[32];
    int row;
    if (!event_matches_search(event) || !event_matches_type_filter(event)) return;
    format_event_time(&event->timestamp, time_text, ARRAYSIZE(time_text));
    StringCchPrintfW(pid_text, ARRAYSIZE(pid_text), L"%lu", event->process.pid);
    row = insert_list_item(g_gui.timeline_list, time_text, event_index);
    if (row < 0) return;
    ListView_SetItemText(g_gui.timeline_list, row, 1, (LPWSTR)event_category_name(event->type));
    ListView_SetItemText(g_gui.timeline_list, row, 2, (LPWSTR)event->process_name);
    ListView_SetItemText(g_gui.timeline_list, row, 3, pid_text);
    ListView_SetItemText(g_gui.timeline_list, row, 4, (LPWSTR)event->details);
}

static void add_network_row(const TraceGlassEvent *event, size_t event_index) {
    WCHAR time_text[32];
    WCHAR pid_text[32];
    WCHAR local_text[96];
    WCHAR remote_text[96];
    int row;
    if (!event_matches_search(event)) return;
    format_event_time(&event->timestamp, time_text, ARRAYSIZE(time_text));
    StringCchPrintfW(pid_text, ARRAYSIZE(pid_text), L"%lu", event->process.pid);
    StringCchPrintfW(local_text, ARRAYSIZE(local_text), L"%s:%u",
        event->local_address, (unsigned int)event->local_port);
    StringCchPrintfW(remote_text, ARRAYSIZE(remote_text), L"%s:%u",
        event->remote_address, (unsigned int)event->remote_port);
    row = insert_list_item(g_gui.network_list, time_text, event_index);
    if (row < 0) return;
    ListView_SetItemText(g_gui.network_list, row, 1, (LPWSTR)event->process_name);
    ListView_SetItemText(g_gui.network_list, row, 2, pid_text);
    ListView_SetItemText(g_gui.network_list, row, 3, (LPWSTR)event->protocol);
    ListView_SetItemText(g_gui.network_list, row, 4, local_text);
    ListView_SetItemText(g_gui.network_list, row, 5, remote_text);
    ListView_SetItemText(g_gui.network_list, row, 6, (LPWSTR)event->network_state);
}

static void add_alert_row(const TraceGlassEvent *event, size_t event_index) {
    WCHAR time_text[32];
    int row;
    if (!event_matches_search(event)) return;
    format_event_time(&event->timestamp, time_text, ARRAYSIZE(time_text));
    row = insert_list_item(g_gui.alert_list, time_text, event_index);
    if (row < 0) return;
    ListView_SetItemText(g_gui.alert_list, row, 1, (LPWSTR)alert_severity_name(event->severity));
    ListView_SetItemText(g_gui.alert_list, row, 2, (LPWSTR)event->rule_name);
    ListView_SetItemText(g_gui.alert_list, row, 3, (LPWSTR)event->process_name);
    ListView_SetItemText(g_gui.alert_list, row, 4, (LPWSTR)event->details);
}

static int compare_text(const WCHAR *left, const WCHAR *right) {
    int result = CompareStringOrdinal(left != NULL ? left : L"", -1,
        right != NULL ? right : L"", -1, TRUE);
    if (result == CSTR_LESS_THAN) return -1;
    if (result == CSTR_GREATER_THAN) return 1;
    return 0;
}

static int compare_numbers(ULONGLONG left, ULONGLONG right) {
    if (left < right) return -1;
    return left > right ? 1 : 0;
}

static int compare_event_times(const TraceGlassEvent *left, const TraceGlassEvent *right) {
    FILETIME left_time;
    FILETIME right_time;
    int result;
    if (SystemTimeToFileTime(&left->timestamp, &left_time) &&
        SystemTimeToFileTime(&right->timestamp, &right_time)) {
        result = compare_numbers(filetime_to_u64(left_time), filetime_to_u64(right_time));
        if (result != 0) return result;
    }
    return compare_numbers(left->sequence, right->sequence);
}

static int compare_endpoints(const TraceGlassEvent *left, const TraceGlassEvent *right, BOOL remote) {
    int result = compare_text(
        remote ? left->remote_address : left->local_address,
        remote ? right->remote_address : right->local_address
    );
    if (result != 0) return result;
    return compare_numbers(
        remote ? left->remote_port : left->local_port,
        remote ? right->remote_port : right->local_port
    );
}

static int CALLBACK compare_event_items(LPARAM left_value, LPARAM right_value, LPARAM parameter) {
    const SortState *sort = (const SortState *)parameter;
    size_t left_index = (size_t)left_value;
    size_t right_index = (size_t)right_value;
    const TraceGlassEvent *left;
    const TraceGlassEvent *right;
    int result = 0;
    if (sort == NULL || left_index >= g_gui.store.event_count ||
        right_index >= g_gui.store.event_count) {
        return 0;
    }
    left = &g_gui.store.events[left_index];
    right = &g_gui.store.events[right_index];

    if (sort->view == VIEW_TIMELINE) {
        switch (sort->column) {
            case 0: result = compare_event_times(left, right); break;
            case 1: result = compare_text(event_category_name(left->type), event_category_name(right->type)); break;
            case 2: result = compare_text(left->process_name, right->process_name); break;
            case 3: result = compare_numbers(left->process.pid, right->process.pid); break;
            case 4: result = compare_text(left->details, right->details); break;
            default: break;
        }
    } else if (sort->view == VIEW_NETWORK) {
        switch (sort->column) {
            case 0: result = compare_event_times(left, right); break;
            case 1: result = compare_text(left->process_name, right->process_name); break;
            case 2: result = compare_numbers(left->process.pid, right->process.pid); break;
            case 3: result = compare_text(left->protocol, right->protocol); break;
            case 4: result = compare_endpoints(left, right, FALSE); break;
            case 5: result = compare_endpoints(left, right, TRUE); break;
            case 6: result = compare_text(left->network_state, right->network_state); break;
            default: break;
        }
    } else if (sort->view == VIEW_ALERTS) {
        switch (sort->column) {
            case 0: result = compare_event_times(left, right); break;
            case 1: result = compare_numbers(left->severity, right->severity); break;
            case 2: result = compare_text(left->rule_name, right->rule_name); break;
            case 3: result = compare_text(left->process_name, right->process_name); break;
            case 4: result = compare_text(left->details, right->details); break;
            default: break;
        }
    }
    if (result == 0) result = compare_numbers(left->sequence, right->sequence);
    return sort->ascending ? result : -result;
}

static HWND list_for_view(GuiView view) {
    if (view == VIEW_NETWORK) return g_gui.network_list;
    if (view == VIEW_TIMELINE) return g_gui.timeline_list;
    if (view == VIEW_ALERTS) return g_gui.alert_list;
    return NULL;
}

static void update_sort_indicator(GuiView view) {
    HWND list = list_for_view(view);
    HWND header;
    int count;
    int index;
    SortState *sort;
    if (list == NULL) return;
    header = ListView_GetHeader(list);
    count = Header_GetItemCount(header);
    sort = &g_gui.sort_states[view];
    for (index = 0; index < count; ++index) {
        HDITEMW item;
        ZeroMemory(&item, sizeof(item));
        item.mask = HDI_FORMAT;
        if (Header_GetItem(header, index, &item)) {
            item.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
            if (sort->active && index == sort->column) {
                item.fmt |= sort->ascending ? HDF_SORTUP : HDF_SORTDOWN;
            }
            Header_SetItem(header, index, &item);
        }
    }
}

static void sort_view(GuiView view, BOOL force_default) {
    HWND list = list_for_view(view);
    SortState default_sort;
    SortState *sort;
    if (list == NULL) return;
    sort = &g_gui.sort_states[view];
    if (!sort->active && !force_default) return;
    if (sort->active) {
        ListView_SortItems(list, compare_event_items, (LPARAM)sort);
    } else {
        default_sort.column = 0;
        default_sort.ascending = TRUE;
        default_sort.active = FALSE;
        default_sort.view = view;
        ListView_SortItems(list, compare_event_items, (LPARAM)&default_sort);
    }
}

static void handle_column_click(HWND list, int column) {
    GuiView view;
    SortState *sort;
    if (list == g_gui.network_list) view = VIEW_NETWORK;
    else if (list == g_gui.timeline_list) view = VIEW_TIMELINE;
    else if (list == g_gui.alert_list) view = VIEW_ALERTS;
    else return;
    sort = &g_gui.sort_states[view];
    if (sort->active && sort->column == column) {
        sort->ascending = !sort->ascending;
    } else {
        sort->column = column;
        sort->ascending = TRUE;
        sort->active = TRUE;
    }
    update_sort_indicator(view);
    sort_view(view, FALSE);
}

static void rebuild_event_lists(void) {
    size_t index;
    SendMessageW(g_gui.timeline_list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g_gui.network_list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g_gui.alert_list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g_gui.timeline_list);
    ListView_DeleteAllItems(g_gui.network_list);
    ListView_DeleteAllItems(g_gui.alert_list);
    for (index = 0; index < g_gui.store.event_count; ++index) {
        const TraceGlassEvent *event = &g_gui.store.events[index];
        if (index >= g_gui.view_floor[VIEW_TIMELINE]) add_timeline_row(event, index);
        if (event->type == EVENT_NETWORK && index >= g_gui.view_floor[VIEW_NETWORK]) {
            add_network_row(event, index);
        } else if (event->type == EVENT_ALERT && index >= g_gui.view_floor[VIEW_ALERTS]) {
            add_alert_row(event, index);
        }
    }
    sort_view(VIEW_TIMELINE, TRUE);
    sort_view(VIEW_NETWORK, TRUE);
    sort_view(VIEW_ALERTS, TRUE);
    SendMessageW(g_gui.timeline_list, WM_SETREDRAW, TRUE, 0);
    SendMessageW(g_gui.network_list, WM_SETREDRAW, TRUE, 0);
    SendMessageW(g_gui.alert_list, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(g_gui.timeline_list, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
    RedrawWindow(g_gui.network_list, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
    RedrawWindow(g_gui.alert_list, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

static void format_activity(const TraceGlassEvent *event, WCHAR *buffer, size_t buffer_count) {
    switch (event->type) {
        case EVENT_PROCESS_START:
            StringCchPrintfW(buffer, buffer_count, L"%s started (PID %lu)",
                event->process_name, event->process.pid);
            break;
        case EVENT_PROCESS_STOP:
            StringCchPrintfW(buffer, buffer_count, L"%s stopped (PID %lu)",
                event->process_name, event->process.pid);
            break;
        case EVENT_NETWORK:
            StringCchPrintfW(buffer, buffer_count,
                L"%s owns TCP connection: local %s:%u; remote %s:%u",
                event->process_name, event->local_address,
                (unsigned int)event->local_port, event->remote_address,
                (unsigned int)event->remote_port);
            break;
        default:
            StringCchPrintfW(buffer, buffer_count, L"%s: %s",
                event_category_name(event->type), event->details);
            break;
    }
}

static void rebuild_overview_lists(void) {
    size_t index;
    size_t activity_count = 0;
    size_t alert_count = 0;
    WCHAR time_text[32];
    WCHAR activity[1400];
    SendMessageW(g_gui.recent_list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g_gui.recent_alert_list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g_gui.recent_list);
    ListView_DeleteAllItems(g_gui.recent_alert_list);
    for (index = g_gui.store.event_count; index > g_gui.view_floor[VIEW_OVERVIEW]; --index) {
        size_t event_index = index - 1;
        const TraceGlassEvent *event = &g_gui.store.events[event_index];
        int row;
        if (!event_matches_search(event) || !event_matches_type_filter(event)) continue;
        format_event_time(&event->timestamp, time_text, ARRAYSIZE(time_text));
        if (event->type == EVENT_ALERT) {
            if (alert_count >= RECENT_ALERT_LIMIT) continue;
            row = insert_list_item(g_gui.recent_alert_list, time_text, event_index);
            if (row >= 0) {
                ListView_SetItemText(g_gui.recent_alert_list, row, 1,
                    (LPWSTR)alert_severity_name(event->severity));
                ListView_SetItemText(g_gui.recent_alert_list, row, 2,
                    (LPWSTR)event->rule_name);
                ListView_SetItemText(g_gui.recent_alert_list, row, 3,
                    (LPWSTR)event->process_name);
                ++alert_count;
            }
        } else {
            if (activity_count >= RECENT_EVENT_LIMIT) continue;
            format_activity(event, activity, ARRAYSIZE(activity));
            row = insert_list_item(g_gui.recent_list, time_text, event_index);
            if (row >= 0) {
                ListView_SetItemText(g_gui.recent_list, row, 1, activity);
                ++activity_count;
            }
        }
        if (activity_count >= RECENT_EVENT_LIMIT && alert_count >= RECENT_ALERT_LIMIT) break;
    }
    SendMessageW(g_gui.recent_list, WM_SETREDRAW, TRUE, 0);
    SendMessageW(g_gui.recent_alert_list, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(g_gui.recent_list, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
    RedrawWindow(g_gui.recent_alert_list, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

static ptrdiff_t find_parent_index(size_t child_index) {
    const ProcessRecord *child = &g_gui.store.processes[child_index];
    const ProcessRecord *parent;
    if (child->parent.pid == 0 || child->parent.pid == child->key.pid) return -1;
    parent = event_store_find_process_const(&g_gui.store, child->parent);
    if (parent == NULL) return -1;
    return parent - g_gui.store.processes;
}

static HTREEITEM insert_process_node(size_t index, HTREEITEM *items, BYTE *states) {
    TVINSERTSTRUCTW insertion;
    WCHAR label[MAX_PATH + 80];
    ptrdiff_t parent_index;
    HTREEITEM parent_item = TVI_ROOT;
    const ProcessRecord *record;
    if (states[index] == 2) return items[index];
    if (states[index] == 1) return TVI_ROOT;
    states[index] = 1;
    parent_index = find_parent_index(index);
    if (parent_index >= 0 && (size_t)parent_index != index) {
        parent_item = insert_process_node((size_t)parent_index, items, states);
    }
    record = &g_gui.store.processes[index];
    StringCchPrintfW(label, ARRAYSIZE(label),
        record->active ? L"%s (PID %lu)" : L"%s (PID %lu) [stopped]",
        record->name[0] != L'\0' ? record->name : L"<unknown>", record->key.pid);
    ZeroMemory(&insertion, sizeof(insertion));
    insertion.hParent = parent_item;
    insertion.hInsertAfter = TVI_LAST;
    insertion.item.mask = TVIF_TEXT | TVIF_PARAM;
    insertion.item.pszText = label;
    insertion.item.lParam = (LPARAM)index;
    items[index] = TreeView_InsertItem(g_gui.process_tree, &insertion);
    states[index] = 2;
    return items[index];
}

static void rebuild_process_tree(void) {
    HTREEITEM *items;
    BYTE *states;
    size_t index;
    HTREEITEM root;
    SendMessageW(g_gui.process_tree, WM_SETREDRAW, FALSE, 0);
    TreeView_DeleteAllItems(g_gui.process_tree);
    if (g_gui.store.process_count == 0) {
        SendMessageW(g_gui.process_tree, WM_SETREDRAW, TRUE, 0);
        return;
    }
    items = (HTREEITEM *)calloc(g_gui.store.process_count, sizeof(*items));
    states = (BYTE *)calloc(g_gui.store.process_count, sizeof(*states));
    if (items == NULL || states == NULL) {
        free(items);
        free(states);
        SendMessageW(g_gui.process_tree, WM_SETREDRAW, TRUE, 0);
        logger_write(L"ERROR", L"Process tree allocation failed");
        return;
    }
    for (index = 0; index < g_gui.store.process_count; ++index) {
        insert_process_node(index, items, states);
    }
    root = TreeView_GetRoot(g_gui.process_tree);
    while (root != NULL) {
        TreeView_Expand(g_gui.process_tree, root, TVE_EXPAND);
        root = TreeView_GetNextSibling(g_gui.process_tree, root);
    }
    free(items);
    free(states);
    SendMessageW(g_gui.process_tree, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(g_gui.process_tree, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
}

static void update_monitoring_label(void) {
    SetWindowTextW(
        g_gui.monitoring_label,
        g_gui.rendering_paused
            ? L"\x25CF Collecting \x00B7 View Paused"
            : L"\x25CF Live Monitoring"
    );
    SetWindowTextW(g_gui.pause_button, g_gui.rendering_paused ? L"Resume" : L"Pause");
}

static void update_overview_metrics(void) {
    WCHAR value[128];
    WCHAR reason[256];
    WCHAR note[640];
    ULONGLONG elapsed_seconds = (GetTickCount64() - g_gui.session_started_tick) / 1000ULL;
    ULONGLONG hours = elapsed_seconds / 3600ULL;
    ULONGLONG minutes = (elapsed_seconds % 3600ULL) / 60ULL;
    ULONGLONG seconds = elapsed_seconds % 60ULL;

    SetWindowTextW(g_gui.metric_values[0], telemetry_mode_name(&g_gui.telemetry));
    StringCchPrintfW(value, ARRAYSIZE(value), L"%zu", g_gui.store.event_count);
    SetWindowTextW(g_gui.metric_values[1], value);
    StringCchPrintfW(value, ARRAYSIZE(value), L"%zu", g_gui.store.active_process_count);
    SetWindowTextW(g_gui.metric_values[2], value);
    StringCchPrintfW(value, ARRAYSIZE(value), L"%zu", g_gui.store.network_count);
    SetWindowTextW(g_gui.metric_values[3], value);
    StringCchPrintfW(value, ARRAYSIZE(value), L"%zu", g_gui.store.alert_count);
    SetWindowTextW(g_gui.metric_values[4], value);
    StringCchPrintfW(value, ARRAYSIZE(value), L"%02llu:%02llu:%02llu",
        hours, minutes, seconds);
    SetWindowTextW(g_gui.metric_values[5], value);

    if (telemetry_etw_enabled(&g_gui.telemetry)) {
        copy_wstring(
            note,
            ARRAYSIZE(note),
            L"ETW process telemetry is active. Win32 snapshot reconciliation remains enabled to recover missed events."
        );
    } else {
        DWORD error = telemetry_etw_error(&g_gui.telemetry);
        if (error == ERROR_SUCCESS) {
            copy_wstring(reason, ARRAYSIZE(reason), L"Unavailable");
        } else {
            format_windows_error(error, reason, ARRAYSIZE(reason));
        }
        StringCchPrintfW(
            note,
            ARRAYSIZE(note),
            L"ETW unavailable: %s\r\n\r\nSome telemetry sources may require elevation. Win32 fallback remains active; TraceGlass will not request elevation automatically.",
            reason
        );
    }
    if (!g_gui.telemetry.network_available) {
        StringCchCatW(note, ARRAYSIZE(note),
            L"\r\n\r\nIPv4 TCP telemetry is unavailable in this session.");
    }
    SetWindowTextW(g_gui.telemetry_note, note);
}

static void set_transient_status(const WCHAR *message) {
    copy_wstring(g_gui.transient_status, ARRAYSIZE(g_gui.transient_status), message);
    g_gui.transient_until = GetTickCount64() + 3500ULL;
}

static void update_status_bar(void) {
    WCHAR text[256];
    ULONGLONG now = GetTickCount64();
    if (g_gui.transient_status[0] != L'\0' && now < g_gui.transient_until) {
        SendMessageW(g_gui.status_bar, SB_SETTEXTW, 0, (LPARAM)g_gui.transient_status);
    } else {
        g_gui.transient_status[0] = L'\0';
        StringCchPrintfW(text, ARRAYSIZE(text), L"Events: %zu", g_gui.store.event_count);
        SendMessageW(g_gui.status_bar, SB_SETTEXTW, 0, (LPARAM)text);
    }
    StringCchPrintfW(text, ARRAYSIZE(text), L"Processes: %zu", g_gui.store.active_process_count);
    SendMessageW(g_gui.status_bar, SB_SETTEXTW, 1, (LPARAM)text);
    StringCchPrintfW(text, ARRAYSIZE(text), L"TCP: %zu", g_gui.store.network_count);
    SendMessageW(g_gui.status_bar, SB_SETTEXTW, 2, (LPARAM)text);
    StringCchPrintfW(text, ARRAYSIZE(text), L"Alerts: %zu", g_gui.store.alert_count);
    SendMessageW(g_gui.status_bar, SB_SETTEXTW, 3, (LPARAM)text);
    StringCchPrintfW(
        text,
        ARRAYSIZE(text),
        g_gui.rendering_paused ? L"Telemetry: %s  |  View paused" : L"Telemetry: %s",
        telemetry_mode_name(&g_gui.telemetry)
    );
    SendMessageW(g_gui.status_bar, SB_SETTEXTW, 4, (LPARAM)text);
}

static void set_overview_visible(BOOL visible) {
    size_t index;
    int command = visible ? SW_SHOW : SW_HIDE;
    ShowWindow(g_gui.overview_status_group, command);
    ShowWindow(g_gui.overview_activity_group, command);
    ShowWindow(g_gui.overview_alert_group, command);
    ShowWindow(g_gui.telemetry_note, command);
    ShowWindow(g_gui.recent_list, command);
    ShowWindow(g_gui.recent_alert_list, command);
    for (index = 0; index < ARRAYSIZE(g_gui.metric_names); ++index) {
        ShowWindow(g_gui.metric_names[index], command);
        ShowWindow(g_gui.metric_values[index], command);
    }
}

static GuiView current_view(void) {
    int selected = TabCtrl_GetCurSel(g_gui.navigation);
    if (selected < 0 || selected >= VIEW_COUNT) return VIEW_OVERVIEW;
    return (GuiView)selected;
}

static void update_selected_view(void) {
    GuiView view = current_view();
    set_overview_visible(view == VIEW_OVERVIEW);
    ShowWindow(g_gui.process_tree, view == VIEW_PROCESSES ? SW_SHOW : SW_HIDE);
    ShowWindow(g_gui.network_list, view == VIEW_NETWORK ? SW_SHOW : SW_HIDE);
    ShowWindow(g_gui.timeline_list, view == VIEW_TIMELINE ? SW_SHOW : SW_HIDE);
    ShowWindow(g_gui.alert_list, view == VIEW_ALERTS ? SW_SHOW : SW_HIDE);
    EnableWindow(g_gui.search_edit, view != VIEW_PROCESSES);
    EnableWindow(g_gui.search_label, view != VIEW_PROCESSES);
    EnableWindow(g_gui.event_filter, view == VIEW_OVERVIEW || view == VIEW_TIMELINE);
    EnableWindow(g_gui.event_label, view == VIEW_OVERVIEW || view == VIEW_TIMELINE);
    EnableWindow(g_gui.clear_button, view != VIEW_PROCESSES);
    if (view == VIEW_PROCESSES) {
        SetFocus(g_gui.process_tree);
    }
}

static BOOL add_accepted_event(TraceGlassEvent *event) {
    BOOL accepted = FALSE;
    size_t event_index;
    if (!event_store_add(&g_gui.store, event, &accepted)) {
        logger_write(L"ERROR", L"Event store allocation failed; event dropped");
        return FALSE;
    }
    if (!accepted) return FALSE;
    event_index = g_gui.store.event_count - 1;
    if (!g_gui.rendering_paused) {
        if (event_index >= g_gui.view_floor[VIEW_TIMELINE]) {
            add_timeline_row(&g_gui.store.events[event_index], event_index);
        }
        if (event->type == EVENT_NETWORK && event_index >= g_gui.view_floor[VIEW_NETWORK]) {
            add_network_row(&g_gui.store.events[event_index], event_index);
        } else if (event->type == EVENT_ALERT && event_index >= g_gui.view_floor[VIEW_ALERTS]) {
            add_alert_row(&g_gui.store.events[event_index], event_index);
        }
    }
    return TRUE;
}

static void drain_events(void) {
    TraceGlassEvent event;
    TraceGlassEvent alerts[TRACEGLASS_MAX_ALERTS_PER_EVENT];
    size_t alert_count;
    size_t alert_index;
    size_t count_before = g_gui.store.event_count;
    BOOL tree_changed = FALSE;
    BOOL any_accepted = FALSE;

    while (pop_event(&g_gui.queue, &event)) {
        if (!add_accepted_event(&event)) continue;
        any_accepted = TRUE;
        if (event.type == EVENT_PROCESS_START || event.type == EVENT_PROCESS_STOP) {
            tree_changed = TRUE;
        }
        alert_count = detection_evaluate(&event, alerts, ARRAYSIZE(alerts));
        for (alert_index = 0; alert_index < alert_count; ++alert_index) {
            if (add_accepted_event(&alerts[alert_index])) any_accepted = TRUE;
        }
    }
    if (!any_accepted) return;
    if (g_gui.rendering_paused) {
        g_gui.render_dirty = TRUE;
        return;
    }
    if (tree_changed) rebuild_process_tree();
    rebuild_overview_lists();
    sort_view(VIEW_TIMELINE, count_before == 0);
    sort_view(VIEW_NETWORK, count_before == 0);
    sort_view(VIEW_ALERTS, count_before == 0);
    update_overview_metrics();
    update_status_bar();
}

static void rebuild_filtered_views(void) {
    if (g_gui.rendering_paused) {
        g_gui.render_dirty = TRUE;
        return;
    }
    rebuild_event_lists();
    rebuild_overview_lists();
}

static void toggle_pause(void) {
    g_gui.rendering_paused = !g_gui.rendering_paused;
    update_monitoring_label();
    if (!g_gui.rendering_paused && g_gui.render_dirty) {
        rebuild_event_lists();
        rebuild_process_tree();
        rebuild_overview_lists();
        g_gui.render_dirty = FALSE;
    }
    update_overview_metrics();
    update_status_bar();
    set_transient_status(
        g_gui.rendering_paused
            ? L"View paused; telemetry collection continues"
            : L"View resumed; buffered events rendered"
    );
    update_status_bar();
}

static void clear_current_view(void) {
    GuiView view = current_view();
    if (view == VIEW_PROCESSES) return;
    g_gui.view_floor[view] = g_gui.store.event_count;
    if (view == VIEW_OVERVIEW) {
        ListView_DeleteAllItems(g_gui.recent_list);
        ListView_DeleteAllItems(g_gui.recent_alert_list);
        set_transient_status(L"Overview activity cleared; collection continues");
    } else if (view == VIEW_NETWORK) {
        ListView_DeleteAllItems(g_gui.network_list);
        set_transient_status(L"Network view cleared; collection continues");
    } else if (view == VIEW_TIMELINE) {
        ListView_DeleteAllItems(g_gui.timeline_list);
        set_transient_status(L"Timeline view cleared; collection continues");
    } else if (view == VIEW_ALERTS) {
        ListView_DeleteAllItems(g_gui.alert_list);
        set_transient_status(L"Alerts view cleared; collection continues");
    }
    update_status_bar();
}

static BOOL copy_text_to_clipboard(HWND owner, const WCHAR *text) {
    HGLOBAL memory;
    WCHAR *destination;
    size_t byte_count;
    if (text == NULL) return FALSE;
    byte_count = (wcslen(text) + 1) * sizeof(WCHAR);
    memory = GlobalAlloc(GMEM_MOVEABLE, byte_count);
    if (memory == NULL) return FALSE;
    destination = (WCHAR *)GlobalLock(memory);
    if (destination == NULL) {
        GlobalFree(memory);
        return FALSE;
    }
    CopyMemory(destination, text, byte_count);
    GlobalUnlock(memory);
    if (!OpenClipboard(owner)) {
        GlobalFree(memory);
        return FALSE;
    }
    EmptyClipboard();
    if (SetClipboardData(CF_UNICODETEXT, memory) == NULL) {
        CloseClipboard();
        GlobalFree(memory);
        return FALSE;
    }
    CloseClipboard();
    return TRUE;
}

static void append_format(WCHAR *buffer, size_t buffer_count, const WCHAR *format, ...) {
    WCHAR fragment[1600];
    va_list arguments;
    va_start(arguments, format);
    if (SUCCEEDED(StringCchVPrintfW(fragment, ARRAYSIZE(fragment), format, arguments))) {
        StringCchCatW(buffer, buffer_count, fragment);
    }
    va_end(arguments);
}

static void format_event_details(
    const TraceGlassEvent *event,
    WCHAR *buffer,
    size_t buffer_count
) {
    buffer[0] = L'\0';
    append_format(buffer, buffer_count,
        L"Type: %s\r\nTime: %04u-%02u-%02u %02u:%02u:%02u.%03u\r\n\r\n",
        event_type_name(event->type),
        (unsigned int)event->timestamp.wYear, (unsigned int)event->timestamp.wMonth,
        (unsigned int)event->timestamp.wDay, (unsigned int)event->timestamp.wHour,
        (unsigned int)event->timestamp.wMinute, (unsigned int)event->timestamp.wSecond,
        (unsigned int)event->timestamp.wMilliseconds);
    append_format(buffer, buffer_count,
        L"Process\r\nName: %s\r\nPID: %lu\r\nPath: %s\r\n",
        event->process_name[0] != L'\0' ? event->process_name : L"<unknown>",
        event->process.pid,
        event->executable_path[0] != L'\0' ? event->executable_path : L"<unavailable>");
    if (event->parent.pid != 0 || event->parent_name[0] != L'\0') {
        append_format(buffer, buffer_count,
            L"\r\nParent\r\nName: %s\r\nPID: %lu\r\n",
            event->parent_name[0] != L'\0' ? event->parent_name : L"<unknown>",
            event->parent.pid);
    }
    if (event->protocol[0] != L'\0') {
        append_format(buffer, buffer_count,
            L"\r\nNetwork\r\nProtocol: %s\r\nLocal: %s:%u\r\nRemote: %s:%u\r\nState: %s\r\n",
            event->protocol, event->local_address, (unsigned int)event->local_port,
            event->remote_address, (unsigned int)event->remote_port,
            event->network_state[0] != L'\0' ? event->network_state : L"<unknown>");
    }
    if (event->type == EVENT_ALERT) {
        append_format(buffer, buffer_count,
            L"\r\nAlert\r\nSeverity: %s\r\nRule: %s\r\n",
            alert_severity_name(event->severity),
            event->rule_name[0] != L'\0' ? event->rule_name : L"<unspecified>");
    }
    append_format(buffer, buffer_count, L"\r\nDetails\r\n%s\r\n", event->details);
}

static LRESULT CALLBACK details_window_proc(
    HWND window,
    UINT message,
    WPARAM wparam,
    LPARAM lparam
) {
    DetailsState *state = (DetailsState *)GetWindowLongPtrW(window, GWLP_USERDATA);
    switch (message) {
        case WM_CREATE: {
            CREATESTRUCTW *creation = (CREATESTRUCTW *)lparam;
            WCHAR details[4096];
            UINT dpi = GetDpiForWindow(window);
            state = (DetailsState *)creation->lpCreateParams;
            SetWindowLongPtrW(window, GWLP_USERDATA, (LONG_PTR)state);
            state->font = CreateFontW(
                -MulDiv(10, (int)dpi, 72), 0, 0, 0, FW_NORMAL,
                FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
            );
            state->edit = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE |
                    ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
                0, 0, 0, 0, window, (HMENU)(INT_PTR)IDC_DETAILS_EDIT,
                g_gui.instance, NULL
            );
            state->copy_button = CreateWindowExW(
                0, L"BUTTON", L"Copy",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                0, 0, 0, 0, window, (HMENU)(INT_PTR)IDC_DETAILS_COPY,
                g_gui.instance, NULL
            );
            state->close_button = CreateWindowExW(
                0, L"BUTTON", L"Close",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                0, 0, 0, 0, window, (HMENU)(INT_PTR)IDC_DETAILS_CLOSE,
                g_gui.instance, NULL
            );
            set_control_font(state->edit, state->font);
            set_control_font(state->copy_button, state->font);
            set_control_font(state->close_button, state->font);
            format_event_details(&state->event, details, ARRAYSIZE(details));
            SetWindowTextW(state->edit, details);
            return 0;
        }

        case WM_SIZE:
            if (state != NULL) {
                RECT client;
                int padding = MulDiv(14, (int)GetDpiForWindow(window), 96);
                int button_width = MulDiv(84, (int)GetDpiForWindow(window), 96);
                int button_height = MulDiv(32, (int)GetDpiForWindow(window), 96);
                GetClientRect(window, &client);
                MoveWindow(state->edit, padding, padding,
                    client.right - 2 * padding,
                    client.bottom - 3 * padding - button_height, TRUE);
                MoveWindow(state->close_button,
                    client.right - padding - button_width,
                    client.bottom - padding - button_height,
                    button_width, button_height, TRUE);
                MoveWindow(state->copy_button,
                    client.right - 2 * padding - 2 * button_width,
                    client.bottom - padding - button_height,
                    button_width, button_height, TRUE);
            }
            return 0;

        case WM_COMMAND:
            if (state != NULL && LOWORD(wparam) == IDC_DETAILS_COPY) {
                WCHAR details[4096];
                format_event_details(&state->event, details, ARRAYSIZE(details));
                copy_text_to_clipboard(window, details);
                return 0;
            }
            if (LOWORD(wparam) == IDC_DETAILS_CLOSE) {
                DestroyWindow(window);
                return 0;
            }
            break;

        case WM_GETMINMAXINFO: {
            MINMAXINFO *limits = (MINMAXINFO *)lparam;
            UINT dpi = GetDpiForWindow(window);
            limits->ptMinTrackSize.x = MulDiv(480, (int)dpi, 96);
            limits->ptMinTrackSize.y = MulDiv(390, (int)dpi, 96);
            return 0;
        }

        case WM_CLOSE:
            DestroyWindow(window);
            return 0;

        case WM_NCDESTROY:
            if (state != NULL) {
                if (state->font != NULL) DeleteObject(state->font);
                free(state);
                SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            }
            return DefWindowProcW(window, message, wparam, lparam);

        default:
            break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static void open_event_details(const TraceGlassEvent *event) {
    DetailsState *state;
    HWND window;
    RECT owner;
    int width = scale_value(610);
    int height = scale_value(560);
    if (event == NULL) return;
    state = (DetailsState *)calloc(1, sizeof(*state));
    if (state == NULL) return;
    state->event = *event;
    GetWindowRect(g_gui.window, &owner);
    window = CreateWindowExW(
        WS_EX_DLGMODALFRAME, TRACEGLASS_DETAILS_CLASS, L"Event Details",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
        owner.left + ((owner.right - owner.left) - width) / 2,
        owner.top + ((owner.bottom - owner.top) - height) / 2,
        width, height, g_gui.window, NULL, g_gui.instance, state
    );
    if (window == NULL) {
        free(state);
        return;
    }
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
}

static const TraceGlassEvent *selected_event_from_list(HWND list, size_t *event_index) {
    int selected;
    LVITEMW item;
    size_t index;
    if (list == NULL) return NULL;
    selected = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    if (selected < 0) return NULL;
    ZeroMemory(&item, sizeof(item));
    item.mask = LVIF_PARAM;
    item.iItem = selected;
    if (!ListView_GetItem(list, &item)) return NULL;
    index = (size_t)item.lParam;
    if (index >= g_gui.store.event_count) return NULL;
    if (event_index != NULL) *event_index = index;
    return &g_gui.store.events[index];
}

static BOOL is_event_list(HWND control) {
    return control == g_gui.timeline_list || control == g_gui.network_list ||
        control == g_gui.alert_list || control == g_gui.recent_list ||
        control == g_gui.recent_alert_list;
}

static void format_event_row(HWND list, const TraceGlassEvent *event, WCHAR *buffer, size_t count) {
    WCHAR time_text[32];
    format_event_time(&event->timestamp, time_text, ARRAYSIZE(time_text));
    if (list == g_gui.network_list) {
        StringCchPrintfW(buffer, count,
            L"%s\t%s\t%lu\t%s\t%s:%u\t%s:%u\t%s",
            time_text, event->process_name, event->process.pid, event->protocol,
            event->local_address, (unsigned int)event->local_port,
            event->remote_address, (unsigned int)event->remote_port,
            event->network_state);
    } else if (list == g_gui.alert_list || list == g_gui.recent_alert_list) {
        StringCchPrintfW(buffer, count, L"%s\t%s\t%s\t%s\t%s",
            time_text, alert_severity_name(event->severity), event->rule_name,
            event->process_name, event->details);
    } else if (list == g_gui.recent_list) {
        WCHAR activity[1400];
        format_activity(event, activity, ARRAYSIZE(activity));
        StringCchPrintfW(buffer, count, L"%s\t%s", time_text, activity);
    } else {
        StringCchPrintfW(buffer, count, L"%s\t%s\t%s\t%lu\t%s",
            time_text, event_category_name(event->type), event->process_name,
            event->process.pid, event->details);
    }
}

static void show_context_menu(HWND list, POINT screen_point) {
    const TraceGlassEvent *event = selected_event_from_list(list, NULL);
    HMENU menu;
    UINT command;
    WCHAR text[2048];
    if (event == NULL) return;
    if (screen_point.x == -1 && screen_point.y == -1) {
        int selected = ListView_GetNextItem(list, -1, LVNI_SELECTED);
        RECT item_rectangle;
        if (selected >= 0 && ListView_GetItemRect(list, selected, &item_rectangle, LVIR_BOUNDS)) {
            screen_point.x = item_rectangle.left;
            screen_point.y = item_rectangle.bottom;
            ClientToScreen(list, &screen_point);
        } else {
            GetCursorPos(&screen_point);
        }
    }
    menu = CreatePopupMenu();
    if (menu == NULL) return;
    AppendMenuW(menu, MF_STRING, IDM_COPY_ROW, L"Copy row");
    if (event->process_name[0] != L'\0') {
        AppendMenuW(menu, MF_STRING, IDM_COPY_PROCESS, L"Copy process name");
    }
    if (event->process.pid != 0) AppendMenuW(menu, MF_STRING, IDM_COPY_PID, L"Copy PID");
    if (event->remote_address[0] != L'\0') {
        AppendMenuW(menu, MF_STRING, IDM_COPY_REMOTE, L"Copy remote address");
    }
    SetForegroundWindow(g_gui.window);
    command = TrackPopupMenuEx(
        menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
        screen_point.x, screen_point.y, g_gui.window, NULL
    );
    DestroyMenu(menu);
    if (command == IDM_COPY_ROW) {
        format_event_row(list, event, text, ARRAYSIZE(text));
    } else if (command == IDM_COPY_PROCESS) {
        copy_wstring(text, ARRAYSIZE(text), event->process_name);
    } else if (command == IDM_COPY_PID) {
        StringCchPrintfW(text, ARRAYSIZE(text), L"%lu", event->process.pid);
    } else if (command == IDM_COPY_REMOTE) {
        StringCchPrintfW(text, ARRAYSIZE(text), L"%s:%u", event->remote_address,
            (unsigned int)event->remote_port);
    } else {
        return;
    }
    if (copy_text_to_clipboard(g_gui.window, text)) {
        set_transient_status(L"Copied to clipboard");
        update_status_bar();
    }
}

static void show_about_dialog(void) {
    WCHAR text[512];
    StringCchPrintfW(
        text,
        ARRAYSIZE(text),
        L"TraceGlass\r\nVersion %s\r\n\r\nNative Windows behavioral telemetry viewer.\r\n\r\nBuilt with C, Win32 APIs and ETW.",
        TRACEGLASS_VERSION_STRING
    );
    MessageBoxW(g_gui.window, text, L"About TraceGlass", MB_OK | MB_ICONINFORMATION);
}

static void export_session_events(void) {
    WCHAR path[MAX_PATH];
    WCHAR error[768];
    ExportResult result = export_events_with_dialog(
        g_gui.window, &g_gui.store, 0,
        path, ARRAYSIZE(path), error, ARRAYSIZE(error)
    );
    if (result == EXPORT_RESULT_SUCCEEDED) {
        logger_write(L"INFO", L"Exported %zu events to %s", g_gui.store.event_count, path);
        set_transient_status(L"Session events exported successfully");
        update_status_bar();
    } else if (result == EXPORT_RESULT_FAILED) {
        logger_write(L"ERROR", L"Event export failed: %s", error);
        MessageBoxW(g_gui.window, error, L"TraceGlass Export", MB_OK | MB_ICONERROR);
    }
}

static void show_initialization_error(HWND window, const WCHAR *operation, DWORD error) {
    WCHAR reason[256];
    WCHAR message[768];
    format_windows_error(error, reason, ARRAYSIZE(reason));
    StringCchPrintfW(message, ARRAYSIZE(message),
        L"%s\r\n\r\nReason:\r\n%s", operation, reason);
    MessageBoxW(window, message, TRACEGLASS_APP_NAME, MB_OK | MB_ICONERROR);
}

static void shutdown_gui_state(void) {
    if (g_gui.shutting_down) return;
    g_gui.shutting_down = TRUE;
    if (g_gui.window != NULL) {
        KillTimer(g_gui.window, TIMER_STATUS);
        KillTimer(g_gui.window, TIMER_FILTER);
    }
    if (g_gui.telemetry_ready) {
        shutdown_telemetry(&g_gui.telemetry);
        g_gui.telemetry_ready = FALSE;
    }
    if (g_gui.queue_ready) {
        event_queue_set_window(&g_gui.queue, NULL);
        event_queue_destroy(&g_gui.queue);
        g_gui.queue_ready = FALSE;
    }
    if (g_gui.store_ready) {
        event_store_destroy(&g_gui.store);
        g_gui.store_ready = FALSE;
    }
}

static LRESULT CALLBACK main_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_CREATE: {
            DWORD error;
            WCHAR title[128];
            size_t index;
            g_gui.window = window;
            g_gui.dpi = GetDpiForWindow(window);
            g_gui.session_started_tick = GetTickCount64();
            for (index = 0; index < VIEW_COUNT; ++index) {
                g_gui.sort_states[index].column = 0;
                g_gui.sort_states[index].ascending = TRUE;
                g_gui.sort_states[index].view = (GuiView)index;
            }
            if (!recreate_fonts() || !create_controls(window)) {
                MessageBoxW(
                    window,
                    L"TraceGlass could not initialize its interface.",
                    TRACEGLASS_APP_NAME,
                    MB_OK | MB_ICONERROR
                );
                return -1;
            }
            StringCchPrintfW(title, ARRAYSIZE(title), L"TraceGlass v%s", TRACEGLASS_VERSION_STRING);
            SetWindowTextW(g_gui.title_label, title);
            if (!event_store_initialize(&g_gui.store)) {
                MessageBoxW(window, L"The event store could not be initialized.",
                    TRACEGLASS_APP_NAME, MB_OK | MB_ICONERROR);
                return -1;
            }
            g_gui.store_ready = TRUE;
            if (!event_queue_initialize(&g_gui.queue, window, TRACEGLASS_WM_EVENTS_READY)) {
                MessageBoxW(window, L"The telemetry event queue could not be initialized.",
                    TRACEGLASS_APP_NAME, MB_OK | MB_ICONERROR);
                return -1;
            }
            g_gui.queue_ready = TRUE;
            if (!initialize_telemetry(&g_gui.telemetry, &g_gui.queue)) {
                error = GetLastError();
                show_initialization_error(window,
                    L"TraceGlass could not initialize telemetry.", error);
                return -1;
            }
            g_gui.telemetry_ready = TRUE;
            if (!start_telemetry(&g_gui.telemetry)) {
                error = GetLastError();
                show_initialization_error(window,
                    L"TraceGlass could not start its telemetry worker.", error);
                return -1;
            }
            if (!telemetry_etw_enabled(&g_gui.telemetry)) {
                logger_write(L"INFO", L"GUI reports telemetry mode: Win32 Fallback");
            }
            SetTimer(window, TIMER_STATUS, STATUS_INTERVAL_MS, NULL);
            update_monitoring_label();
            update_selected_view();
            layout_controls(window);
            update_overview_metrics();
            update_status_bar();
            return 0;
        }

        case WM_GETMINMAXINFO: {
            MINMAXINFO *limits = (MINMAXINFO *)lparam;
            UINT dpi = g_gui.dpi == 0 ? 96 : g_gui.dpi;
            limits->ptMinTrackSize.x = MulDiv(960, (int)dpi, 96);
            limits->ptMinTrackSize.y = MulDiv(640, (int)dpi, 96);
            return 0;
        }

        case WM_SIZE:
            if (g_gui.status_bar != NULL && wparam != SIZE_MINIMIZED) {
                layout_controls(window);
            }
            return 0;

        case WM_DPICHANGED: {
            RECT *suggested = (RECT *)lparam;
            g_gui.dpi = HIWORD(wparam);
            SetWindowPos(window, NULL, suggested->left, suggested->top,
                suggested->right - suggested->left,
                suggested->bottom - suggested->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
            recreate_fonts();
            SendMessageW(g_gui.status_bar, SB_SETMINHEIGHT, (WPARAM)scale_value(24), 0);
            layout_controls(window);
            return 0;
        }

        case WM_TIMER:
            if (wparam == TIMER_FILTER) {
                KillTimer(window, TIMER_FILTER);
                g_gui.filter_timer_active = FALSE;
                rebuild_filtered_views();
                return 0;
            }
            if (wparam == TIMER_STATUS && !g_gui.rendering_paused) {
                update_overview_metrics();
                update_status_bar();
                return 0;
            }
            break;

        case WM_COMMAND:
            if (LOWORD(wparam) == IDC_SEARCH_EDIT && HIWORD(wparam) == EN_CHANGE) {
                if (g_gui.filter_timer_active) KillTimer(window, TIMER_FILTER);
                SetTimer(window, TIMER_FILTER, FILTER_DELAY_MS, NULL);
                g_gui.filter_timer_active = TRUE;
                return 0;
            }
            if (LOWORD(wparam) == IDC_EVENT_FILTER && HIWORD(wparam) == CBN_SELCHANGE) {
                rebuild_filtered_views();
                return 0;
            }
            if (LOWORD(wparam) == IDC_PAUSE_BUTTON && HIWORD(wparam) == BN_CLICKED) {
                toggle_pause();
                return 0;
            }
            if (LOWORD(wparam) == IDC_CLEAR_BUTTON && HIWORD(wparam) == BN_CLICKED) {
                clear_current_view();
                return 0;
            }
            if (LOWORD(wparam) == IDC_EXPORT_BUTTON && HIWORD(wparam) == BN_CLICKED) {
                export_session_events();
                return 0;
            }
            if (LOWORD(wparam) == IDC_ABOUT_BUTTON && HIWORD(wparam) == BN_CLICKED) {
                show_about_dialog();
                return 0;
            }
            break;

        case WM_NOTIFY: {
            LPNMHDR header = (LPNMHDR)lparam;
            if (header == NULL) break;
            if (header->hwndFrom == g_gui.navigation && header->code == TCN_SELCHANGE) {
                update_selected_view();
                return 0;
            }
            if (header->code == LVN_COLUMNCLICK && is_event_list(header->hwndFrom)) {
                NMLISTVIEW *notification = (NMLISTVIEW *)lparam;
                handle_column_click(header->hwndFrom, notification->iSubItem);
                return 0;
            }
            if (header->code == NM_DBLCLK && is_event_list(header->hwndFrom)) {
                const TraceGlassEvent *event = selected_event_from_list(header->hwndFrom, NULL);
                if (event != NULL) open_event_details(event);
                return 0;
            }
            break;
        }

        case WM_CONTEXTMENU: {
            HWND control = (HWND)wparam;
            if (is_event_list(control)) {
                POINT point;
                point.x = GET_X_LPARAM(lparam);
                point.y = GET_Y_LPARAM(lparam);
                show_context_menu(control, point);
                return 0;
            }
            break;
        }

        case TRACEGLASS_WM_EVENTS_READY:
            if (g_gui.queue_ready) drain_events();
            return 0;

        case WM_CTLCOLORSTATIC: {
            HDC device_context = (HDC)wparam;
            HWND control = (HWND)lparam;
            SetBkMode(device_context, TRANSPARENT);
            if (control == g_gui.monitoring_label) {
                SetTextColor(device_context, RGB(25, 130, 76));
            } else if (control == g_gui.telemetry_note &&
                !telemetry_etw_enabled(&g_gui.telemetry)) {
                SetTextColor(device_context, RGB(150, 92, 0));
            }
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }

        case WM_CLOSE:
            SetWindowTextW(g_gui.monitoring_label, L"Stopping telemetry...");
            EnableWindow(window, FALSE);
            UpdateWindow(window);
            DestroyWindow(window);
            return 0;

        case WM_DESTROY:
            shutdown_gui_state();
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

int gui_run(HINSTANCE instance, int show_command) {
    WNDCLASSEXW main_class;
    WNDCLASSEXW details_class;
    HWND window;
    MSG message;
    BOOL message_result;
    int result = 0;
    WCHAR window_title[192];

    ZeroMemory(&g_gui, sizeof(g_gui));
    g_gui.instance = instance;

    ZeroMemory(&details_class, sizeof(details_class));
    details_class.cbSize = sizeof(details_class);
    details_class.lpfnWndProc = details_window_proc;
    details_class.hInstance = instance;
    details_class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    details_class.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    details_class.lpszClassName = TRACEGLASS_DETAILS_CLASS;
    if (!RegisterClassExW(&details_class)) {
        logger_write(L"ERROR", L"Event details window class registration failed: %lu", GetLastError());
        return 1;
    }

    ZeroMemory(&main_class, sizeof(main_class));
    main_class.cbSize = sizeof(main_class);
    main_class.style = CS_HREDRAW | CS_VREDRAW;
    main_class.lpfnWndProc = main_window_proc;
    main_class.hInstance = instance;
    main_class.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    main_class.hCursor = LoadCursorW(NULL, IDC_ARROW);
    main_class.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    main_class.lpszClassName = TRACEGLASS_WINDOW_CLASS;
    main_class.hIconSm = main_class.hIcon;
    if (!RegisterClassExW(&main_class)) {
        logger_write(L"ERROR", L"Main window class registration failed: %lu", GetLastError());
        UnregisterClassW(TRACEGLASS_DETAILS_CLASS, instance);
        return 1;
    }

    StringCchPrintfW(
        window_title,
        ARRAYSIZE(window_title),
        L"TraceGlass v%s - Behavioral Telemetry Viewer",
        TRACEGLASS_VERSION_STRING
    );
    window = CreateWindowExW(
        0, TRACEGLASS_WINDOW_CLASS, window_title,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1280, 780,
        NULL, NULL, instance, NULL
    );
    if (window == NULL) {
        logger_write(L"ERROR", L"Main window creation failed: %lu", GetLastError());
        shutdown_gui_state();
        destroy_fonts();
        UnregisterClassW(TRACEGLASS_WINDOW_CLASS, instance);
        UnregisterClassW(TRACEGLASS_DETAILS_CLASS, instance);
        return 1;
    }

    ShowWindow(window, show_command);
    UpdateWindow(window);
    ZeroMemory(&message, sizeof(message));
    while ((message_result = GetMessageW(&message, NULL, 0, 0)) != 0) {
        if (message_result == -1) {
            logger_write(L"ERROR", L"Message loop failed: %lu", GetLastError());
            result = 1;
            break;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (result == 0) result = (int)message.wParam;
    shutdown_gui_state();
    destroy_fonts();
    UnregisterClassW(TRACEGLASS_WINDOW_CLASS, instance);
    UnregisterClassW(TRACEGLASS_DETAILS_CLASS, instance);
    return result;
}
