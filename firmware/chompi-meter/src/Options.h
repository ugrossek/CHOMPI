#pragma once
// CIRCLES's settings on the card: /CIRCLES/options.json, in TAPE's format
// (a list of name and value). Read once at power-on; written back when it is
// missing or differs from what was read (a new setting appears there with its
// default, an unknown value goes back to the default). The folder is made when
// it is not there, so CIRCLES never writes to the card's root.
//
//   Align To L0  true (default): a new length or a figure from the library
//                comes in on the next L0 beat, the player back on its place
//                there, in step with L0. false: at the player's own loop
//                start, wherever the drift has taken it (nothing jumps).
//
// Read by hand, not with a JSON library: a name, then the value after it.

#include "daisy_seed.h"

#include <cstring>

namespace meter {

struct Options {
    bool align = true;

    void Load() {
        if (f_chdir("/CIRCLES") != FR_OK) {
            f_mkdir("/CIRCLES");
            if (f_chdir("/CIRCLES") != FR_OK) return; // no card, or it will not take a folder
        }
        alignas(32) static char was[kSize];
        size_t len = 0;
        if (f_open(&f_, kName, FA_READ) == FR_OK) {
            UINT br = 0;
            f_read(&f_, was, kSize - 1, &br);
            f_close(&f_);
            len = br;
        }
        was[len] = '\0';
        Bool(was, "Align To L0", align);

        alignas(32) static char now[kSize];
        std::strcpy(now, "{\n\t\"chompi\": [\n\t\t{\n\t\t\t\"name\": \"Align To L0\",\n\t\t\t\"value\": ");
        std::strcat(now, align ? "true" : "false");
        std::strcat(now, "\n\t\t}\n\t]\n}");
        if (std::strcmp(now, was) == 0) return;
        if (f_open(&f_, kName, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK) {
            UINT bw = 0;
            f_write(&f_, now, std::strlen(now), &bw);
            f_close(&f_);
        }
    }

private:
    // The value after "name": "<name>": true or false; anything else leaves v.
    static void Bool(const char* s, const char* name, bool& v) {
        char q[48] = "\"";
        std::strcat(q, name);
        std::strcat(q, "\"");
        const char* p = std::strstr(s, q);
        if (!p) return;
        p = std::strstr(p + std::strlen(q), "\"value\"");
        if (!p) return;
        p += 7;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ':') p++;
        if (std::strncmp(p, "true", 4) == 0) v = true;
        else if (std::strncmp(p, "false", 5) == 0) v = false;
    }

    static constexpr const char* kName = "options.json";
    static constexpr size_t kSize = 1024;
    FIL f_;
};

} // namespace meter
