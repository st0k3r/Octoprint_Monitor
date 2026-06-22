#include "config.h"
#include <storage/storage.h>
#include <string.h>

static void parse_line(OctoPrintApp* app, const char* line) {
    if(strncmp(line, CONFIG_IP_KEY, strlen(CONFIG_IP_KEY)) == 0) {
        strncpy(app->ip, line + strlen(CONFIG_IP_KEY), CONFIG_MAX_LEN - 1);
    } else if(strncmp(line, CONFIG_APIKEY_KEY, strlen(CONFIG_APIKEY_KEY)) == 0) {
        strncpy(app->apikey, line + strlen(CONFIG_APIKEY_KEY), CONFIG_MAX_LEN - 1);
    }
}

bool config_load(OctoPrintApp* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File*    file    = storage_file_alloc(storage);

    memset(app->ip,     0, CONFIG_MAX_LEN);
    memset(app->apikey, 0, CONFIG_MAX_LEN);

    if(!storage_file_open(file, CONFIG_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        storage_file_free(file);
        furi_record_close(RECORD_STORAGE);
        return false;
    }

    char    line[CONFIG_MAX_LEN * 2];
    size_t  pos = 0;
    uint8_t byte;

    while(storage_file_read(file, &byte, 1) == 1) {
        if(byte == '\n' || byte == '\r') {
            if(pos > 0) {
                line[pos] = '\0';
                parse_line(app, line);
                pos = 0;
            }
        } else if(pos < sizeof(line) - 1) {
            line[pos++] = (char)byte;
        }
    }
    if(pos > 0) {
        line[pos] = '\0';
        parse_line(app, line);
    }

    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);

    return app->ip[0] != '\0';
}

bool config_save(OctoPrintApp* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);

    /* Ensure directory exists */
    storage_simply_mkdir(storage, CONFIG_DIR);

    File* file = storage_file_alloc(storage);
    if(!storage_file_open(file, CONFIG_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        storage_file_free(file);
        furi_record_close(RECORD_STORAGE);
        return false;
    }

    char buf[CONFIG_MAX_LEN * 2];
    int  len;

    len = snprintf(buf, sizeof(buf), "ip=%s\n", app->ip);
    storage_file_write(file, buf, (uint16_t)len);

    len = snprintf(buf, sizeof(buf), "apikey=%s\n", app->apikey);
    storage_file_write(file, buf, (uint16_t)len);

    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
    return true;
}
