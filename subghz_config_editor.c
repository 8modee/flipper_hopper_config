#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/variable_item_list.h>
#include <storage/storage.h>
#include <ctype.h>

#define TAG "SubGHzCfgEdit"
#define SETTING_USER_PATH "/ext/subghz/assets/setting_user"

/* ---------- Enums ---------- */
typedef enum {
    SubGHzConfigEditorViewMain,
    SubGHzConfigEditorViewCount,
} SubGHzConfigEditorView;

typedef enum {
    SubGHzConfigEditorSceneMain,
    SubGHzConfigEditorSceneCount,
} SubGHzConfigEditorScene;

typedef enum {
    ItemTypeFrequency,
    ItemTypePreset,
    ItemTypeUnknown,
} ItemType;

/* ---------- Data structures ---------- */
typedef struct {
    char* raw_text;      // Original line text
    char* display_text;  // Text to display in UI
    bool is_item;        // Is this a frequency or preset line?
    bool enabled;        // Is it uncommented (ON)?
    ItemType item_type;  // Frequency, Preset, or Unknown
} FileLine;

typedef struct {
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    VariableItemList* variable_item_list;

    FileLine* lines;
    uint16_t line_count;
    uint16_t line_capacity;

    uint16_t* item_indices;  // Indices of lines that are items
    uint16_t item_count;
    uint16_t item_capacity;

    bool modified;
} SubGHzConfigEditorApp;

/* ---------- Forward declarations ---------- */
static void subghz_config_editor_scene_main_on_enter(void* context);
static bool subghz_config_editor_scene_main_on_event(void* context, SceneManagerEvent event);
static void subghz_config_editor_scene_main_on_exit(void* context);
static void subghz_config_editor_item_changed(VariableItem* item);

/* ---------- Scene handlers table ---------- */
void (*const scene_on_enter_handlers[])(void*) = {
    subghz_config_editor_scene_main_on_enter,
};

bool (*const scene_on_event_handlers[])(void*, SceneManagerEvent) = {
    subghz_config_editor_scene_main_on_event,
};

void (*const scene_on_exit_handlers[])(void*) = {
    subghz_config_editor_scene_main_on_exit,
};

static const SceneManagerHandlers scene_handlers = {
    .on_enter_handlers = scene_on_enter_handlers,
    .on_event_handlers = scene_on_event_handlers,
    .on_exit_handlers = scene_on_exit_handlers,
    .scene_num = SubGHzConfigEditorSceneCount,
};

/* ---------- String helpers ---------- */
static char* subghz_config_editor_strdup(const char* str) {
    if(!str) return NULL;
    size_t len = strlen(str);
    char* copy = malloc(len + 1);
    furi_check(copy);
    memcpy(copy, str, len + 1);
    return copy;
}

static char* subghz_config_editor_trim(char* str) {
    if(!str) return str;
    // Trim left
    char* start = str;
    while(*start == ' ' || *start == '\t') start++;
    
    // Trim right
    char* end = start + strlen(start);
    while(end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) {
        end--;
    }
    *end = '\0';
    
    return start;
}

static bool subghz_config_editor_is_numeric(const char* str) {
    if(!str || !*str) return false;
    while(*str) {
        if(!isdigit(*str)) return false;
        str++;
    }
    return true;
}

static bool subghz_config_editor_is_commented(const char* line) {
    if(!line) return false;
    const char* p = subghz_config_editor_trim((char*)line);
    return *p == '#';
}

static void subghz_config_editor_remove_comment(char* line) {
    if(!line) return;
    char* p = subghz_config_editor_trim(line);
    if(*p == '#') {
        memmove(p, p + 1, strlen(p));
        subghz_config_editor_trim(p);
    }
}

/* ---------- Line management ---------- */
static void subghz_config_editor_add_line(SubGHzConfigEditorApp* app, const char* text) {
    if(app->line_count >= app->line_capacity) {
        app->line_capacity = app->line_capacity ? app->line_capacity * 2 : 32;
        FileLine* new_lines = realloc(app->lines, sizeof(FileLine) * app->line_capacity);
        furi_check(new_lines);
        app->lines = new_lines;
    }

    FileLine* line = &app->lines[app->line_count];
    memset(line, 0, sizeof(FileLine));
    line->raw_text = subghz_config_editor_strdup(text);
    line->is_item = false;
    line->enabled = true;
    line->item_type = ItemTypeUnknown;

    // Make a working copy
    char* work = subghz_config_editor_strdup(text);
    subghz_config_editor_trim(work);
    
    if(!*work) {
        // Empty line
        free(work);
        app->line_count++;
        return;
    }

    // Check if commented
    bool commented = (*work == '#');
    if(commented) {
        line->enabled = false;
        // Remove comment for processing
        memmove(work, work + 1, strlen(work));
        subghz_config_editor_trim(work);
    }

    // Skip section headers and pure comments
    if(!*work || *work == '#' || *work == '[') {
        free(work);
        app->line_count++;
        return;
    }

    // Determine item type
    if(subghz_config_editor_is_numeric(work)) {
        line->is_item = true;
        line->item_type = ItemTypeFrequency;
        line->display_text = subghz_config_editor_strdup(work);
    } else if(strncmp(work, "Frequency:", 10) == 0) {
        line->is_item = true;
        line->item_type = ItemTypeFrequency;
        char* val = subghz_config_editor_trim(work + 10);
        line->display_text = subghz_config_editor_strdup(val);
    } else if(strncmp(work, "Preset:", 7) == 0) {
        line->is_item = true;
        line->item_type = ItemTypePreset;
        char* val = subghz_config_editor_trim(work + 7);
        line->display_text = subghz_config_editor_strdup(val);
    } else {
        // Assume it's a preset name (like AM650, OOK_U, etc.)
        line->is_item = true;
        line->item_type = ItemTypePreset;
        line->display_text = subghz_config_editor_strdup(work);
    }

    free(work);
    app->line_count++;
}

static void subghz_config_editor_add_item_index(SubGHzConfigEditorApp* app, uint16_t line_index) {
    if(app->item_count >= app->item_capacity) {
        app->item_capacity = app->item_capacity ? app->item_capacity * 2 : 16;
        uint16_t* new_indices = realloc(app->item_indices, sizeof(uint16_t) * app->item_capacity);
        furi_check(new_indices);
        app->item_indices = new_indices;
    }
    app->item_indices[app->item_count++] = line_index;
}

static void subghz_config_editor_clear_data(SubGHzConfigEditorApp* app) {
    for(uint16_t i = 0; i < app->line_count; i++) {
        free(app->lines[i].raw_text);
        free(app->lines[i].display_text);
    }
    free(app->lines);
    free(app->item_indices);
    app->lines = NULL;
    app->line_count = 0;
    app->line_capacity = 0;
    app->item_indices = NULL;
    app->item_count = 0;
    app->item_capacity = 0;
    app->modified = false;
}

/* ---------- File I/O ---------- */
static void subghz_config_editor_load_file(SubGHzConfigEditorApp* app) {
    subghz_config_editor_clear_data(app);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);

    FURI_LOG_I(TAG, "Attempting to open: %s", SETTING_USER_PATH);
    
    if(!storage_file_open(file, SETTING_USER_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        FURI_LOG_W(TAG, "Failed to open %s, creating new", SETTING_USER_PATH);
        // File doesn't exist, that's okay - we'll work with empty data
        storage_file_free(file);
        furi_record_close(RECORD_STORAGE);
        return;
    }

    uint64_t size = storage_file_size(file);
    if(size == 0) {
        FURI_LOG_W(TAG, "File is empty");
        storage_file_close(file);
        storage_file_free(file);
        furi_record_close(RECORD_STORAGE);
        return;
    }

    // Limit file size to prevent memory issues
    if(size > 65536) {
        FURI_LOG_E(TAG, "File too large: %lu bytes", size);
        size = 65536;
    }

    char* buf = malloc(size + 1);
    furi_check(buf);
    uint16_t read = storage_file_read(file, buf, (uint16_t)size);
    buf[read] = '\0';

    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);

    // Parse lines
    char* p = buf;
    while(*p) {
        char* line_start = p;
        while(*p && *p != '\n') p++;
        size_t line_len = p - line_start;

        char* line = malloc(line_len + 1);
        furi_check(line);
        memcpy(line, line_start, line_len);
        line[line_len] = '\0';

        FURI_LOG_I(TAG, "Parsing line: %s", line);
        subghz_config_editor_add_line(app, line);
        free(line);

        if(*p == '\n') p++;
    }

    FURI_LOG_I(TAG, "Parsed %d lines, %d items", app->line_count, app->item_count);
    free(buf);
}

static void subghz_config_editor_save_file(SubGHzConfigEditorApp* app) {
    if(app->line_count == 0) return;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    
    // Ensure directory exists
    if(!storage_common_stat(storage, "/ext/subghz/assets", NULL)) {
        storage_common_mkdir(storage, "/ext/subghz");
        storage_common_mkdir(storage, "/ext/subghz/assets");
    }

    File* file = storage_file_alloc(storage);
    if(!storage_file_open(file, SETTING_USER_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        FURI_LOG_E(TAG, "Failed to open %s for writing", SETTING_USER_PATH);
        storage_file_free(file);
        furi_record_close(RECORD_STORAGE);
        return;
    }

    for(uint16_t i = 0; i < app->line_count; i++) {
        FileLine* line = &app->lines[i];
        
        if(line->is_item) {
            // Write comment prefix if disabled
            if(!line->enabled) {
                storage_file_write(file, "# ", 2);
            }
            
            // Write the original text (without comment if it had one)
            if(line->raw_text) {
                // Check if original was commented
                char* work = subghz_config_editor_strdup(line->raw_text);
                subghz_config_editor_trim(work);
                bool was_commented = (*work == '#');
                free(work);
                
                if(was_commented && line->enabled) {
                    // Remove comment
                    char* clean = subghz_config_editor_strdup(line->raw_text);
                    subghz_config_editor_remove_comment(clean);
                    storage_file_write(file, clean, strlen(clean));
                    free(clean);
                } else if(!was_commented && !line->enabled) {
                    // Add comment
                    storage_file_write(file, "# ", 2);
                    storage_file_write(file, line->raw_text, strlen(line->raw_text));
                } else {
                    // Keep as-is
                    storage_file_write(file, line->raw_text, strlen(line->raw_text));
                }
            } else if(line->display_text) {
                // Fallback: write display text
                if(!line->enabled) {
                    storage_file_write(file, "# ", 2);
                }
                storage_file_write(file, line->display_text, strlen(line->display_text));
            }
        } else {
            // Non-item line, write as-is
            if(line->raw_text) {
                storage_file_write(file, line->raw_text, strlen(line->raw_text));
            }
        }
        storage_file_write(file, "\n", 1);
    }

    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    
    FURI_LOG_I(TAG, "File saved successfully");
}

/* ---------- UI callbacks ---------- */
static void subghz_config_editor_item_changed(VariableItem* item) {
    SubGHzConfigEditorApp* app = variable_item_get_context(item);
    uint8_t list_index = variable_item_list_get_selected_item_index(app->variable_item_list);
    if(list_index >= app->item_count) return;

    uint16_t line_index = app->item_indices[list_index];
    uint8_t value_index = variable_item_get_current_value_index(item);

    app->lines[line_index].enabled = (value_index == 1);
    app->modified = true;
    
    variable_item_set_current_value_text(item, value_index == 1 ? "ON" : "OFF");
}

static bool subghz_config_editor_navigation_event_callback(void* context) {
    SubGHzConfigEditorApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

/* ---------- Scene: Main ---------- */
static void subghz_config_editor_scene_main_on_enter(void* context) {
    SubGHzConfigEditorApp* app = context;

    subghz_config_editor_load_file(app);
    variable_item_list_reset(app->variable_item_list);

    // Collect item indices
    for(uint16_t i = 0; i < app->line_count; i++) {
        if(app->lines[i].is_item) {
            subghz_config_editor_add_item_index(app, i);
        }
    }

    if(app->item_count == 0) {
        VariableItem* item = variable_item_list_add(
            app->variable_item_list,
            "No config found",
            1,
            NULL,
            app);
        variable_item_set_current_value_text(item, "Add frequencies/presets");
        variable_item_set_current_value_index(item, 0);
    } else {
        for(uint16_t i = 0; i < app->item_count; i++) {
            uint16_t line_index = app->item_indices[i];
            FileLine* line = &app->lines[line_index];

            char label[128];
            if(line->item_type == ItemTypeFrequency) {
                snprintf(label, sizeof(label), "Freq: %s", line->display_text ? line->display_text : "???");
            } else if(line->item_type == ItemTypePreset) {
                snprintf(label, sizeof(label), "Preset: %s", line->display_text ? line->display_text : "???");
            } else {
                snprintf(label, sizeof(label), "%s", line->display_text ? line->display_text : "???");
            }

            VariableItem* vi = variable_item_list_add(
                app->variable_item_list,
                label,
                2,
                subghz_config_editor_item_changed,
                app);

            variable_item_set_current_value_index(vi, line->enabled ? 1 : 0);
            variable_item_set_current_value_text(vi, line->enabled ? "ON" : "OFF");
        }
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, SubGHzConfigEditorViewMain);
}

static bool subghz_config_editor_scene_main_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

static void subghz_config_editor_scene_main_on_exit(void* context) {
    SubGHzConfigEditorApp* app = context;
    if(app->modified) {
        subghz_config_editor_save_file(app);
        app->modified = false;
    }
    variable_item_list_reset(app->variable_item_list);
    subghz_config_editor_clear_data(app);
}

/* ---------- App lifecycle ---------- */
static SubGHzConfigEditorApp* subghz_config_editor_app_alloc() {
    SubGHzConfigEditorApp* app = malloc(sizeof(SubGHzConfigEditorApp));
    memset(app, 0, sizeof(SubGHzConfigEditorApp));

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&scene_handlers, app);

    view_dispatcher_enable_queue(app->view_dispatcher);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, subghz_config_editor_navigation_event_callback);

    app->variable_item_list = variable_item_list_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        SubGHzConfigEditorViewMain,
        variable_item_list_get_view(app->variable_item_list));

    Gui* gui = furi_record_open(RECORD_GUI);
    view_dispatcher_attach_to_gui(app->view_dispatcher, gui, ViewDispatcherTypeFullscreen);

    scene_manager_next_scene(app->scene_manager, SubGHzConfigEditorSceneMain);

    return app;
}

static void subghz_config_editor_app_free(SubGHzConfigEditorApp* app) {
    scene_manager_stop(app->scene_manager);
    view_dispatcher_remove_view(app->view_dispatcher, SubGHzConfigEditorViewMain);
    variable_item_list_free(app->variable_item_list);
    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);
    subghz_config_editor_clear_data(app);
    free(app);
    furi_record_close(RECORD_GUI);
}

/* ---------- Entry point ---------- */
int32_t subghz_config_editor_app(void* p) {
    UNUSED(p);

    SubGHzConfigEditorApp* app = subghz_config_editor_app_alloc();
    view_dispatcher_run(app->view_dispatcher);
    subghz_config_editor_app_free(app);

    return 0;
}