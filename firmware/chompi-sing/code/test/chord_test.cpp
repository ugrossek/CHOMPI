/* Host test for ChordTarget.h: absolute chords.
 *
 *   g++ -std=c++14 -O2 -I../src chord_test.cpp -o chord_test && ./chord_test
 */
#include <cstdio>
#include "ChordTarget.h"

using namespace chompi;

static int failures = 0;
static void Check(bool ok, const char *what)
{
    if (!ok)
    {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

/** semitones each key of a chord is shifted, for a sung note */
static void Chord(const char *name, const int *keys, int n, int sung, bool nearest,
                  const int *expect_targets)
{
    printf("%-34s sung %3d:", name, sung);
    for (int i = 0; i < n; i++)
    {
        const int t = ChordTarget(keys[i], sung, nearest);
        printf("  key %d -> %d (%+d)", keys[i], t, t - sung);
        Check(t == expect_targets[i], name);
        Check(!nearest || (t - sung >= -5 && t - sung <= 6), "nearest stays within -5..+6");
        Check(((t - keys[i]) % 12 + 12) % 12 == 0, "target keeps the key's pitch class");
    }
    printf("\n");
}

int main()
{
    /* C major (C4 E4 G4 = 60 64 67) */
    const int cmaj[] = {60, 64, 67};

    /* singing E4: C a third below, E in unison, G a third above */
    { const int e[] = {60, 64, 67}; Chord("C major, sing E4, nearest", cmaj, 3, 64, true, e); }
    /* singing E3, an octave lower: the chord follows into that octave */
    { const int e[] = {48, 52, 55}; Chord("C major, sing E3, nearest", cmaj, 3, 52, true, e); }
    /* singing A4: C up a third, E down a fourth, G down a second */
    { const int e[] = {72, 64, 67}; Chord("C major, sing A4, nearest", cmaj, 3, 69, true, e); }
    /* the same keys from the lower octave give the same notes with nearest */
    { const int low[] = {48, 52, 55}, e[] = {60, 64, 67};
      Chord("C major low keys, sing E4, nearest", low, 3, 64, true, e); }
    /* pressed-key octave: the keys' own notes */
    { const int e[] = {60, 64, 67}; Chord("C major, sing E3, key octave", cmaj, 3, 52, false, e); }
    /* tritone: F# above C counts as +6, not -6 */
    { const int k[] = {66}, e[] = {66}; Chord("F#, sing C4, nearest", k, 1, 60, true, e); }
    printf("\n");

    /* the reference with hysteresis */
    SungReference r;
    Check(r.note == 60, "starts at middle C");
    Check(!r.Update(false, 64.f) && r.note == 60, "unvoiced doesn't move it");
    Check(r.Update(true, 64.1f) && r.note == 64, "a clear new note moves it");
    Check(!r.Update(true, 64.6f) && r.note == 64, "vibrato within 0.7 keeps it (64.6)");
    Check(!r.Update(true, 63.35f) && r.note == 64, "vibrato within 0.7 keeps it (63.35)");
    Check(r.Update(true, 64.75f) && r.note == 65, "past 0.7 it moves (64.75 -> 65)");
    Check(!r.Update(true, 64.4f) && r.note == 65, "and doesn't flip back at 64.4");
    Check(r.Update(true, 64.2f) && r.note == 64, "but does at 64.2");
    Check(!r.Update(false, 40.f) && r.note == 64, "an unvoiced gap keeps the last note");

    /* a slow slide C4 -> E4 steps through the semitones, once each */
    {
        SungReference s;
        s.Update(true, 60.f);
        int changes = 0;
        for (float x = 60.f; x <= 64.f; x += .01f)
            changes += s.Update(true, x);
        printf("slide C4 -> E4: %d reference changes, ends on %d\n", changes, s.note);
        Check(changes == 4 && s.note == 64, "a slide steps once per semitone");
    }

    printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "all checks passed",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
