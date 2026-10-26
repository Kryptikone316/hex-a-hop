/* See joykey.hpp. Default bit table + optional keymap.txt override, same as
 * every other port on this device. */
#include "joykey.hpp"

#include <stdio.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#define KEYMAP_FILE "/mnt/sdcard/frogui/keymap.txt"

static const char *key_names[JK_COUNT] = {
    "LEFT", "RIGHT", "UP", "DOWN", "A", "B", "L1", "R1", "X", "Y", "START", "SELECT", "L2", "R2"
};
/* L2=8, R2=9 confirmed empirically on real hardware (2026-10-17, hex-a-hop
 * port) via joykey_read_raw() while alternating L2/R2 presses - FrogUI's
 * own default_bits[] table (frogui_fork/input.c) marks both -1/unmapped,
 * so this device's actual L2/R2 bits aren't documented anywhere upstream. */
static int key_bits[JK_COUNT] = { 7, 5, 2, 3, 13, 14, 10, 11, 12, 15, 1, 0, 8, 9 };

static volatile unsigned int *cv_keys = NULL;

static void load_keymap(void) {
    FILE *f = fopen(KEYMAP_FILE, "r");
    char line[64], name[32];
    int bit, i;
    if (!f) return;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "%31[^=]=%d", name, &bit) != 2) continue;
        for (i = 0; i < JK_COUNT; i++)
            if (strcmp(name, key_names[i]) == 0 && bit >= 0 && bit < 32) key_bits[i] = bit;
    }
    fclose(f);
}

void joykey_init(void) {
    key_t key;
    int id;
    void *mem;

    load_keymap();

    key = ftok("/tmp/joy_key", 'a');
    if (key == (key_t)-1) return;
    id = shmget(key, 4, 0666);
    if (id < 0) return;
    mem = shmat(id, NULL, 0);
    if (mem != (void *)-1) cv_keys = (volatile unsigned int *)mem;
}

unsigned int joykey_read(void) {
    unsigned int raw, out;
    int i;

    if (!cv_keys) return 0;

    raw = *cv_keys & 0xFFFFu;
    out = 0;
    for (i = 0; i < JK_COUNT; i++) {
        if (raw & (1u << key_bits[i])) out |= 1u << i;
    }
    return out;
}

/* Full, UNTRANSLATED hardware bitmask - joykey_read() only ever reports
 * bits present in key_bits[], so a button with no logical slot (e.g. L2/R2
 * on devices where they're not in the default table) is silently invisible
 * through joykey_read() alone. No mask applied (joykey_read()'s 0xFFFF cap
 * could itself hide a bit >15) - use this to discover an undocumented
 * button's real bit on real hardware, then add it to key_bits[]/key_names[]
 * properly once known. */
unsigned int joykey_read_raw(void) {
    if (!cv_keys) return 0;
    return *cv_keys;
}
