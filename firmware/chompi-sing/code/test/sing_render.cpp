/* Renders a recording through SING on the PC, to listen to: the Harmonizer
 * (Human or Robot, a C major chord held from the start) and the glitter
 * cloud, optionally frozen halfway. Writes one stereo wav per setting.
 *
 *   L=../../../chompi-tape/code/libs/DaisySP/Source
 *   g++ -std=c++14 -O2 -I../src -I$L sing_render.cpp \
 *       $L/Utility/dcblock.cpp $L/Filters/svf.cpp -o sing_render
 *   ./sing_render voice.wav outdir
 *
 * The input is a 16-bit wav at 48 kHz (mono, or stereo mixed down). It is
 * set to the level the sims use (rms .05); each output is normalised to
 * -1 dBFS peak, so compare sound, not loudness. */
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include "Harmonizer.h"
#include "PitchDetector.h"
#include "Cloud.h"

static const int kPoly = 7;
float chompi::shift_mem[kPoly][chompi::kShiftBufFrames * 2];
int16_t chompi::shift_ana[kPoly][chompi::kShiftAnaLen];
float chompi::chorus_mem[2][chompi::kChorusLen];
float chompi::vocoder_hist[chompi::Vocoder::kHistFrames][chompi::Vocoder::kBands];
float chompi::voice_audio[chompi::VoiceFreeze::kLen];
float chompi::voice_pitch[chompi::VoiceFreeze::kFrames];
float chompi::cloud_mem[chompi::kCloudLen];

using H = chompi::Harmonizer<kPoly>;
static const float kSr = 48000.f;
static const int   kBlock = 48;

static bool ReadWav(const char *path, std::vector<float> &out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    char id[4];
    uint32_t size, rate = 0;
    uint16_t fmt = 0, ch = 0, bits = 0;
    fseek(f, 12, SEEK_SET);
    while (fread(id, 1, 4, f) == 4 && fread(&size, 4, 1, f) == 1)
    {
        if (!memcmp(id, "fmt ", 4))
        {
            fread(&fmt, 2, 1, f); fread(&ch, 2, 1, f); fread(&rate, 4, 1, f);
            fseek(f, 6, SEEK_CUR); fread(&bits, 2, 1, f);
            fseek(f, size - 16, SEEK_CUR);
        }
        else if (!memcmp(id, "data", 4))
        {
            if (fmt != 1 || bits != 16 || rate != 48000 || ch < 1)
            {
                fprintf(stderr, "need 16-bit PCM at 48 kHz (got fmt %d, %d bit, %u Hz)\n", fmt, bits, rate);
                fclose(f);
                return false;
            }
            std::vector<int16_t> s(size / 2);
            fread(s.data(), 2, s.size(), f);
            for (size_t i = 0; i + ch <= s.size(); i += ch)
            {
                float v = 0.f;
                for (int c = 0; c < ch; c++) v += s[i + c];
                out.push_back(v / ch / 32768.f);
            }
            fclose(f);
            return true;
        }
        else
            fseek(f, size + (size & 1), SEEK_CUR);
    }
    fclose(f);
    return false;
}

static void WriteWav(const std::string &path, const std::vector<float> &l, const std::vector<float> &r)
{
    float peak = 1e-9f;
    for (size_t i = 0; i < l.size(); i++) peak = fmaxf(peak, fmaxf(fabsf(l[i]), fabsf(r[i])));
    const float g = .89f / peak; // -1 dBFS
    FILE *f = fopen(path.c_str(), "wb");
    const uint32_t n = uint32_t(l.size()), data = n * 4;
    auto w32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
    auto w16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f); w32(36 + data); fwrite("WAVEfmt ", 1, 8, f);
    w32(16); w16(1); w16(2); w32(48000); w32(48000 * 4); w16(4); w16(16);
    fwrite("data", 1, 4, f); w32(data);
    for (uint32_t i = 0; i < n; i++)
    {
        w16(uint16_t(int16_t(lrintf(fmaxf(-1.f, fminf(1.f, l[i] * g)) * 32767.f))));
        w16(uint16_t(int16_t(lrintf(fmaxf(-1.f, fminf(1.f, r[i] * g)) * 32767.f))));
    }
    fclose(f);
}

enum class Src { Dry, Human, Robot };

struct Setting
{
    const char *name;
    Src   src;
    float cloud, glitter;
    bool  freeze; // halfway through
};

static void Render(const std::vector<float> &voice, const Setting &s, const std::string &dir)
{
    static H h;
    static chompi::PitchDetector pd;
    static chompi::Cloud cloud;
    h.Init(kSr);
    pd.Init();
    cloud.Init(kSr, chompi::cloud_mem);
    cloud.SetAmount(s.cloud);
    cloud.SetGlitter(s.glitter);
    h.SetMode(s.src == Src::Robot ? H::ChordMode::Robot : H::ChordMode::Keys);
    h.NoteOn(1, 0.f); h.NoteOn(2, 4.f); h.NoteOn(3, 7.f); // C E G, around middle C

    /* the voice, then 2 s more for the tails (and the frozen sound) */
    const size_t n = (voice.size() + size_t(2.f * kSr)) / kBlock * kBlock;
    std::vector<float> L(n), R(n);
    for (size_t b = 0; b < n; b += kBlock)
    {
        float in[kBlock], l[kBlock] = {}, r[kBlock] = {};
        for (int i = 0; i < kBlock; i++)
            in[i] = b + i < voice.size() ? voice[b + i] : 0.f;
        if (s.freeze && b == voice.size() / 2 / kBlock * kBlock)
            h.SetFreeze(true);
        if (s.src == Src::Dry)
            for (int i = 0; i < kBlock; i++) l[i] = r[i] = in[i];
        else
        {
            pd.Process(in, kBlock);
            pd.Update();
            h.SetSung(pd.Voiced(), pd.Note());
            h.Process(in, false, l, r, kBlock);
            for (int i = 0; i < kBlock; i++) { l[i] = tanhf(l[i]); r[i] = tanhf(r[i]); } // the engine's SoftClip
        }
        cloud.Process(l, r, kBlock);
        memcpy(&L[b], l, sizeof l);
        memcpy(&R[b], r, sizeof r);
    }
    const std::string path = dir + "/" + s.name + ".wav";
    WriteWav(path, L, R);
    printf("    %s\n", path.c_str());
}

int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "usage: sing_render voice.wav outdir\n"); return 1; }
    std::vector<float> voice;
    if (!ReadWav(argv[1], voice)) { fprintf(stderr, "can't read %s\n", argv[1]); return 1; }
    double e = 0.;
    for (float v : voice) e += v * v;
    const float rms = sqrtf(float(e / voice.size()));
    for (float &v : voice) v *= .05f / rms;
    printf("%s: %.2f s\n", argv[1], voice.size() / kSr);

    static const Setting kSettings[] = {
        {"01-dry",                  Src::Dry,   0.f,  .5f, false},
        {"02-dry-cloud-plain",      Src::Dry,   .6f,  0.f, false},
        {"03-dry-cloud-sparkle",    Src::Dry,   .6f,  1.f, false},
        {"04-human-chord",          Src::Human, 0.f,  .5f, false},
        {"05-human-cloud-half",     Src::Human, .5f,  .5f, false},
        {"06-human-cloud-sparkle",  Src::Human, 1.f,  1.f, false},
        {"07-robot-chord",          Src::Robot, 0.f,  .5f, false},
        {"08-robot-cloud-sparkle",  Src::Robot, .7f,  1.f, false},
        {"09-human-freeze-cloud",   Src::Human, .8f,  .8f, true},
    };
    for (const Setting &s : kSettings)
        Render(voice, s, argv[2]);
}
