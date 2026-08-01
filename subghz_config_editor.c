#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/variable_item_list.h>
#include <storage/storage.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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
    ItemTypeOther,
} ItemType;

// Data for each line in the file
typedef struct {
    char* raw_line;          // Original line from file
    char* display_name;      // Display name (frequency value or preset name)
    bool enabled;            // ON/OFF state
    ItemType type;          // Type of item
    bool is_toggleable;      // Whether this line can be toggled
} ConfigLine;

// App state
typedef struct {
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    VariableItemList* variable_item_list;
    ConfigLine* lines;
    uint16_t line_count;
    uint16_t line_capacity;
    uint16_t* toggle_indices;  // Indices of toggleable items in lines array
    uint16_t toggle_count;
    uint16_t toggle_capacity;
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

// Add a line from the file
static void hopper_add_line(HopperConfigApp* app, const char* raw_line) {
    if(app->line_count >= app->line_capacity) {
        app->line_capacity = app->line_capacity ? app->line_capacity * 2 : 64;
        ConfigLine* new_lines = realloc(app->lines, sizeof(ConfigLine) * app->line_capacity);
        furi_check(new_lines);
        app->lines = new_lines;
    }
    
    ConfigLine* line = &app->lines[app->line_count];
    memset(line, 0, sizeof(ConfigLine));
    line->raw_line = hopper_strdup(raw_line);
    line->is_toggleable = false;
    line->enabled = true;
    line->type = ItemTypeOther;
    
    // Work with a copy we can modify
    char* work = hopper_strdup(raw_line);
    hopper_trim(work);
    
    // Check if line is empty
    if(strlen(work) == 0) {
        free(work);
        app->line_count++;
        return;
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
        line->is_toggleable = true;
        line->type = ItemTypeHopperFreq;
        line->enabled = !is_commented;
        char* val = hopper_trim_left(ptr + 17);
        line->display_name = hopper_strdup(val);
    }
    // Check for Hopping_Preset
    else if(strncmp(ptr, "Hopping_Preset:", 15) == 0) {
        line->is_toggleable = true;
        line->type = ItemTypeHopperPreset;
        line->enabled = !is_commented;
        char* val = hopper_trim_left(ptr + 15);
        line->display_name = hopper_strdup(val);
    }
    // Check for Frequency: (alternative format)
    else if(strncmp(ptr, "Frequency:", 10) == 0) {
        line->is_toggleable = true;
        line->type = ItemTypeHopperFreq;
        line->enabled = !is_commented;
        char* val = hopper_trim_left(ptr + 10);
        line->display_name = hopper_strdup(val);
    }
    // Check for Preset: (alternative format)
    else if(strncmp(ptr, "Preset:", 7) == 0) {
        line->is_toggleable = true;
        line->type = ItemTypeHopperPreset;
        line->enabled = !is_commented;
        char* val = hopper_trim_left(ptr + 7);
        line->display_name = hopper_strdup(val);
    }
    
    free(work);
    app->line_count++;
}

// Add index of a toggleable item
static void hopper_add_toggle_index(HopperConfigApp* app, uint16_t line_idx) {
    if(app->toggle_count >= app->toggle_capacity) {
        app->toggle_capacity = app->toggle_capacity ? app->toggle_capacity * 2 : 32;
        uint16_t* new_indices = realloc(app->toggle_indices, sizeof(uint16_t) * app->toggle_capacity);
        furi_check(new_indices);
        app->toggle_indices = new_indices;
    }
    app->toggle_indices[app->toggle_count++] = line_idx;
}

// Clear all data
static void hopper_clear_data(HopperConfigApp* app) {
    for(uint16_t i = 0; i < app->line_count; i++) {
        free(app->lines[i].raw_line);
        free(app->lines[i].display_name);
    }
    free(app->lines);
    free(app->toggle_indices);
    app->lines = NULL;
    app->line_count = 0;
    app->line_capacity = 0;
    app->toggle_indices = NULL;
    app->toggle_count = 0;
    app->toggle_capacity = 0;
    app->modified = false;
}

// Load settings from file
static void hopper_load_file(HopperConfigApp* app) {
    hopper_clear_data(app);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    
    FURI_LOG_I(TAG, "Loading: %s", SETTING_USER_PATH);
    
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
        
        // Handle line ending
        if(*p == '\n') {
            p++;
        }
        
        // Create null-terminated line
        char* line = malloc(line_len + 1);
        furi_check(line);
        memcpy(line, line_start, line_len);
        line[line_len] = '\0';
        
        hopper_add_line(app, line);
        free(line);
    }
    free(buf);
    
    FURI_LOG_I(TAG, "Loaded %d lines, %d toggleable items", app->line_count, app->toggle_count);
}

// Save settings to file
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
    
    // Write each line back, updating toggleable items
    for(uint16_t i = 0; i < app->line_count; i++) {
        ConfigLine* line = &app->lines[i];
        
        if(line->is_toggleable) {
            // Reconstruct the line based on current state
            const char* prefix = line->enabled ? "" : "# ";
            const char* key = "";
            
            if(line->type == ItemTypeHopperFreq) {
                key = "Hopper_frequency:";
            } else if(line->type == ItemTypeHopperPreset) {
                key = "Hopping_Preset:";
            } else if(line->type == ItemTypeOther) {
                // For Frequency: or Preset: format
                if(strstr(line->raw_line, "Frequency:")) {
                    key = "Frequency:";
                } else if(strstr(line->raw_line, "Preset:")) {
                    key = "Preset:";
                }
            }
            
            if(strlen(key) > 0) {
                // Write the line with proper formatting
                char temp_buf[256];
                snprintf(temp_buf, sizeof(temp_buf), "%s%s %s\n", prefix, key, line->display_name ? line->display_name : "");
                storage_file_write(file, temp_buf, strlen(temp_buf));
            } else {
                // Fallback: write original line
                storage_file_write(file, line->raw_line, strlen(line->raw_line));
                storage_file_write(file, "\n", 1);
            }
        } else {
            // Write non-toggleable lines as-is
            storage_file_write(file, line->raw_line, strlen(line->raw_line));
            storage_file_write(file, "\n", 1);
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
    if(idx >= app->toggle_count) return;
    
    uint16_t line_idx = app->toggle_indices[idx];
    app->lines[line_idx].enabled = !app->lines[line_idx].enabled;
    app->modified = true;
    
    variable_item_set_current_value_index(item, app->lines[line_idx].enabled ? 1 : 0);
    variable_item_set_current_value_text(item, app->lines[line_idx].enabled ? "ON" : "OFF");
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
    
    // Build list of toggleable items
    for(uint16_t i = 0; i < app->line_count; i++) {
        if(app->lines[i].is_toggleable) {
            hopper_add_toggle_index(app, i);
        }
    }
    
    // Add separators for frequencies and presets
    uint16_t freq_count = 0, preset_count = 0;
    for(uint16_t i = 0; i < app->toggle_count; i++) {
        uint16_t line_idx = app->toggle_indices[i];
        if(app->lines[line_idx].type == ItemTypeHopperFreq || 
           (app->lines[line_idx].type == ItemTypeOther && strstr(app->lines[line_idx].raw_line, "Frequency:"))) {
            freq_count++;
        } else if(app->lines[line_idx].type == ItemTypeHopperPreset || 
                  (app->lines[line_idx].type == ItemTypeOther && strstr(app->lines[line_idx].raw_line, "Preset:"))) {
            preset_count++;
        }
    }
    
    // Add frequency items
    if(freq_count > 0) {
        VariableItem* header = variable_item_list_add(app->variable_item_list, "--- Hopper Frequencies ---", 1, NULL, app);
        variable_item_set_current_value_text(header, "");
    }
    for(uint16_t i = 0; i < app->toggle_count; i++) {
        uint16_t line_idx = app->toggle_indices[i];
        ConfigLine* line = &app->lines[line_idx];
        
        if(line->type == ItemTypeHopperFreq || 
           (line->type == ItemTypeOther && strstr(line->raw_line, "Frequency:"))) {
            char label[64];
            snprintf(label, sizeof(label), "Freq: %s", line->display_name ? line->display_name : "???");
            VariableItem* vi = variable_item_list_add(app->variable_item_list, label, 2, hopper_item_changed, app);
            variable_item_set_current_value_index(vi, line->enabled ? 1 : 0);
            variable_item_set_current_value_text(vi, line->enabled ? "ON" : "OFF");
        }
    }
    
    // Add preset items
    if(preset_count > 0) {
        VariableItem* header = variable_item_list_add(app->variable_item_list, "--- Hopper Presets ---", 1, NULL, app);
        variable_item_set_current_value_text(header, "");
    }
    for(uint16_t i = 0; i < app->toggle_count; i++) {
        uint16_t line_idx = app->toggle_indices[i];
        ConfigLine* line = &app->lines[line_idx];
        
        if(line->type == ItemTypeHopperPreset || 
           (line->type == ItemTypeOther && strstr(line->raw_line, "Preset:"))) {
            char label[64];
            snprintf(label, sizeof(label), "Preset: %s", line->display_name ? line->display_name : "???");
            VariableItem* vi = variable_item_list_add(app->variable_item_list, label, 2, hopper_item_changed, app);
            variable_item_set_current_value_index(vi, line->enabled ? 1 : 0);
            variable_item_set_current_value_text(vi, line->enabled ? "ON" : "OFF");
        }
    }
    
    if(app->toggle_count == 0) {
        VariableItem* item = variable_item_list_add(app->variable_item_list, "No hopper items", 1, NULL, app);
        variable_item_set_current_value_text(item, "File not found or empty");
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
    if(app->modified) {
        hopper_save_file(app);
    }
    variable_item_list_reset(app->variable_item_list);
    hopper_clear_data(app);
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
    hopper_clear_data(app);
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
