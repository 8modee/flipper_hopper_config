#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/variable_item_list.h>
#include <storage/storage.h>
#include <flipper_format/flipper_format.h>
#include <stdlib.h>
#include <string.h>

#define TAG "HopperCfg"
#define SETTING_USER_PATH "/ext/subghz/assets/setting_user"

// Views
typedef enum {
    HopperConfigViewMain,
    HopperConfigViewCount,
} HopperConfigView;

// Scenes
typedef enum {
    HopperConfigSceneMain,
    HopperConfigSceneCount,
} HopperConfigScene;

// Item types
typedef enum {
    ItemTypeHopperFreq,
    ItemTypeHopperPreset,
} ItemType;

// Data for each toggleable item
typedef struct {
    union {
        uint32_t freq;
        char* preset_name;
    } value;
    bool enabled;
    ItemType type;
} HopperItem;

// App state
typedef struct {
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    VariableItemList* variable_item_list;
    HopperItem* items;
    uint16_t item_count;
    uint16_t item_capacity;
    bool modified;
    bool file_exists;
} HopperConfigApp;

// Forward declarations
static void hopper_scene_main_on_enter(void* context);
static bool hopper_scene_main_on_event(void* context, SceneManagerEvent event);
static void hopper_scene_main_on_exit(void* context);
static void hopper_item_changed(VariableItem* item);

// Scene handlers
void (*const scene_on_enter_handlers[])(void*) = {hopper_scene_main_on_enter};
bool (*const scene_on_event_handlers[])(void*, SceneManagerEvent) = {hopper_scene_main_on_event};
void (*const scene_on_exit_handlers[])(void*) = {hopper_scene_main_on_exit};

static const SceneManagerHandlers scene_handlers = {
    .on_enter_handlers = scene_on_enter_handlers,
    .on_event_handlers = scene_on_event_handlers,
    .on_exit_handlers = scene_on_exit_handlers,
    .scene_num = HopperConfigSceneCount,
};

// Helper: safe string dup
static char* hopper_strdup(const char* str) {
    if(!str) return NULL;
    size_t len = strlen(str);
    char* copy = malloc(len + 1);
    furi_check(copy);
    memcpy(copy, str, len + 1);
    return copy;
}

// Helper: trim whitespace from left
static char* hopper_trim_left(char* str) {
    if(!str) return str;
    while(*str == ' ' || *str == '\t') str++;
    return str;
}

// Helper: trim whitespace from right
static void hopper_trim_right(char* str) {
    if(!str) return;
    size_t len = strlen(str);
    while(len > 0 && (str[len - 1] == ' ' || str[len - 1] == '\t' || str[len - 1] == '\r')) {
        str[len - 1] = '\0';
        len--;
    }
}

// Helper: trim both ends
static char* hopper_trim(char* str) {
    if(!str) return str;
    hopper_trim_right(str);
    return hopper_trim_left(str);
}

// Add a frequency item
static void hopper_add_item(HopperConfigApp* app, uint32_t freq, bool enabled) {
    if(app->item_count >= app->item_capacity) {
        app->item_capacity = app->item_capacity ? app->item_capacity * 2 : 32;
        HopperItem* new_items = realloc(app->items, sizeof(HopperItem) * app->item_capacity);
        furi_check(new_items);
        app->items = new_items;
    }
    HopperItem* item = &app->items[app->item_count++];
    item->value.freq = freq;
    item->enabled = enabled;
    item->type = ItemTypeHopperFreq;
}

// Add a preset item
static void hopper_add_preset(HopperConfigApp* app, const char* preset_name, bool enabled) {
    if(app->item_count >= app->item_capacity) {
        app->item_capacity = app->item_capacity ? app->item_capacity * 2 : 32;
        HopperItem* new_items = realloc(app->items, sizeof(HopperItem) * app->item_capacity);
        furi_check(new_items);
        app->items = new_items;
    }
    HopperItem* item = &app->items[app->item_count++];
    item->value.preset_name = hopper_strdup(preset_name);
    item->enabled = enabled;
    item->type = ItemTypeHopperPreset;
}

// Clear all items
static void hopper_clear_items(HopperConfigApp* app) {
    for(uint16_t i = 0; i < app->item_count; i++) {
        if(app->items[i].type == ItemTypeHopperPreset) {
            free(app->items[i].value.preset_name);
        }
    }
    free(app->items);
    app->items = NULL;
    app->item_count = 0;
    app->item_capacity = 0;
    app->modified = false;
    app->file_exists = false;
}

// Try to load as FFF binary format first
static bool hopper_try_load_fff(HopperConfigApp* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* fff = flipper_format_file_alloc(storage);
    FuriString* temp_str = furi_string_alloc();
    uint32_t temp_uint32;
    bool success = false;

    FURI_LOG_I(TAG, "Trying FFF format");

    if(flipper_format_file_open_existing(fff, SETTING_USER_PATH)) {
        // Check header
        if(flipper_format_read_header(fff, temp_str, &temp_uint32)) {
            const char* type = furi_string_get_cstr(temp_str);
            if(strcmp(type, "Flipper SubGhz Setting File") == 0 && temp_uint32 == 1) {
                FURI_LOG_I(TAG, "Valid FFF header");
                
                // Load Hopper_frequency entries
                if(flipper_format_rewind(fff)) {
                    while(flipper_format_read_uint32(fff, "Hopper_frequency", &temp_uint32, 1)) {
                        FURI_LOG_I(TAG, "FFF: Hopper freq %lu", temp_uint32);
                        hopper_add_item(app, temp_uint32, true);
                    }
                }
                
                // Load Hopping_Preset entries
                if(flipper_format_rewind(fff)) {
                    furi_string_reset(temp_str);
                    while(flipper_format_read_string(fff, "Hopping_Preset", temp_str)) {
                        const char* preset = furi_string_get_cstr(temp_str);
                        FURI_LOG_I(TAG, "FFF: Hopping preset %s", preset);
                        hopper_add_preset(app, preset, true);
                    }
                }
                
                // Also try Frequency: and Preset: for compatibility
                if(flipper_format_rewind(fff)) {
                    while(flipper_format_read_uint32(fff, "Frequency", &temp_uint32, 1)) {
                        FURI_LOG_I(TAG, "FFF: Frequency %lu", temp_uint32);
                        hopper_add_item(app, temp_uint32, true);
                    }
                }
                
                if(flipper_format_rewind(fff)) {
                    furi_string_reset(temp_str);
                    while(flipper_format_read_string(fff, "Preset", temp_str)) {
                        const char* preset = furi_string_get_cstr(temp_str);
                        FURI_LOG_I(TAG, "FFF: Preset %s", preset);
                        hopper_add_preset(app, preset, true);
                    }
                }
                
                success = (app->item_count > 0);
            }
        }
        flipper_format_file_close(fff);
    }
    
    furi_string_free(temp_str);
    flipper_format_free(fff);
    furi_record_close(RECORD_STORAGE);
    
    return success;
}

// Fallback: load as text file (ARF format)
static void hopper_load_text_file(HopperConfigApp* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    
    FURI_LOG_I(TAG, "Trying text format");
    
    if(!storage_file_open(file, SETTING_USER_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        FURI_LOG_W(TAG, "File not found: %s", SETTING_USER_PATH);
        storage_file_free(file);
        furi_record_close(RECORD_STORAGE);
        return;
    }
    
    uint64_t size = storage_file_size(file);
    if(size == 0 || size > 65536) {
        storage_file_close(file);
        storage_file_free(file);
        furi_record_close(RECORD_STORAGE);
        return;
    }
    
    char* buf = malloc(size + 1);
    furi_check(buf);
    uint16_t read = storage_file_read(file, buf, (uint16_t)size);
    buf[read] = '\0';
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    
    // Parse file line by line
    char* p = buf;
    while(*p) {
        char* line_start = p;
        while(*p && *p != '\n') p++;
        size_t line_len = p - line_start;
        
        if(*p == '\n') p++;
        
        char* line = malloc(line_len + 1);
        furi_check(line);
        memcpy(line, line_start, line_len);
        line[line_len] = '\0';
        
        // Work with a copy we can modify
        char* work = hopper_strdup(line);
        hopper_trim(work);
        
        if(strlen(work) == 0) {
            free(work);
            free(line);
            continue;
        }
        
        // Check if line is a comment
        bool is_commented = false;
        char* ptr = work;
        if(*ptr == '#') {
            is_commented = true;
            ptr = hopper_trim_left(ptr + 1);
        }
        
        // Check for Hopper_frequency
        if(strncmp(ptr, "Hopper_frequency:", 17) == 0) {
            char* val = hopper_trim_left(ptr + 17);
            uint32_t freq = strtoul(val, NULL, 10);
            if(freq > 0) {
                FURI_LOG_I(TAG, "TEXT: Hopper freq %lu", freq);
                hopper_add_item(app, freq, !is_commented);
            }
        }
        // Check for Hopping_Preset
        else if(strncmp(ptr, "Hopping_Preset:", 15) == 0) {
            char* val = hopper_trim_left(ptr + 15);
            FURI_LOG_I(TAG, "TEXT: Hopping preset %s", val);
            hopper_add_preset(app, val, !is_commented);
        }
        // Check for Frequency: (alternative format)
        else if(strncmp(ptr, "Frequency:", 10) == 0) {
            char* val = hopper_trim_left(ptr + 10);
            uint32_t freq = strtoul(val, NULL, 10);
            if(freq > 0) {
                FURI_LOG_I(TAG, "TEXT: Frequency %lu", freq);
                hopper_add_item(app, freq, !is_commented);
            }
        }
        // Check for Preset: (alternative format)
        else if(strncmp(ptr, "Preset:", 7) == 0) {
            char* val = hopper_trim_left(ptr + 7);
            FURI_LOG_I(TAG, "TEXT: Preset %s", val);
            hopper_add_preset(app, val, !is_commented);
        }
        
        free(work);
        free(line);
    }
    free(buf);
    
    FURI_LOG_I(TAG, "Text parsing loaded %d items", app->item_count);
}

// Load settings from file (tries FFF first, then text)
static void hopper_load_file(HopperConfigApp* app) {
    hopper_clear_items(app);
    app->file_exists = true;
    
    // Try FFF format first
    if(hopper_try_load_fff(app)) {
        FURI_LOG_I(TAG, "Loaded via FFF format");
        return;
    }
    
    // Fallback to text format
    hopper_load_text_file(app);
    
    if(app->item_count == 0) {
        app->file_exists = false;
    }
    
    FURI_LOG_I(TAG, "Total loaded %d items", app->item_count);
}

// Save settings to file (text format for ARF compatibility)
static void hopper_save_file(HopperConfigApp* app) {
    if(!app->modified) return;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_common_mkdir(storage, "/ext/subghz/assets");
    File* file = storage_file_alloc(storage);
    
    FURI_LOG_I(TAG, "Saving to: %s", SETTING_USER_PATH);
    
    if(!storage_file_open(file, SETTING_USER_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        FURI_LOG_E(TAG, "Failed to open %s for writing", SETTING_USER_PATH);
        storage_file_free(file);
        furi_record_close(RECORD_STORAGE);
        return;
    }
    
    // Write header
    storage_file_write(file, "# Hopper Config - Auto-generated\n", 30);
    storage_file_write(file, "Filetype: Flipper SubGhz Setting File\n", 35);
    storage_file_write(file, "Version: 1\n\n", 11);
    
    // Write enabled hopper frequencies
    for(uint16_t i = 0; i < app->item_count; i++) {
        if(app->items[i].type == ItemTypeHopperFreq && app->items[i].enabled) {
            char line[64];
            snprintf(line, sizeof(line), "Hopper_frequency: %lu\n", app->items[i].value.freq);
            storage_file_write(file, line, strlen(line));
        }
    }
    
    // Write enabled hopping presets
    for(uint16_t i = 0; i < app->item_count; i++) {
        if(app->items[i].type == ItemTypeHopperPreset && app->items[i].enabled) {
            char line[64];
            snprintf(line, sizeof(line), "Hopping_Preset: %s\n", app->items[i].value.preset_name);
            storage_file_write(file, line, strlen(line));
        }
    }
    
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    app->modified = false;
    FURI_LOG_I(TAG, "Saved successfully");
}

// UI: Item toggle callback
static void hopper_item_changed(VariableItem* item) {
    HopperConfigApp* app = variable_item_get_context(item);
    uint8_t idx = variable_item_list_get_selected_item_index(app->variable_item_list);
    if(idx >= app->item_count) return;
    app->items[idx].enabled = !app->items[idx].enabled;
    app->modified = true;
    variable_item_set_current_value_index(item, app->items[idx].enabled ? 1 : 0);
    variable_item_set_current_value_text(item, app->items[idx].enabled ? "ON" : "OFF");
}

// Navigation callback
static bool hopper_navigation_callback(void* context) {
    return scene_manager_handle_back_event(((HopperConfigApp*)context)->scene_manager);
}

// Scene: Main - populate UI
static void hopper_scene_main_on_enter(void* context) {
    HopperConfigApp* app = context;
    hopper_load_file(app);
    variable_item_list_reset(app->variable_item_list);

    uint16_t freq_count = 0, preset_count = 0;
    for(uint16_t i = 0; i < app->item_count; i++) {
        if(app->items[i].type == ItemTypeHopperFreq) freq_count++;
        else if(app->items[i].type == ItemTypeHopperPreset) preset_count++;
    }

    if(freq_count > 0) {
        VariableItem* h = variable_item_list_add(app->variable_item_list, "--- Hopper Frequencies ---", 1, NULL, app);
        variable_item_set_current_value_text(h, "");
    }
    for(uint16_t i = 0; i < app->item_count; i++) {
        if(app->items[i].type == ItemTypeHopperFreq) {
            char label[64];
            snprintf(label, sizeof(label), "%lu MHz", app->items[i].value.freq / 1000000);
            VariableItem* vi = variable_item_list_add(app->variable_item_list, label, 2, hopper_item_changed, app);
            variable_item_set_current_value_index(vi, app->items[i].enabled ? 1 : 0);
            variable_item_set_current_value_text(vi, app->items[i].enabled ? "ON" : "OFF");
        }
    }
    if(preset_count > 0) {
        VariableItem* h = variable_item_list_add(app->variable_item_list, "--- Hopper Presets ---", 1, NULL, app);
        variable_item_set_current_value_text(h, "");
    }
    for(uint16_t i = 0; i < app->item_count; i++) {
        if(app->items[i].type == ItemTypeHopperPreset) {
            char label[64];
            snprintf(label, sizeof(label), "%s", app->items[i].value.preset_name ? app->items[i].value.preset_name : "???");
            VariableItem* vi = variable_item_list_add(app->variable_item_list, label, 2, hopper_item_changed, app);
            variable_item_set_current_value_index(vi, app->items[i].enabled ? 1 : 0);
            variable_item_set_current_value_text(vi, app->items[i].enabled ? "ON" : "OFF");
        }
    }
    if(app->item_count == 0) {
        VariableItem* item = variable_item_list_add(app->variable_item_list, "No hopper items", 1, NULL, app);
        variable_item_set_current_value_text(item, app->file_exists ? "File empty" : "File not found");
    }
    view_dispatcher_switch_to_view(app->view_dispatcher, HopperConfigViewMain);
}

static bool hopper_scene_main_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

static void hopper_scene_main_on_exit(void* context) {
    HopperConfigApp* app = context;
    if(app->modified) hopper_save_file(app);
    variable_item_list_reset(app->variable_item_list);
    hopper_clear_items(app);
}

// App allocation
static HopperConfigApp* hopper_app_alloc() {
    HopperConfigApp* app = malloc(sizeof(HopperConfigApp));
    memset(app, 0, sizeof(HopperConfigApp));
    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&scene_handlers, app);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, hopper_navigation_callback);
    app->variable_item_list = variable_item_list_alloc();
    view_dispatcher_add_view(app->view_dispatcher, HopperConfigViewMain, variable_item_list_get_view(app->variable_item_list));
    Gui* gui = furi_record_open(RECORD_GUI);
    view_dispatcher_attach_to_gui(app->view_dispatcher, gui, ViewDispatcherTypeFullscreen);
    scene_manager_next_scene(app->scene_manager, HopperConfigSceneMain);
    return app;
}

// App cleanup
static void hopper_app_free(HopperConfigApp* app) {
    scene_manager_stop(app->scene_manager);
    view_dispatcher_remove_view(app->view_dispatcher, HopperConfigViewMain);
    variable_item_list_free(app->variable_item_list);
    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);
    hopper_clear_items(app);
    free(app);
    furi_record_close(RECORD_GUI);
}

// Entry point
int32_t subghz_config_editor_app(void* p) {
    UNUSED(p);
    HopperConfigApp* app = hopper_app_alloc();
    view_dispatcher_run(app->view_dispatcher);
    hopper_app_free(app);
    return 0;
}
