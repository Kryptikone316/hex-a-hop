/* See tfdriver.hpp. */
#include "tfdriver.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TFDEVICE_ENV   "/tmp/tfdevice.env"
#define DEFAULT_DRIVER "/mnt/sdcard/cubegm/driver_sf3500.so"

const char *tfenv_get(const char *key) {
    static char val[256];
    const char *e = getenv(key);
    size_t n = strlen(key);
    FILE *f;
    char line[320];

    if (e && *e) {
        snprintf(val, sizeof(val), "%s", e);
        return val;
    }

    f = fopen(TFDEVICE_ENV, "r");
    if (!f) return NULL;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, key, n) != 0 || line[n] != '=') continue;
        line[strcspn(line, "\r\n")] = '\0';
        fclose(f);
        if (!line[n + 1]) return NULL;
        snprintf(val, sizeof(val), "%s", line + n + 1);
        return val;
    }
    fclose(f);
    return NULL;
}

const char *tfdriver_path(void) {
    static char path[256];
    static int resolved = 0;
    const char *d;

    if (!resolved) {
        resolved = 1;
        d = tfenv_get("TF_DRIVER");
        snprintf(path, sizeof(path), "%s", d ? d : DEFAULT_DRIVER);
    }
    return path;
}
