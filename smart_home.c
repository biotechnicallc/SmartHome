#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/dialog_ex.h>
#include <gui/modules/text_input.h>
#include <storage/storage.h>
#include <stdlib.h>

#include "flipper_http/flipper_http.h"

#define TAG "SmartHome"

#define MAX_DEVICES 10
#define DEVICE_NAME_SIZE 32
#define DEVICE_IP_SIZE 16
#define DEVICE_REMOTE_ID_SIZE 64
#define URL_SIZE 320
#define RELAY_URL_SIZE 192
#define API_TOKEN_SIZE 128
#define DEVICE_FILE APP_DATA_PATH("devices.txt")
#define SETTINGS_FILE APP_DATA_PATH("settings.txt")

typedef enum {
    DeviceTypeShelly = 1,
    DeviceTypeHomeAssistantThermostat = 2,
} DeviceType;

typedef struct {
    DeviceType type;
    char name[DEVICE_NAME_SIZE];
    char ip[DEVICE_IP_SIZE];
    char remote_id[DEVICE_REMOTE_ID_SIZE];
} SmartHomeDevice;

typedef struct {
    void* app;
} SmartHomeIpViewModel;

typedef enum {
    SmartHomeViewMain,
    SmartHomeViewDevice,
    SmartHomeViewResult,
    SmartHomeViewTextInput,
    SmartHomeViewIpInput,
    SmartHomeViewDeviceType,
    SmartHomeViewSettings,
} SmartHomeView;

typedef enum {
    DeviceActionOn = 100,
    DeviceActionOff,
    DeviceActionToggle,
    DeviceActionRename,
    DeviceActionDelete,

    DeviceActionThermostatStatus,
    DeviceActionThermostatTempUp,
    DeviceActionThermostatTempDown,
    DeviceActionThermostatSetTemperature,
    DeviceActionThermostatCool,
    DeviceActionThermostatHeat,
    DeviceActionThermostatAuto,
    DeviceActionThermostatOff,
    DeviceActionThermostatFanAuto,
    DeviceActionThermostatFanOn,
} DeviceAction;

typedef enum {
    InputModeNone,
    InputModeName,
    InputModeIp,
    InputModeRename,
    InputModeSetTemperature,
    InputModeHaEntity,
    InputModeRelayUrl,
    InputModeApiToken,
} InputMode;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;

    Submenu* main_menu;
    Submenu* device_menu;
    Submenu* device_type_menu;
    Submenu* settings_menu;
    DialogEx* dialog;
    TextInput* text_input;
    View* ip_input_view;

    FlipperHTTP* fhttp;

    SmartHomeDevice devices[MAX_DEVICES];
    size_t device_count;
    size_t selected_device;
    DeviceType pending_device_type;

    char input_buffer[DEVICE_NAME_SIZE];
    char pending_name[DEVICE_NAME_SIZE];
    char entity_input[DEVICE_REMOTE_ID_SIZE];

    char relay_url[RELAY_URL_SIZE];
    char api_token[API_TOKEN_SIZE];
    char settings_input[RELAY_URL_SIZE];

    uint8_t ip_bytes[4];
    uint8_t ip_selected_octet;
    InputMode input_mode;
} SmartHomeApp;

/* ---------- FlipperHTTP ---------- */

static bool smart_home_wait_for_board(FlipperHTTP* fhttp) {
    if(!flipper_http_send_command(fhttp, HTTP_CMD_PING)) {
        return false;
    }

    uint8_t counter = 20;

    while((fhttp->state == INACTIVE) && (--counter > 0)) {
        furi_delay_ms(100);
    }

    return counter > 0;
}

static bool smart_home_send(SmartHomeApp* app, const char* url) {
    app->fhttp->state = IDLE;

    char headers[192];
    snprintf(
        headers,
        sizeof(headers),
        "{\"Authorization\":\"Bearer %s\"}",
        app->api_token);

    if(!flipper_http_request(app->fhttp, GET, url, headers, NULL)) {
        return false;
    }

    app->fhttp->state = RECEIVING;

    uint8_t counter = 100;

    while((app->fhttp->state != IDLE) &&
          (app->fhttp->state != ISSUE) &&
          (--counter > 0)) {
        furi_delay_ms(100);
    }

    if(app->fhttp->state == ISSUE) {
        return false;
    }

    return counter > 0;
}

static bool smart_home_send_post(
    SmartHomeApp* app,
    const char* url,
    const char* payload) {

    app->fhttp->state = IDLE;

    if(app->fhttp->last_response) {
        app->fhttp->last_response[0] = '\0';
    }

    char headers[224];

    snprintf(
        headers,
        sizeof(headers),
        "{\"Authorization\":\"Bearer %s\","
        "\"Content-Type\":\"application/json\"}",
        app->api_token);

    if(!flipper_http_request(
           app->fhttp,
           POST,
           url,
           headers,
           payload ? payload : "{}")) {
        return false;
    }

    app->fhttp->state = RECEIVING;

    uint8_t counter = 100;

    while((app->fhttp->state != IDLE) &&
          (app->fhttp->state != ISSUE) &&
          (--counter > 0)) {
        furi_delay_ms(100);
    }

    if(app->fhttp->state == ISSUE) {
        return false;
    }

    return counter > 0;
}


static bool smart_home_extract_json_scalar(
    const char* json,
    const char* key,
    char* output,
    size_t output_size) {

    if(!json || !key || !output || output_size == 0) {
        return false;
    }

    char needle[64];

    int needle_len = snprintf(
        needle,
        sizeof(needle),
        "\"%s\"",
        key);

    if(needle_len <= 0 ||
       (size_t)needle_len >= sizeof(needle)) {
        return false;
    }

    const char* pos = strstr(json, needle);

    if(!pos) {
        return false;
    }

    pos += strlen(needle);

    while(*pos == ' ' ||
          *pos == '\t' ||
          *pos == '\r' ||
          *pos == '\n') {
        pos++;
    }

    if(*pos != ':') {
        return false;
    }

    pos++;

    while(*pos == ' ' ||
          *pos == '\t' ||
          *pos == '\r' ||
          *pos == '\n') {
        pos++;
    }

    bool quoted = false;

    if(*pos == '"') {
        quoted = true;
        pos++;
    }

    const char* end = pos;

    if(quoted) {
        while(*end && *end != '"') {
            end++;
        }
    } else {
        while(*end &&
              *end != ',' &&
              *end != '}' &&
              *end != ']' &&
              *end != '\r' &&
              *end != '\n') {
            end++;
        }

        while(end > pos &&
              (end[-1] == ' ' || end[-1] == '\t')) {
            end--;
        }
    }

    if(end <= pos) {
        return false;
    }

    size_t len = (size_t)(end - pos);

    if(len >= output_size) {
        len = output_size - 1;
    }

    memcpy(output, pos, len);
    output[len] = '\0';

    return true;
}


static bool smart_home_extract_json_string(
    const char* json,
    const char* key,
    char* output,
    size_t output_size) {

    if(!json || !key || !output || output_size == 0) {
        return false;
    }

    char needle[48];

    int needle_len = snprintf(
        needle,
        sizeof(needle),
        "\"%s\"",
        key);

    if(needle_len <= 0 ||
       (size_t)needle_len >= sizeof(needle)) {
        return false;
    }

    const char* pos = strstr(json, needle);

    if(!pos) {
        return false;
    }

    pos += strlen(needle);

    while(*pos == ' ' ||
          *pos == '\t' ||
          *pos == '\r' ||
          *pos == '\n') {
        pos++;
    }

    if(*pos != ':') {
        return false;
    }

    pos++;

    while(*pos == ' ' ||
          *pos == '\t' ||
          *pos == '\r' ||
          *pos == '\n') {
        pos++;
    }

    if(*pos != '"') {
        return false;
    }

    pos++;

    const char* end = strchr(pos, '"');

    if(!end) {
        return false;
    }

    size_t len = (size_t)(end - pos);

    if(len == 0 || len >= output_size) {
        return false;
    }

    memcpy(output, pos, len);
    output[len] = '\0';

    return true;
}

static bool smart_home_register_shelly(
    SmartHomeApp* app,
    const char* ip,
    char* remote_id,
    size_t remote_id_size) {

    if(!app || !app->fhttp ||
       !ip || !remote_id ||
       remote_id_size == 0) {
        return false;
    }

    if(app->relay_url[0] == '\0' ||
       app->api_token[0] == '\0') {
        return false;
    }

    char url[URL_SIZE];
    char headers[192];
    char payload[96];

    snprintf(
        url,
        sizeof(url),
        "%s/register",
        app->relay_url);

    snprintf(
        headers,
        sizeof(headers),
        "{\"Authorization\":\"Bearer %s\","
        "\"Content-Type\":\"application/json\"}",
        app->api_token);

    snprintf(
        payload,
        sizeof(payload),
        "{\"type\":\"shelly\",\"ip\":\"%s\"}",
        ip);

    app->fhttp->state = IDLE;

    if(app->fhttp->last_response) {
        app->fhttp->last_response[0] = '\0';
    }

    if(!flipper_http_request(
           app->fhttp,
           POST,
           url,
           headers,
           payload)) {
        return false;
    }

    app->fhttp->state = RECEIVING;

    uint8_t counter = 100;

    while((app->fhttp->state != IDLE) &&
          (app->fhttp->state != ISSUE) &&
          (--counter > 0)) {
        furi_delay_ms(100);
    }

    if(app->fhttp->state == ISSUE ||
       counter == 0 ||
       !app->fhttp->last_response) {
        return false;
    }

    const char* response =
        app->fhttp->last_response;

    if(strstr(response, "\"ok\":true") == NULL &&
       strstr(response, "\"ok\": true") == NULL) {
        return false;
    }

    if(!smart_home_extract_json_string(
           response,
           "id",
           remote_id,
           remote_id_size)) {
        return false;
    }

    if(strncmp(remote_id, "shelly", 6) != 0) {
        remote_id[0] = '\0';
        return false;
    }

    return true;
}

/* ---------- Storage ---------- */

static const char* smart_home_device_type_name(DeviceType type) {
    switch(type) {
    case DeviceTypeShelly:
        return "shelly";
    case DeviceTypeHomeAssistantThermostat:
        return "ha_thermostat";
    default:
        return "unknown";
    }
}

static bool smart_home_save_devices(SmartHomeApp* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);

    bool ok = storage_file_open(
        file,
        DEVICE_FILE,
        FSAM_WRITE,
        FSOM_CREATE_ALWAYS);

    if(ok) {
        for(size_t i = 0; i < app->device_count; i++) {
            char line[
                16 +
                DEVICE_NAME_SIZE +
                DEVICE_IP_SIZE +
                DEVICE_REMOTE_ID_SIZE +
                8];

            int len = snprintf(
                line,
                sizeof(line),
                "%s|%s|%s|%s\n",
                smart_home_device_type_name(app->devices[i].type),
                app->devices[i].name,
                app->devices[i].ip,
                app->devices[i].remote_id);

            if(len > 0 && (size_t)len < sizeof(line)) {
                storage_file_write(file, line, (size_t)len);
            }
        }
    }

    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);

    return ok;
}

static void smart_home_load_devices(SmartHomeApp* app) {
    app->device_count = 0;

    Storage* storage = furi_record_open(RECORD_STORAGE);

    if(!storage_file_exists(storage, DEVICE_FILE)) {
        furi_record_close(RECORD_STORAGE);
        return;
    }

    File* file = storage_file_alloc(storage);

    bool opened = storage_file_open(
        file,
        DEVICE_FILE,
        FSAM_READ,
        FSOM_OPEN_EXISTING);

    if(opened) {
        uint64_t file_size = storage_file_size(file);

        if(file_size > 0 && file_size < 4096) {
            char* data = malloc((size_t)file_size + 1);

            if(data) {
                size_t read =
                    storage_file_read(file, data, (size_t)file_size);

                data[read] = '\0';

                char* line = data;

                while(*line && app->device_count < MAX_DEVICES) {
                    char* newline = strchr(line, '\n');

                    if(newline) {
                        *newline = '\0';
                    }

                    /*
                     * New format:
                     *   type|name|ip|remote_id
                     *
                     * Legacy format:
                     *   name|ip
                     */
                    char* first = strchr(line, '|');

                    if(first) {
                        char* second = strchr(first + 1, '|');

                        SmartHomeDevice* device =
                            &app->devices[app->device_count];

                        memset(device, 0, sizeof(SmartHomeDevice));

                        if(second) {
                            /*
                             * New generic format.
                             */
                            char* third = strchr(second + 1, '|');

                            if(third) {
                                *first = '\0';
                                *second = '\0';
                                *third = '\0';

                                const char* type = line;
                                const char* name = first + 1;
                                const char* ip = second + 1;
                                const char* remote_id = third + 1;

                                DeviceType parsed_type = 0;
                                bool valid_record = false;

                                if(strcmp(type, "shelly") == 0 &&
                                   strlen(name) > 0 &&
                                   strlen(ip) > 0) {

                                    parsed_type = DeviceTypeShelly;
                                    valid_record = true;

                                } else if(
                                    strcmp(type, "ha_thermostat") == 0 &&
                                    strlen(name) > 0 &&
                                    strlen(remote_id) > 0) {

                                    parsed_type =
                                        DeviceTypeHomeAssistantThermostat;
                                    valid_record = true;
                                }

                                if(valid_record) {
                                    device->type = parsed_type;

                                    strlcpy(
                                        device->name,
                                        name,
                                        sizeof(device->name));

                                    strlcpy(
                                        device->ip,
                                        ip,
                                        sizeof(device->ip));

                                    strlcpy(
                                        device->remote_id,
                                        remote_id,
                                        sizeof(device->remote_id));

                                    app->device_count++;
                                }
                            }
                        } else {
                            /*
                             * Legacy name|ip record.
                             * Existing versions only supported Shelly,
                             * so legacy records migrate as Shelly.
                             */
                            *first = '\0';

                            const char* name = line;
                            const char* ip = first + 1;

                            if(strlen(name) > 0 && strlen(ip) > 0) {
                                device->type = DeviceTypeShelly;

                                strlcpy(
                                    device->name,
                                    name,
                                    sizeof(device->name));

                                strlcpy(
                                    device->ip,
                                    ip,
                                    sizeof(device->ip));

                                device->remote_id[0] = '\0';

                                app->device_count++;
                            }
                        }
                    }

                    if(!newline) {
                        break;
                    }

                    line = newline + 1;
                }

                free(data);
            }
        }
    }

    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}

/* ---------- Settings storage ---------- */

static bool smart_home_save_settings(SmartHomeApp* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);

    bool ok = storage_file_open(
        file,
        SETTINGS_FILE,
        FSAM_WRITE,
        FSOM_CREATE_ALWAYS);

    if(ok) {
        char line[RELAY_URL_SIZE + 32];

        int len = snprintf(
            line,
            sizeof(line),
            "relay_url=%s\n",
            app->relay_url);

        if(len > 0 && (size_t)len < sizeof(line)) {
            storage_file_write(file, line, (size_t)len);
        }

        len = snprintf(
            line,
            sizeof(line),
            "api_token=%s\n",
            app->api_token);

        if(len > 0 && (size_t)len < sizeof(line)) {
            storage_file_write(file, line, (size_t)len);
        }
    }

    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);

    return ok;
}

static void smart_home_load_settings(SmartHomeApp* app) {
    /*
     * Public-safe defaults.
     *
     * A fresh installation contains no relay URL or API token.
     * Existing installations load them from settings.txt below.
     */
    app->relay_url[0] = '\0';
    app->api_token[0] = '\0';

    Storage* storage = furi_record_open(RECORD_STORAGE);

    if(!storage_file_exists(storage, SETTINGS_FILE)) {
        furi_record_close(RECORD_STORAGE);
        return;
    }

    File* file = storage_file_alloc(storage);

    bool opened = storage_file_open(
        file,
        SETTINGS_FILE,
        FSAM_READ,
        FSOM_OPEN_EXISTING);

    if(opened) {
        uint64_t file_size = storage_file_size(file);

        if(file_size > 0 && file_size < 1024) {
            char* data = malloc((size_t)file_size + 1);

            if(data) {
                size_t read =
                    storage_file_read(
                        file,
                        data,
                        (size_t)file_size);

                data[read] = '\0';

                char* line = data;

                while(*line) {
                    char* newline = strchr(line, '\n');

                    if(newline) {
                        *newline = '\0';
                    }

                    if(strncmp(
                           line,
                           "relay_url=",
                           strlen("relay_url=")) == 0) {

                        strlcpy(
                            app->relay_url,
                            line + strlen("relay_url="),
                            sizeof(app->relay_url));

                    } else if(strncmp(
                                  line,
                                  "api_token=",
                                  strlen("api_token=")) == 0) {

                        strlcpy(
                            app->api_token,
                            line + strlen("api_token="),
                            sizeof(app->api_token));
                    }

                    if(!newline) {
                        break;
                    }

                    line = newline + 1;
                }

                free(data);
            }
        }
    }

    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}

/* ---------- UI helpers ---------- */

static uint32_t smart_home_exit_callback(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static uint32_t smart_home_device_back(void* context) {
    UNUSED(context);
    return SmartHomeViewMain;
}

static uint32_t smart_home_result_back(void* context) {
    SmartHomeApp* app = context;
    UNUSED(app);
    return SmartHomeViewDevice;
}

static uint32_t smart_home_input_back(void* context) {
    UNUSED(context);
    return SmartHomeViewMain;
}

static void smart_home_show_result(
    SmartHomeApp* app,
    const char* header,
    const char* message) {

    dialog_ex_set_header(
        app->dialog,
        header,
        64,
        12,
        AlignCenter,
        AlignCenter);

    dialog_ex_set_text(
        app->dialog,
        message,
        64,
        34,
        AlignCenter,
        AlignCenter);

    dialog_ex_set_center_button_text(app->dialog, "OK");

    view_dispatcher_switch_to_view(
        app->view_dispatcher,
        SmartHomeViewResult);
}

static void smart_home_refresh_main_menu(SmartHomeApp* app);

static void smart_home_open_device(
    SmartHomeApp* app,
    size_t index) {

    if(index >= app->device_count) {
        return;
    }

    app->selected_device = index;

    submenu_reset(app->device_menu);
    submenu_set_header(
        app->device_menu,
        app->devices[index].name);
}

/* ---------- Input ---------- */

static void smart_home_ip_entered(void* context) {
    SmartHomeApp* app = context;

    if(app->device_count >= MAX_DEVICES) {
        smart_home_show_result(
            app,
            "Smart Home",
            "Device list full");
        return;
    }

    SmartHomeDevice* device =
        &app->devices[app->device_count];

    memset(device, 0, sizeof(SmartHomeDevice));
    device->type = DeviceTypeShelly;
    device->remote_id[0] = '\0';

    strlcpy(
        device->name,
        app->pending_name,
        sizeof(device->name));

    snprintf(
        device->ip,
        sizeof(device->ip),
        "%u.%u.%u.%u",
        (unsigned int)app->ip_bytes[0],
        (unsigned int)app->ip_bytes[1],
        (unsigned int)app->ip_bytes[2],
        (unsigned int)app->ip_bytes[3]);

    /*
     * A fresh/public installation requires remote access settings
     * before a Shelly can be registered.
     */
    if(app->relay_url[0] == '\0' ||
       app->api_token[0] == '\0') {

        memset(device, 0, sizeof(SmartHomeDevice));

        smart_home_show_result(
            app,
            "Remote Setup",
            "Configure Settings first");

        return;
    }

    /*
     * Register the Shelly with the relay before committing the
     * new device locally. The relay returns the stable Shelly ID.
     */
    if(!smart_home_register_shelly(
           app,
           device->ip,
           device->remote_id,
           sizeof(device->remote_id))) {

        memset(device, 0, sizeof(SmartHomeDevice));

        smart_home_show_result(
            app,
            "Remote Error",
            "Registration failed");

        return;
    }

    app->device_count++;

    smart_home_save_devices(app);
    smart_home_refresh_main_menu(app);

    /*
     * Adding a device originates from our custom IP input view.
     * Return directly to the refreshed device list rather than
     * routing through the generic action-result dialog.
     */
    view_dispatcher_switch_to_view(
        app->view_dispatcher,
        SmartHomeViewMain);
}


static void smart_home_ip_draw_callback(Canvas* canvas, void* model_ptr) {
    SmartHomeIpViewModel* model = model_ptr;
    SmartHomeApp* app = model->app;

    char ip[DEVICE_IP_SIZE];

    snprintf(
        ip,
        sizeof(ip),
        "%u.%u.%u.%u",
        (unsigned int)app->ip_bytes[0],
        (unsigned int)app->ip_bytes[1],
        (unsigned int)app->ip_bytes[2],
        (unsigned int)app->ip_bytes[3]);

    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(
        canvas,
        64,
        8,
        AlignCenter,
        AlignCenter,
        "Enter device IP");

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(
        canvas,
        64,
        29,
        AlignCenter,
        AlignCenter,
        ip);

    /*
     * Show which decimal octet is currently selected.
     */
    char selector[32];

    snprintf(
        selector,
        sizeof(selector),
        "Octet %u of 4: %u",
        (unsigned int)(app->ip_selected_octet + 1),
        (unsigned int)app->ip_bytes[app->ip_selected_octet]);

    canvas_draw_str_aligned(
        canvas,
        64,
        43,
        AlignCenter,
        AlignCenter,
        selector);

    canvas_draw_str_aligned(
        canvas,
        64,
        55,
        AlignCenter,
        AlignCenter,
        "< > select   ^ v change");

    canvas_draw_str_aligned(
        canvas,
        64,
        64,
        AlignCenter,
        AlignBottom,
        "OK = Save");
}

static bool smart_home_ip_input_callback(
    InputEvent* event,
    void* context) {

    SmartHomeApp* app = context;

    /*
     * Short handles normal clicks.
     * Repeat makes holding Up/Down rapidly change the value.
     */
    if((event->type != InputTypeShort) &&
       (event->type != InputTypeRepeat)) {
        return false;
    }

    bool handled = true;

    switch(event->key) {
    case InputKeyLeft:
        if(event->type == InputTypeShort) {
            if(app->ip_selected_octet == 0) {
                app->ip_selected_octet = 3;
            } else {
                app->ip_selected_octet--;
            }
        }
        break;

    case InputKeyRight:
        if(event->type == InputTypeShort) {
            app->ip_selected_octet =
                (app->ip_selected_octet + 1) % 4;
        }
        break;

    case InputKeyUp:
        if(app->ip_bytes[app->ip_selected_octet] < 255) {
            app->ip_bytes[app->ip_selected_octet]++;
        } else {
            app->ip_bytes[app->ip_selected_octet] = 0;
        }
        break;

    case InputKeyDown:
        if(app->ip_bytes[app->ip_selected_octet] > 0) {
            app->ip_bytes[app->ip_selected_octet]--;
        } else {
            app->ip_bytes[app->ip_selected_octet] = 255;
        }
        break;

    case InputKeyOk:
        if(event->type == InputTypeShort) {
            smart_home_ip_entered(app);
        }
        break;

    default:
        handled = false;
        break;
    }

    if(handled &&
       event->key != InputKeyOk) {
        /*
         * State is stored directly in SmartHomeApp rather than
         * in a View model. Request a redraw by switching back
         * to the already-active view.
         */
        view_dispatcher_switch_to_view(
            app->view_dispatcher,
            SmartHomeViewIpInput);
    }

    return handled;
}

static bool smart_home_valid_climate_entity(
    const char* entity_id) {

    if(!entity_id) {
        return false;
    }

    const char prefix[] = "climate.";

    if(strncmp(
           entity_id,
           prefix,
           sizeof(prefix) - 1) != 0) {
        return false;
    }

    const char* p =
        entity_id + sizeof(prefix) - 1;

    if(*p == '\0') {
        return false;
    }

    for(; *p; p++) {
        bool valid =
            (*p >= 'a' && *p <= 'z') ||
            (*p >= '0' && *p <= '9') ||
            (*p == '_');

        if(!valid) {
            return false;
        }
    }

    return true;
}


static void smart_home_ha_entity_entered(
    void* context) {

    SmartHomeApp* app = context;

    if(app->device_count >= MAX_DEVICES) {
        smart_home_show_result(
            app,
            "Smart Home",
            "Device list full");
        return;
    }

    if(!smart_home_valid_climate_entity(
           app->entity_input)) {

        smart_home_show_result(
            app,
            "Invalid Entity",
            "Use climate.name");

        return;
    }

    SmartHomeDevice* device =
        &app->devices[app->device_count];

    memset(
        device,
        0,
        sizeof(SmartHomeDevice));

    device->type =
        DeviceTypeHomeAssistantThermostat;

    strlcpy(
        device->name,
        app->pending_name,
        sizeof(device->name));

    device->ip[0] = '\0';

    strlcpy(
        device->remote_id,
        app->entity_input,
        sizeof(device->remote_id));

    app->device_count++;

    smart_home_save_devices(app);
    smart_home_refresh_main_menu(app);

    view_dispatcher_switch_to_view(
        app->view_dispatcher,
        SmartHomeViewMain);
}


static void smart_home_name_entered(void* context) {
    SmartHomeApp* app = context;

    if(strlen(app->input_buffer) == 0) {
        view_dispatcher_switch_to_view(
            app->view_dispatcher,
            SmartHomeViewMain);
        return;
    }

    strlcpy(
        app->pending_name,
        app->input_buffer,
        sizeof(app->pending_name));

    if(app->pending_device_type ==
       DeviceTypeHomeAssistantThermostat) {

        if(app->relay_url[0] == '\0' ||
           app->api_token[0] == '\0') {

            smart_home_show_result(
                app,
                "Remote Setup",
                "Configure Settings first");

            return;
        }

        memset(
            app->entity_input,
            0,
            sizeof(app->entity_input));

        strlcpy(
            app->entity_input,
            "climate.",
            sizeof(app->entity_input));

        app->input_mode =
            InputModeHaEntity;

        text_input_reset(app->text_input);

        text_input_set_header_text(
            app->text_input,
            "HA climate entity");

        text_input_set_result_callback(
            app->text_input,
            smart_home_ha_entity_entered,
            app,
            app->entity_input,
            DEVICE_REMOTE_ID_SIZE,
            false);

        view_dispatcher_switch_to_view(
            app->view_dispatcher,
            SmartHomeViewTextInput);

        return;
    }

    app->input_mode = InputModeIp;

    /*
     * Convenient editable starting address for IPv4 entry.
     * Every octet remains fully editable.
     */
    app->ip_bytes[0] = 192;
    app->ip_bytes[1] = 168;
    app->ip_bytes[2] = 7;
    app->ip_bytes[3] = 0;

    app->ip_selected_octet = 3;

    view_dispatcher_switch_to_view(
        app->view_dispatcher,
        SmartHomeViewIpInput);
}

static void smart_home_rename_entered(void* context) {
    SmartHomeApp* app = context;

    if(app->selected_device >= app->device_count) {
        view_dispatcher_switch_to_view(
            app->view_dispatcher,
            SmartHomeViewMain);
        return;
    }

    if(strlen(app->input_buffer) == 0) {
        view_dispatcher_switch_to_view(
            app->view_dispatcher,
            SmartHomeViewDevice);
        return;
    }

    strlcpy(
        app->devices[app->selected_device].name,
        app->input_buffer,
        sizeof(app->devices[app->selected_device].name));

    smart_home_save_devices(app);
    smart_home_refresh_main_menu(app);

    /*
     * Update the device-menu header too, so the new name is
     * visible immediately without leaving/reopening the app.
     */
    submenu_set_header(
        app->device_menu,
        app->devices[app->selected_device].name);

    smart_home_show_result(
        app,
        "Smart Home",
        "Device renamed");
}

static void smart_home_set_temperature_entered(void* context) {
    SmartHomeApp* app = context;

    if(app->selected_device >= app->device_count) {
        view_dispatcher_switch_to_view(
            app->view_dispatcher,
            SmartHomeViewMain);
        return;
    }

    SmartHomeDevice* device =
        &app->devices[app->selected_device];

    if(device->type !=
       DeviceTypeHomeAssistantThermostat) {
        return;
    }

    char* end = NULL;

    float temperature =
        strtof(app->input_buffer, &end);

    if(end == app->input_buffer ||
       *end != '\0' ||
       temperature < 50.0f ||
       temperature > 90.0f) {

        smart_home_show_result(
            app,
            "Invalid Temperature",
            "Enter 50 to 90 F");

        return;
    }

    if(app->relay_url[0] == '\0' ||
       app->api_token[0] == '\0') {

        smart_home_show_result(
            app,
            "Remote Setup",
            "Configure Settings first");

        return;
    }

    char url[URL_SIZE];
    char payload[96];

    snprintf(
        url,
        sizeof(url),
        "%s/ha/climate/%s/temperature",
        app->relay_url,
        device->remote_id);

    snprintf(
        payload,
        sizeof(payload),
        "{\"temperature\":%.1f}",
        (double)temperature);

    dialog_ex_set_header(
        app->dialog,
        device->name,
        64,
        12,
        AlignCenter,
        AlignCenter);

    dialog_ex_set_text(
        app->dialog,
        "Setting temperature...",
        64,
        34,
        AlignCenter,
        AlignCenter);

    dialog_ex_set_center_button_text(
        app->dialog,
        NULL);

    view_dispatcher_switch_to_view(
        app->view_dispatcher,
        SmartHomeViewResult);

    bool success =
        smart_home_send_post(
            app,
            url,
            payload);

    if(!success) {
        smart_home_show_result(
            app,
            "HTTP Error",
            app->fhttp->last_response &&
                    strlen(app->fhttp->last_response) ?
                app->fhttp->last_response :
                "No response");

        return;
    }

    char message[48];

    snprintf(
        message,
        sizeof(message),
        "Set to %.1f F",
        (double)temperature);

    smart_home_show_result(
        app,
        device->name,
        message);
}


static void smart_home_begin_set_temperature(
    SmartHomeApp* app) {

    if(app->selected_device >= app->device_count) {
        return;
    }

    memset(
        app->input_buffer,
        0,
        sizeof(app->input_buffer));

    /*
     * Convenient starting value. It remains fully editable.
     */
    strlcpy(
        app->input_buffer,
        "75",
        sizeof(app->input_buffer));

    app->input_mode =
        InputModeSetTemperature;

    text_input_reset(app->text_input);

    text_input_set_header_text(
        app->text_input,
        "Set temperature F");

    text_input_set_result_callback(
        app->text_input,
        smart_home_set_temperature_entered,
        app,
        app->input_buffer,
        DEVICE_NAME_SIZE,
        false);

    view_dispatcher_switch_to_view(
        app->view_dispatcher,
        SmartHomeViewTextInput);
}


static void smart_home_begin_rename_device(SmartHomeApp* app) {
    if(app->selected_device >= app->device_count) {
        return;
    }

    strlcpy(
        app->input_buffer,
        app->devices[app->selected_device].name,
        sizeof(app->input_buffer));

    app->input_mode = InputModeRename;

    text_input_reset(app->text_input);

    text_input_set_header_text(
        app->text_input,
        "Rename device");

    text_input_set_result_callback(
        app->text_input,
        smart_home_rename_entered,
        app,
        app->input_buffer,
        DEVICE_NAME_SIZE,
        false);

    view_dispatcher_switch_to_view(
        app->view_dispatcher,
        SmartHomeViewTextInput);
}

static void smart_home_begin_device_details(SmartHomeApp* app);

static void smart_home_device_type_callback(
    void* context,
    uint32_t index) {

    SmartHomeApp* app = context;

    switch(index) {
    case DeviceTypeShelly:
    case DeviceTypeHomeAssistantThermostat:
        app->pending_device_type = (DeviceType)index;
        smart_home_begin_device_details(app);
        break;

    default:
        break;
    }
}

static void smart_home_begin_add_device(SmartHomeApp* app) {
    if(app->device_count >= MAX_DEVICES) {
        smart_home_show_result(
            app,
            "Smart Home",
            "Device list full");
        return;
    }

    submenu_reset(app->device_type_menu);
    submenu_set_header(
        app->device_type_menu,
        "Select Device Type");

    submenu_add_item(
        app->device_type_menu,
        "Shelly Smart Device",
        DeviceTypeShelly,
        smart_home_device_type_callback,
        app);

    submenu_add_item(
        app->device_type_menu,
        "Home Assistant Thermostat",
        DeviceTypeHomeAssistantThermostat,
        smart_home_device_type_callback,
        app);

    view_dispatcher_switch_to_view(
        app->view_dispatcher,
        SmartHomeViewDeviceType);
}

static void smart_home_begin_device_details(SmartHomeApp* app) {
    if(app->device_count >= MAX_DEVICES) {
        smart_home_show_result(
            app,
            "Smart Home",
            "Device list full");
        return;
    }

    memset(app->input_buffer, 0, sizeof(app->input_buffer));
    memset(app->pending_name, 0, sizeof(app->pending_name));

    app->input_mode = InputModeName;

    text_input_reset(app->text_input);
    text_input_set_header_text(
        app->text_input,
        "Device name");

    text_input_set_result_callback(
        app->text_input,
        smart_home_name_entered,
        app,
        app->input_buffer,
        DEVICE_NAME_SIZE,
        false);

    view_dispatcher_switch_to_view(
        app->view_dispatcher,
        SmartHomeViewTextInput);
}

/* ---------- Settings UI ---------- */

#define SETTINGS_INDEX 1001
#define SETTINGS_RELAY_URL_INDEX 2001
#define SETTINGS_API_TOKEN_INDEX 2002

static uint32_t smart_home_settings_back(void* context) {
    UNUSED(context);
    return SmartHomeViewMain;
}

static void smart_home_relay_url_entered(void* context) {
    SmartHomeApp* app = context;

    if(strlen(app->settings_input) > 0) {
        strlcpy(
            app->relay_url,
            app->settings_input,
            sizeof(app->relay_url));

        smart_home_save_settings(app);
    }

    view_dispatcher_switch_to_view(
        app->view_dispatcher,
        SmartHomeViewSettings);
}

static void smart_home_api_token_entered(void* context) {
    SmartHomeApp* app = context;

    if(strlen(app->settings_input) > 0) {
        strlcpy(
            app->api_token,
            app->settings_input,
            sizeof(app->api_token));

        smart_home_save_settings(app);
    }

    view_dispatcher_switch_to_view(
        app->view_dispatcher,
        SmartHomeViewSettings);
}

static void smart_home_settings_callback(
    void* context,
    uint32_t index) {

    SmartHomeApp* app = context;

    memset(
        app->settings_input,
        0,
        sizeof(app->settings_input));

    text_input_reset(app->text_input);

    if(index == SETTINGS_RELAY_URL_INDEX) {
        app->input_mode = InputModeRelayUrl;

        strlcpy(
            app->settings_input,
            app->relay_url,
            sizeof(app->settings_input));

        text_input_set_header_text(
            app->text_input,
            "Relay URL");

        text_input_set_result_callback(
            app->text_input,
            smart_home_relay_url_entered,
            app,
            app->settings_input,
            RELAY_URL_SIZE,
            false);

    } else if(index == SETTINGS_API_TOKEN_INDEX) {
        app->input_mode = InputModeApiToken;

        strlcpy(
            app->settings_input,
            app->api_token,
            sizeof(app->settings_input));

        text_input_set_header_text(
            app->text_input,
            "API Token");

        text_input_set_result_callback(
            app->text_input,
            smart_home_api_token_entered,
            app,
            app->settings_input,
            API_TOKEN_SIZE,
            false);

    } else {
        return;
    }

    view_dispatcher_switch_to_view(
        app->view_dispatcher,
        SmartHomeViewTextInput);
}

static void smart_home_open_settings(SmartHomeApp* app) {
    submenu_reset(app->settings_menu);

    submenu_set_header(
        app->settings_menu,
        "Settings");

    submenu_add_item(
        app->settings_menu,
        "Relay URL",
        SETTINGS_RELAY_URL_INDEX,
        smart_home_settings_callback,
        app);

    submenu_add_item(
        app->settings_menu,
        "API Token",
        SETTINGS_API_TOKEN_INDEX,
        smart_home_settings_callback,
        app);

    view_dispatcher_switch_to_view(
        app->view_dispatcher,
        SmartHomeViewSettings);
}

/* ---------- Menu callbacks ---------- */

#define ADD_DEVICE_INDEX 1000

static void smart_home_device_action_callback(
    void* context,
    uint32_t index) {

    SmartHomeApp* app = context;

    if(app->selected_device >= app->device_count) {
        return;
    }

    SmartHomeDevice* device =
        &app->devices[app->selected_device];

    if(index == DeviceActionRename) {
        smart_home_begin_rename_device(app);
        return;
    }

    if(index == DeviceActionThermostatSetTemperature) {
        smart_home_begin_set_temperature(app);
        return;
    }

    if(index == DeviceActionDelete) {
        for(size_t i = app->selected_device;
            i + 1 < app->device_count;
            i++) {

            app->devices[i] = app->devices[i + 1];
        }

        if(app->device_count > 0) {
            app->device_count--;
        }

        smart_home_save_devices(app);
        smart_home_refresh_main_menu(app);

        view_dispatcher_switch_to_view(
            app->view_dispatcher,
            SmartHomeViewMain);

        return;
    }

    if(app->relay_url[0] == '\0' ||
       app->api_token[0] == '\0') {

        smart_home_show_result(
            app,
            "Remote Setup",
            "Configure Settings first");

        return;
    }

    char url[URL_SIZE];
    char payload[96];

    url[0] = '\0';
    strlcpy(payload, "{}", sizeof(payload));

    bool use_post = false;

    if(device->type == DeviceTypeShelly) {

        if(device->remote_id[0] == '\0') {
            smart_home_show_result(
                app,
                "Remote Error",
                "Device not registered");
            return;
        }

        const char* remote_device =
            device->remote_id;

        switch(index) {
        case DeviceActionOn:
            snprintf(
                url,
                sizeof(url),
                "%s/device/%s/on",
                app->relay_url,
                remote_device);
            break;

        case DeviceActionOff:
            snprintf(
                url,
                sizeof(url),
                "%s/device/%s/off",
                app->relay_url,
                remote_device);
            break;

        case DeviceActionToggle:
            snprintf(
                url,
                sizeof(url),
                "%s/device/%s/toggle",
                app->relay_url,
                remote_device);
            break;

        default:
            return;
        }

    } else if(
        device->type ==
        DeviceTypeHomeAssistantThermostat) {

        switch(index) {
        case DeviceActionThermostatStatus:
            snprintf(
                url,
                sizeof(url),
                "%s/ha/climate/%s/status",
                app->relay_url,
                device->remote_id);
            break;

        case DeviceActionThermostatTempUp:
            snprintf(
                url,
                sizeof(url),
                "%s/ha/climate/%s/temp_up",
                app->relay_url,
                device->remote_id);
            use_post = true;
            break;

        case DeviceActionThermostatTempDown:
            snprintf(
                url,
                sizeof(url),
                "%s/ha/climate/%s/temp_down",
                app->relay_url,
                device->remote_id);
            use_post = true;
            break;

        case DeviceActionThermostatCool:
            snprintf(
                url,
                sizeof(url),
                "%s/ha/climate/%s/mode",
                app->relay_url,
                device->remote_id);
            strlcpy(
                payload,
                "{\"mode\":\"cool\"}",
                sizeof(payload));
            use_post = true;
            break;

        case DeviceActionThermostatHeat:
            snprintf(
                url,
                sizeof(url),
                "%s/ha/climate/%s/mode",
                app->relay_url,
                device->remote_id);
            strlcpy(
                payload,
                "{\"mode\":\"heat\"}",
                sizeof(payload));
            use_post = true;
            break;

        case DeviceActionThermostatAuto:
            snprintf(
                url,
                sizeof(url),
                "%s/ha/climate/%s/mode",
                app->relay_url,
                device->remote_id);
            strlcpy(
                payload,
                "{\"mode\":\"heat_cool\"}",
                sizeof(payload));
            use_post = true;
            break;

        case DeviceActionThermostatOff:
            snprintf(
                url,
                sizeof(url),
                "%s/ha/climate/%s/mode",
                app->relay_url,
                device->remote_id);
            strlcpy(
                payload,
                "{\"mode\":\"off\"}",
                sizeof(payload));
            use_post = true;
            break;

        case DeviceActionThermostatFanAuto:
            snprintf(
                url,
                sizeof(url),
                "%s/ha/climate/%s/fan",
                app->relay_url,
                device->remote_id);
            strlcpy(
                payload,
                "{\"fan_mode\":\"auto\"}",
                sizeof(payload));
            use_post = true;
            break;

        case DeviceActionThermostatFanOn:
            snprintf(
                url,
                sizeof(url),
                "%s/ha/climate/%s/fan",
                app->relay_url,
                device->remote_id);
            strlcpy(
                payload,
                "{\"fan_mode\":\"on\"}",
                sizeof(payload));
            use_post = true;
            break;

        default:
            return;
        }

    } else {
        return;
    }

    dialog_ex_set_header(
        app->dialog,
        device->name,
        64,
        12,
        AlignCenter,
        AlignCenter);

    dialog_ex_set_text(
        app->dialog,
        "Sending...",
        64,
        34,
        AlignCenter,
        AlignCenter);

    dialog_ex_set_center_button_text(
        app->dialog,
        NULL);

    view_dispatcher_switch_to_view(
        app->view_dispatcher,
        SmartHomeViewResult);

    bool success;

    if(use_post) {
        success =
            smart_home_send_post(
                app,
                url,
                payload);
    } else {
        success =
            smart_home_send(
                app,
                url);
    }

    if(!success) {
        FURI_LOG_E(
            TAG,
            "Request failed: %s",
            app->fhttp->last_response ?
                app->fhttp->last_response :
                "No response");

        smart_home_show_result(
            app,
            "HTTP Error",
            app->fhttp->last_response &&
                    strlen(app->fhttp->last_response) ?
                app->fhttp->last_response :
                "No response");

        return;
    }

    if(device->type == DeviceTypeShelly) {
        if(index == DeviceActionOn) {
            smart_home_show_result(
                app,
                device->name,
                "Turned ON");
        } else if(index == DeviceActionOff) {
            smart_home_show_result(
                app,
                device->name,
                "Turned OFF");
        } else {
            smart_home_show_result(
                app,
                device->name,
                "Toggled");
        }

        return;
    }

    if(index == DeviceActionThermostatStatus) {
        char current[16] = "?";
        char target[16] = "?";
        char state[20] = "?";
        char fan[20] = "?";
        char humidity[16] = "?";

        const char* response =
            app->fhttp->last_response;

        if(response) {
            smart_home_extract_json_scalar(
                response,
                "current_temperature",
                current,
                sizeof(current));

            smart_home_extract_json_scalar(
                response,
                "temperature",
                target,
                sizeof(target));

            smart_home_extract_json_scalar(
                response,
                "state",
                state,
                sizeof(state));

            smart_home_extract_json_scalar(
                response,
                "fan_mode",
                fan,
                sizeof(fan));

            smart_home_extract_json_scalar(
                response,
                "current_humidity",
                humidity,
                sizeof(humidity));
        }

        char message[128];

        snprintf(
            message,
            sizeof(message),
            "Now %sF  Set %sF\n%s / Fan %s\nHumidity %s%%",
            current,
            target,
            state,
            fan,
            humidity);

        smart_home_show_result(
            app,
            device->name,
            message);

        return;
    }

    const char* message = "Command sent";

    switch(index) {
    case DeviceActionThermostatTempUp:
        message = "Temperature +1";
        break;

    case DeviceActionThermostatTempDown:
        message = "Temperature -1";
        break;

    case DeviceActionThermostatCool:
        message = "Mode: Cool";
        break;

    case DeviceActionThermostatHeat:
        message = "Mode: Heat";
        break;

    case DeviceActionThermostatAuto:
        message = "Mode: Auto";
        break;

    case DeviceActionThermostatOff:
        message = "HVAC Off";
        break;

    case DeviceActionThermostatFanAuto:
        message = "Fan: Auto";
        break;

    case DeviceActionThermostatFanOn:
        message = "Fan: On";
        break;

    default:
        break;
    }

    smart_home_show_result(
        app,
        device->name,
        message);
}


static void smart_home_main_callback(
    void* context,
    uint32_t index) {

    SmartHomeApp* app = context;

    if(index == ADD_DEVICE_INDEX) {
        smart_home_begin_add_device(app);
        return;
    }

    if(index == SETTINGS_INDEX) {
        smart_home_open_settings(app);
        return;
    }

    if(index < app->device_count) {
        smart_home_open_device(app, index);

        submenu_reset(app->device_menu);
        submenu_set_header(
            app->device_menu,
            app->devices[index].name);

        SmartHomeDevice* device =
            &app->devices[index];

        if(device->type == DeviceTypeShelly) {

            submenu_add_item(
                app->device_menu,
                "Turn ON",
                DeviceActionOn,
                smart_home_device_action_callback,
                app);

            submenu_add_item(
                app->device_menu,
                "Turn OFF",
                DeviceActionOff,
                smart_home_device_action_callback,
                app);

            submenu_add_item(
                app->device_menu,
                "Toggle",
                DeviceActionToggle,
                smart_home_device_action_callback,
                app);

        } else if(
            device->type ==
            DeviceTypeHomeAssistantThermostat) {

            submenu_add_item(
                app->device_menu,
                "Status",
                DeviceActionThermostatStatus,
                smart_home_device_action_callback,
                app);

            submenu_add_item(
                app->device_menu,
                "Temp +1",
                DeviceActionThermostatTempUp,
                smart_home_device_action_callback,
                app);

            submenu_add_item(
                app->device_menu,
                "Temp -1",
                DeviceActionThermostatTempDown,
                smart_home_device_action_callback,
                app);

            submenu_add_item(
                app->device_menu,
                "Set Temperature",
                DeviceActionThermostatSetTemperature,
                smart_home_device_action_callback,
                app);

            submenu_add_item(
                app->device_menu,
                "Cool",
                DeviceActionThermostatCool,
                smart_home_device_action_callback,
                app);

            submenu_add_item(
                app->device_menu,
                "Heat",
                DeviceActionThermostatHeat,
                smart_home_device_action_callback,
                app);

            submenu_add_item(
                app->device_menu,
                "Auto",
                DeviceActionThermostatAuto,
                smart_home_device_action_callback,
                app);

            submenu_add_item(
                app->device_menu,
                "Off",
                DeviceActionThermostatOff,
                smart_home_device_action_callback,
                app);

            submenu_add_item(
                app->device_menu,
                "Fan Auto",
                DeviceActionThermostatFanAuto,
                smart_home_device_action_callback,
                app);

            submenu_add_item(
                app->device_menu,
                "Fan On",
                DeviceActionThermostatFanOn,
                smart_home_device_action_callback,
                app);
        }

        submenu_add_item(
            app->device_menu,
            "Rename Device",
            DeviceActionRename,
            smart_home_device_action_callback,
            app);

        submenu_add_item(
            app->device_menu,
            "Delete Device",
            DeviceActionDelete,
            smart_home_device_action_callback,
            app);

        view_dispatcher_switch_to_view(
            app->view_dispatcher,
            SmartHomeViewDevice);
    }
}

static void smart_home_refresh_main_menu(SmartHomeApp* app) {
    submenu_reset(app->main_menu);
    submenu_set_header(app->main_menu, "Smart Home");

    for(size_t i = 0; i < app->device_count; i++) {
        submenu_add_item(
            app->main_menu,
            app->devices[i].name,
            (uint32_t)i,
            smart_home_main_callback,
            app);
    }

    submenu_add_item(
        app->main_menu,
        "+ Add Device",
        ADD_DEVICE_INDEX,
        smart_home_main_callback,
        app);

    submenu_add_item(
        app->main_menu,
        "Settings",
        SETTINGS_INDEX,
        smart_home_main_callback,
        app);
}

static void smart_home_dialog_callback(
    DialogExResult result,
    void* context) {

    SmartHomeApp* app = context;

    if(result == DialogExResultCenter) {
        view_dispatcher_switch_to_view(
            app->view_dispatcher,
            SmartHomeViewDevice);
    }
}

/* ---------- Application ---------- */

int32_t smart_home_app(void* p) {
    UNUSED(p);

    SmartHomeApp* app = malloc(sizeof(SmartHomeApp));

    if(!app) {
        return -1;
    }

    memset(app, 0, sizeof(SmartHomeApp));

    app->fhttp = flipper_http_alloc();

    if(!app->fhttp) {
        free(app);
        return -1;
    }

    if(!smart_home_wait_for_board(app->fhttp)) {
        flipper_http_free(app->fhttp);
        free(app);
        return -1;
    }

    furi_delay_ms(500);

    if(!flipper_http_send_command(
           app->fhttp,
           HTTP_CMD_WIFI_CONNECT)) {

        flipper_http_free(app->fhttp);
        free(app);
        return -1;
    }

    furi_delay_ms(2000);

    smart_home_load_devices(app);
    smart_home_load_settings(app);

    app->gui = furi_record_open(RECORD_GUI);

    app->view_dispatcher = view_dispatcher_alloc();
    app->main_menu = submenu_alloc();
    app->device_menu = submenu_alloc();
    app->device_type_menu = submenu_alloc();
    app->settings_menu = submenu_alloc();
    app->dialog = dialog_ex_alloc();
    app->text_input = text_input_alloc();

    app->ip_input_view = view_alloc();

    view_allocate_model(
        app->ip_input_view,
        ViewModelTypeLockFree,
        sizeof(SmartHomeIpViewModel));

    SmartHomeIpViewModel* ip_model =
        view_get_model(app->ip_input_view);

    ip_model->app = app;

    view_set_context(app->ip_input_view, app);
    view_set_draw_callback(
        app->ip_input_view,
        smart_home_ip_draw_callback);
    view_set_input_callback(
        app->ip_input_view,
        smart_home_ip_input_callback);

    view_dispatcher_attach_to_gui(
        app->view_dispatcher,
        app->gui,
        ViewDispatcherTypeFullscreen);

    smart_home_refresh_main_menu(app);

    dialog_ex_set_context(app->dialog, app);
    dialog_ex_set_result_callback(
        app->dialog,
        smart_home_dialog_callback);

    view_set_previous_callback(
        submenu_get_view(app->main_menu),
        smart_home_exit_callback);

    view_set_previous_callback(
        submenu_get_view(app->device_menu),
        smart_home_device_back);

    view_set_previous_callback(
        submenu_get_view(app->device_type_menu),
        smart_home_input_back);

    view_set_previous_callback(
        submenu_get_view(app->settings_menu),
        smart_home_settings_back);

    view_set_previous_callback(
        dialog_ex_get_view(app->dialog),
        smart_home_result_back);

    view_set_previous_callback(
        text_input_get_view(app->text_input),
        smart_home_input_back);

    view_set_previous_callback(
        app->ip_input_view,
        smart_home_input_back);

    view_dispatcher_add_view(
        app->view_dispatcher,
        SmartHomeViewMain,
        submenu_get_view(app->main_menu));

    view_dispatcher_add_view(
        app->view_dispatcher,
        SmartHomeViewDevice,
        submenu_get_view(app->device_menu));

    view_dispatcher_add_view(
        app->view_dispatcher,
        SmartHomeViewDeviceType,
        submenu_get_view(app->device_type_menu));

    view_dispatcher_add_view(
        app->view_dispatcher,
        SmartHomeViewSettings,
        submenu_get_view(app->settings_menu));

    view_dispatcher_add_view(
        app->view_dispatcher,
        SmartHomeViewResult,
        dialog_ex_get_view(app->dialog));

    view_dispatcher_add_view(
        app->view_dispatcher,
        SmartHomeViewTextInput,
        text_input_get_view(app->text_input));

    view_dispatcher_add_view(
        app->view_dispatcher,
        SmartHomeViewIpInput,
        app->ip_input_view);

    view_dispatcher_switch_to_view(
        app->view_dispatcher,
        SmartHomeViewMain);

    view_dispatcher_run(app->view_dispatcher);

    view_dispatcher_remove_view(
        app->view_dispatcher,
        SmartHomeViewMain);

    view_dispatcher_remove_view(
        app->view_dispatcher,
        SmartHomeViewDevice);

    view_dispatcher_remove_view(
        app->view_dispatcher,
        SmartHomeViewResult);

    view_dispatcher_remove_view(
        app->view_dispatcher,
        SmartHomeViewTextInput);

    view_dispatcher_remove_view(
        app->view_dispatcher,
        SmartHomeViewIpInput);

    view_free(app->ip_input_view);
    text_input_free(app->text_input);
    dialog_ex_free(app->dialog);
    submenu_free(app->device_menu);

    view_dispatcher_remove_view(
        app->view_dispatcher,
        SmartHomeViewDeviceType);

    view_dispatcher_remove_view(
        app->view_dispatcher,
        SmartHomeViewSettings);

    submenu_free(app->device_type_menu);
    submenu_free(app->settings_menu);
    submenu_free(app->main_menu);
    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_GUI);

    flipper_http_free(app->fhttp);
    free(app);

    return 0;
}
