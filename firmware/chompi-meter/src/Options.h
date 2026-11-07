#pragma once
// CIRCLES's settings on the card: /CIRCLES/options.json, in TAPE's format
// (a list of name and value). Read once at power-on; written back when it is
// missing or differs from what was read (a new setting appears there with its
// default, an unknown value goes back to the default), and when another style
// is chosen at power-on. The folder is made when
// it is not there, so CIRCLES never writes to the card's root.
//
//   Align To L0  true (default): a new length or a figure from the library
//                comes in on the next L0 beat, the player back on its place
//                there, in step with L0. false: at the player's own loop
//                start, wherever the drift has taken it (nothing jumps).
//   Style        "Mallets" or "Acid house": the one chosen last at power-on
//                (CIRCLES asks every time; this one is the brighter key).
//
// Read by hand, not with a JSON library: a name, then the value after it.

#include "daisy_seed.h"

#include <cstring>

namespace meter {

struct Options {
    bool align = true;
    int style = 0; // 0 mallets, 1 acid house
    bool card = false; // the folder could be opened: Save can write

    void Load() {
        if (f_chdir("/CIRCLES") != FR_OK) {
            f_mkdir("/CIRCLES");
            if (f_chdir("/CIRCLES") != FR_OK) return; // no card, or it will not take a folder
        }
        card = true;
        size_t len = 0;
        if (f_open(&f_, kName, FA_READ) == FR_OK) {
            UINT br = 0;
            f_read(&f_, was_, kSize - 1, &br);
            f_close(&f_);
            len = br;
        }
        was_[len] = '\0';
        Bool(was_, "Align To L0", align);
        const char* v = Value(was_, "Style");
        if (v && std::strncmp(v, "\"Acid house\"", 12) == 0) style = 1;
        else if (v && std::strncmp(v, "\"Mallets\"", 9) == 0) style = 0;
        Save();
    }

    // Written when it differs from what is on the card (the main loop, after a
    // style is chosen; the folder is still the current one).
    void Save() {
        if (!card) return;
        alignas(32) static char now[kSize];
        std::strcpy(now, "{\n\t\"chompi\": [\n\t\t{\n\t\t\t\"name\": \"Align To L0\",\n\t\t\t\"value\": ");
        std::strcat(now, align ? "true" : "false");
        std::strcat(now, "\n\t\t},\n\t\t{\n\t\t\t\"name\": \"Style\",\n\t\t\t\"value\": ");
        std::strcat(now, style == 1 ? "\"Acid house\"" : "\"Mallets\"");
        std::strcat(now, "\n\t\t}\n\t]\n}");
        if (std::strcmp(now, was_) == 0) return;
        if (f_open(&f_, kName, FA_CREATE_ALWAYS | FA_WRITE) == FR_OK) {
            UINT bw = 0;
            f_write(&f_, now, std::strlen(now), &bw);
            f_close(&f_);
            std::strcpy(was_, now);
        }
    }

private:
    // Where the value after "name": "<name>" starts, or null.
    static const char* Value(const char* s, const char* name) {
        char q[48] = "\"";
        std::strcat(q, name);
        std::strcat(q, "\"");
        const char* p = std::strstr(s, q);
        if (!p) return nullptr;
        p = std::strstr(p + std::strlen(q), "\"value\"");
        if (!p) return nullptr;
        p += 7;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ':') p++;
        return p;
    }
    // true or false; anything else leaves v.
    static void Bool(const char* s, const char* name, bool& v) {
        const char* p = Value(s, name);
        if (!p) return;
        if (std::strncmp(p, "true", 4) == 0) v = true;
        else if (std::strncmp(p, "false", 5) == 0) v = false;
    }

    static constexpr const char* kName = "options.json";
    static constexpr size_t kSize = 1024;
    FIL f_;
    alignas(32) char was_[kSize] = {}; // what the card has
};

} // namespace meter
