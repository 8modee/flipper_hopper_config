#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/variable_item_list.h>
#include <storage/storage.h>
#include <flipper_format/flipper_format.h>
#include <toolbox/stream/buffer_stream.h>
#include <ctype.h>

#define TAG "HopperCfg"
#define SETTING_USER_PATH "/ext/subghz/assets/setting_user"

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
    bool dirty; // Was this item modified?
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
    .scene_num = SubGHzConfigEditorSceneCount,
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

// Add an item to the list
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
    item->dirty = false;
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
    item->dirty = false;
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
}

// Load settings from FFF file
static void hopper_load_file(HopperConfigApp* app) {
    hopper_clear_items(app);
    
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* fff = flipper_format_file_alloc(storage);
    FuriString* temp_str = furi_string_alloc();
    uint32_t temp_uint32;
    
    FURI_LOG_I(TAG, "Loading: %s", SETTING_USER_PATH);
    
    if(flipper_format_file_open_existing(fff, SETTING_USER_PATH)) {
        // Check file type and version
        if(flipper_format_read_header(fff, temp_str, &temp_uint32)) {
            const char* type = furi_string_get_cstr(temp_str);
            if(strcmp(type, "Flipper SubGhz Setting File") == 0 && temp_uint32 == 1) {
                FURI_LOG_I(TAG, "Valid FFF file");
                
                // Load hopper frequencies
                if(flipper_format_rewind(fff)) {
                    while(flipper_format_read_uint32(fff, "Hopper_frequency", &temp_uint32, 1)) {
                        FURI_LOG_I(TAG, "Found hopper freq: %lu", temp_uint32);
                        hopper_add_item(app, temp_uint32, true);
                    }
                }
                
                // Load hopping presets
                if(flipper_format_rewind(fff)) {
                    furi_string_reset(temp_str);
                    while(flipper_format_read_string(fff, "Hopping_Preset", temp_str)) {
                        const char* preset = furi_string_get_cstr(temp_str);
                        FURI_LOG_I(TAG, "Found hopping preset: %s", preset);
                        hopper_add_preset(app, preset, true);
                    }
                }
            }
        }
        flipper_format_file_close(fff);
    } else {
        FURI_LOG_W(TAG, "File not found: %s", SETTING_USER_PATH);
    }
    
    furi_string_free(temp_str);
    flipper_format_free(fff);
    furi_record_close(RECORD_STORAGE);
    
    FURI_LOG_I(TAG, "Loaded %d items", app->item_count);
}

// Save settings to FFF file
static void hopper_save_file(HopperConfigApp* app) {
    if(!app->modified) return;
    
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* fff = flipper_format_file_alloc(storage);
    FuriString* temp_str = furi_string_alloc();
    
    FURI_LOG_I(TAG, "Saving to: %s", SETTING_USER_PATH);
    
    // Open for writing (create if doesn't exist)
    if(flipper_format_file_open_new(fff, SETTING_USER_PATH)) {
        // Write header
        furi_string_set_str(temp_str, "Flipper SubGhz Setting File");
        flipper_format_write_header(fff, temp_str, 1);
        
        // Write hopper frequencies
        for(uint16_t i = 0; i < app->item_count; i++) {
            if(app->items[i].type == ItemTypeHopperFreq && app->items[i].enabled) {
                flipper_format_write_uint32(fff, "Hopper_frequency", &app->items[i].value.freq, 1);
            }
        }
        
        // Write hopping presets
        for(uint16_t i = 0; i < app->item_count; i++) {
            if(app->items[i].type == ItemTypeHopperPreset && app->items[i].enabled) {
                furi_string_set_str(temp_str, app->items[i].value.preset_name);
                flipper_format_write_string(fff, "Hopping_Preset", temp_str);
            }
        }
        
        flipper_format_file_close(fff);
        FURI_LOG_I(TAG, "Saved successfully");
    } else {
        FURI_LOG_E(TAG, "Failed to open file for writing");
    }
    
    furi_string_free(temp_str);
    flipper_format_free(fff);
    furi_record_close(RECORD_STORAGE);
    app->modified = false;
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
    
    // Add section header for frequencies
    if(freq_count > 0) {
        VariableItem* h = variable_item_list_add(app->variable_item_list, "--- Hopper Frequencies ---", 1, NULL, app);
        variable_item_set_current_value_text(h, "");
    }
    
    // Add frequency items
    for(uint16_t i = 0; i < app->item_count; i++) {
        if(app->items[i].type == ItemTypeHopperFreq) {
            char label[64];
            snprintf(label, sizeof(label), "Freq: %lu", app->items[i].value.freq);
            VariableItem* vi = variable_item_list_add(app->variable_item_list, label, 2, hopper_item_changed, app);
            variable_item_set_current_value_index(vi, app->items[i].enabled ? 1 : 0);
            variable_item_set_current_value_text(vi, app->items[i].enabled ? "ON" : "OFF");
        }
    }
    
    // Add section header for presets
    if(preset_count > 0) {
        VariableItem* h = variable_item_list_add(app->variable_item_list, "--- Hopper Presets ---", 1, NULL, app);
        variable_item_set_current_value_text(h, "");
    }
    
    // Add preset items
    for(uint16_t i = 0; i < app->item_count; i++) {
        if(app->items[i].type == ItemTypeHopperPreset) {
            char label[64];
            snprintf(label, sizeof(label), "Preset: %s", app->items[i].value.preset_name);
            VariableItem* vi = variable_item_list_add(app->variable_item_list, label, 2, hopper_item_changed, app);
            variable_item_set_current_value_index(vi, app->items[i].enabled ? 1 : 0);
            variable_item_set_current_value_text(vi, app->items[i].enabled ? "ON" : "OFF");
        }
    }
    
    if(app->item_count == 0) {
        VariableItem* item = variable_item_list_add(app->variable_item_list, "No hopper items", 1, NULL, app);
        variable_item_set_current_value_text(item, "File not found or empty");
    }
    
    view_dispatcher_switch_to_view(app->view_dispatcher, SubGHzConfigEditorViewMain);
}

static bool hopper_scene_main_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

static void hopper_scene_main_on_exit(void* context) {
    HopperConfigApp* app = context;
    if(app->modified) {
        hopper_save_file(app);
    }
    variable_item_list_reset(app->variable_item_list);
    hopper_clear_items(app);
}

// App allocation
static HopperConfigApp* hopper_app_alloc() {
    HopperConfigApp* app = malloc(sizeof(HopperConfigApp));
    memset(app, 0, sizeof(HopperConfigApp));
    
    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&scene_handlers, app);
    
    view_dispatcher_enable_queue(app->view_dispatcher);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, hopper_navigation_callback);
    
    app->variable_item_list = variable_item_list_alloc();
    view_dispatcher_add_view(app->view_dispatcher, SubGHzConfigEditorViewMain,
        variable_item_list_get_view(app->variable_item_list));
    
    Gui* gui = furi_record_open(RECORD_GUI);
    view_dispatcher_attach_to_gui(app->view_dispatcher, gui, ViewDispatcherTypeFullscreen);
    scene_manager_next_scene(app->scene_manager, SubGHzConfigEditorSceneMain);
    
    return app;
}

// App cleanup
static void hopper_app_free(HopperConfigApp* app) {
    scene_manager_stop(app->scene_manager);
    view_dispatcher_remove_view(app->view_dispatcher, SubGHzConfigEditorViewMain);
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