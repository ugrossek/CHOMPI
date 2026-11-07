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

#include "daisy_seed.h"
#include "core_json.h"

#include <cstdio>
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
        if (len && JSON_Validate(was, len) == JSONSuccess) Parse(was, len);

        alignas(32) static char now[kSize];
        snprintf(now, kSize,
                 "{\n\t\"chompi\": [\n\t\t{\n\t\t\t\"name\": \"Align To L0\",\n\t\t\t\"value\": %s\n\t\t}\n\t]\n}",
                 align ? "true" : "false");
        if (std::strcmp(now, was) == 0) return;
        if (f_open(&f_, kName, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK) {
            UINT bw = 0;
            f_write(&f_, now, std::strlen(now), &bw);
            f_close(&f_);
        }
    }

private:
    void Parse(char* s, size_t len) {
        for (int i = 0; i < kMost; ++i) {
            char q[32];
            char* v;
            size_t n;
            snprintf(q, sizeof q, "chompi[%d].name", i);
            if (JSON_Search(s, len, q, std::strlen(q), &v, &n) != JSONSuccess) break;
            const bool is_align = n == 11 && std::strncmp(v, "Align To L0", n) == 0;
            snprintf(q, sizeof q, "chompi[%d].value", i);
            if (JSON_Search(s, len, q, std::strlen(q), &v, &n) != JSONSuccess) continue;
            if (is_align) {
                if (n == 4 && std::strncmp(v, "true", n) == 0) align = true;
                if (n == 5 && std::strncmp(v, "false", n) == 0) align = false;
            }
        }
    }

    static constexpr const char* kName = "options.json";
    static constexpr size_t kSize = 1024;
    static constexpr int kMost = 16;
    FIL f_;
};

} // namespace meter
