#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "fatfs.h"

namespace chompi
{
    /** names in settings.txt, [page][knob] for knobs 1-3 */
    static const char *const kSingKnobNames[2][3] = {
        {"transpose", "spread", "doubler"},
        {"volume", "attack", "release"},
    };

    /** SING's settings that survive a power cycle: knobs 1-3 on both pages
     *  and the monitor mode. Kept in settings.txt next to the firmware's
     *  other files (/SING), one "name value" pair per line; knob values are
     *  0..1000 (nano printf has no floats). Missing or broken lines keep the
     *  default, so an old or hand-edited file can't break the start. */
    struct SingSettings
    {
        float knob[2][3];
        int   monitor; // MonitorMode as int: 0 headphones, 1 all, 3 off

        void SetDefaults(const float defaults[][6])
        {
            for (int p = 0; p < 2; p++)
                for (int k = 0; k < 3; k++)
                    knob[p][k] = defaults[p][k];
            monitor = 0;
        }

        bool Same(const SingSettings &o) const
        {
            for (int p = 0; p < 2; p++)
                for (int k = 0; k < 3; k++)
                    if (knob[p][k] != o.knob[p][k])
                        return false;
            return monitor == o.monitor;
        }

        /** reads settings.txt over the current values; false if there is none */
        bool Load()
        {
            FIL f;
            if (f_open(&f, kFile, FA_READ) != FR_OK)
                return false;
            char buf[kMaxLen + 1];
            UINT br = 0;
            f_read(&f, buf, kMaxLen, &br);
            f_close(&f);
            buf[br] = '\0';

            char *line = buf;
            while (*line)
            {
                char *eol = strchr(line, '\n');
                if (eol)
                    *eol = '\0';
                Parse(line);
                line = eol ? eol + 1 : line + strlen(line);
            }
            return true;
        }

        void Save() const
        {
            char buf[kMaxLen + 1];
            int  len = snprintf(buf, sizeof(buf),
                                "# SING settings, written by the firmware.\n"
                                "# knobs: 0 = fully left .. 1000 = fully right\n"
                                "# monitor: 0 headphones, 1 all outputs, 3 off\n");
            for (int p = 0; p < 2; p++)
                for (int k = 0; k < 3; k++)
                    len += snprintf(buf + len, sizeof(buf) - len, "%s %d\n",
                                    kSingKnobNames[p][k], int(knob[p][k] * 1000.f + .5f));
            len += snprintf(buf + len, sizeof(buf) - len, "monitor %d\n", monitor);

            FIL f;
            if (f_open(&f, kFile, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
                return;
            UINT bw = 0;
            f_write(&f, buf, len, &bw);
            f_close(&f);
        }

      private:
        static constexpr const char *kFile   = "settings.txt";
        static constexpr size_t      kMaxLen = 511;

        void Parse(const char *line)
        {
            for (int p = 0; p < 2; p++)
                for (int k = 0; k < 3; k++)
                {
                    const size_t n = strlen(kSingKnobNames[p][k]);
                    int v;
                    if (strncmp(line, kSingKnobNames[p][k], n) == 0 && line[n] == ' '
                        && Number(line + n + 1, v) && v >= 0 && v <= 1000)
                        knob[p][k] = v / 1000.f;
                }
            int m;
            if (strncmp(line, "monitor ", 8) == 0 && Number(line + 8, m)
                && (m == 0 || m == 1 || m == 3)) // 2 is TAPE's send/return
                monitor = m;
        }

        /** a whole number, nothing but spaces or \r after it */
        static bool Number(const char *s, int &v)
        {
            char *end;
            const long l = strtol(s, &end, 10);
            if (end == s)
                return false;
            while (*end == ' ' || *end == '\r')
                end++;
            v = int(l);
            return *end == '\0';
        }
    };

} // namespace chompi
