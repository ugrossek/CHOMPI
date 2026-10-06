#pragma once
#include "daisy.h"
#include "fatfs.h"
#include "core_json.h"

namespace chompi {

/** SING's options.json, in its folder on the card (/SING). Read once at
 *  power-on, then written back with every option, so a new card gets a
 *  complete file to edit. Never written while playing (that stalled the
 *  unit). Unknown, missing or broken entries keep their defaults; see
 *  OPTIONS.md. Same layout as TAPE's file:
 *    {"chompi": [{"name": "...", "value": ...}, ...]}
 */
class OptionsManager
{
  public:
    uint8_t midi_ch_in;     // 0..15
    uint8_t midi_ch_out;    // 0..15
    uint8_t monitor_position; // a MonitorMode: 0 headphones, 1 all outputs, 3 off
    bool    delay_split;    // knob 4: delay left, reverb right
    bool    latch_follows;  // latched chords follow the voice
    bool    voice_gate;     // harmonies only while there is sound
    bool    show_cpu;       // audio load on the white keys in the menu
    bool    freeze_dump;    // diagnosis: write the voice to the card on freeze

    void Init()
    {
        midi_ch_in       = 0;
        midi_ch_out      = 0;
        monitor_position = 0;
        delay_split      = true;  // SING: space is reverb left, echo right
        latch_follows    = true;
        voice_gate       = false;
        show_cpu         = false;
        freeze_dump      = false;

        const char fname[] = "options.json";
        const FRESULT res  = f_stat(fname, nullptr);

        // create if not there, don't overwrite
        f_open(&fptr_opt, fname, (FA_OPEN_ALWAYS | FA_WRITE | FA_READ));

        UINT br = 0;
        f_read(&fptr_opt, &opt_file[0], kOptFileSize - 1, &br);
        opt_file[br] = '\0';

        if(res == FR_OK)
            Parse();

        WriteFile();
    }

  private:
    static const size_t kOptFileSize = 4096;
    static const size_t kMaxEntries  = 32; // read at most this many

    FIL  fptr_opt;
    char opt_file[kOptFileSize];

    /** append one entry; first: no comma before it */
    void Entry(char *&p, const char *name, const char *value, bool first = false)
    {
        p += sprintf(p, "%s\n\t\t{\n\t\t\t\"name\": \"%s\",\n\t\t\t\"value\": %s\n\t\t}",
                     first ? "" : ",", name, value);
    }

    void WriteFile()
    {
        char  num[8];
        char *p = opt_file;
        p += sprintf(p, "{\n\t\"chompi\": [");
        sprintf(num, "%d", midi_ch_in + 1);
        Entry(p, "Midi In Channel", num, true);
        sprintf(num, "%d", midi_ch_out + 1);
        Entry(p, "Midi Out Channel", num);
        /* in the file: 1 headphones, 2 all outputs, 3 off */
        sprintf(num, "%d", monitor_position == 3 ? 3 : monitor_position + 1);
        Entry(p, "Monitor Position", num);
        Entry(p, "Split Delay", delay_split ? "true" : "false");
        Entry(p, "Latch Follows Voice", latch_follows ? "true" : "false");
        Entry(p, "Voice Gate", voice_gate ? "true" : "false");
        Entry(p, "Show CPU", show_cpu ? "true" : "false");
        Entry(p, "Freeze Dump", freeze_dump ? "true" : "false");
        p += sprintf(p, "\n\t]\n}\n");

        UINT bw = 0;
        f_lseek(&fptr_opt, 0);
        f_write(&fptr_opt, opt_file, p - opt_file, &bw);
        f_truncate(&fptr_opt);
        f_sync(&fptr_opt);
    }

    /** the value of entry i as a 0-terminated string in buf, or false */
    bool Value(size_t len, size_t i, const char *field, char *buf, size_t buf_len)
    {
        char   query[40];
        char  *value;
        size_t value_len;
        sprintf(query, "chompi[%d].%s", int(i), field);
        if(JSON_Search(opt_file, len, query, strlen(query), &value, &value_len) != JSONSuccess
           || value_len >= buf_len)
            return false;
        memcpy(buf, value, value_len);
        buf[value_len] = '\0';
        return true;
    }

    void Parse()
    {
        const size_t len = strlen(opt_file);
        if(JSON_Validate(opt_file, len) != JSONSuccess)
            return;

        for(size_t i = 0; i < kMaxEntries; i++)
        {
            char name[40], value[16];
            if(!Value(len, i, "name", name, sizeof(name)))
                break; // no more entries
            if(!Value(len, i, "value", value, sizeof(value)))
                continue;

            const bool t = strcmp(value, "true") == 0;
            const bool f = strcmp(value, "false") == 0;
            const int  n = atoi(value);

            if(strcmp(name, "Midi In Channel") == 0 && n >= 1 && n <= 16)
                midi_ch_in = n - 1;
            else if(strcmp(name, "Midi Out Channel") == 0 && n >= 1 && n <= 16)
                midi_ch_out = n - 1;
            else if(strcmp(name, "Monitor Position") == 0 && n >= 1 && n <= 3)
                monitor_position = n == 3 ? 3 : n - 1;
            else if(!t && !f)
                continue; // the rest are true / false
            else if(strcmp(name, "Split Delay") == 0)
                delay_split = t;
            else if(strcmp(name, "Latch Follows Voice") == 0)
                latch_follows = t;
            else if(strcmp(name, "Voice Gate") == 0)
                voice_gate = t;
            else if(strcmp(name, "Show CPU") == 0)
                show_cpu = t;
            else if(strcmp(name, "Freeze Dump") == 0)
                freeze_dump = t;
            // anything else (TAPE's looper options in an old file): ignored
        }
    }
};
} // namespace chompi
