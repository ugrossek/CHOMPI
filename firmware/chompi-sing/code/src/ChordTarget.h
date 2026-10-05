#pragma once
#include <cmath>

namespace chompi
{
    /** The sung note as the reference for absolute chords: the detected
     *  pitch rounded to a semitone. It only moves once the voice is clearly
     *  on another note (more than kHysteresis semitones away), so it doesn't
     *  flicker between two neighbours, and it keeps the last note through
     *  unvoiced gaps. Middle C until anything is sung, which makes absolute
     *  chords behave like relative ones until then. */
    struct SungReference
    {
        static constexpr float kHysteresis = .7f;
        int note = 60;

        /** true if the reference changed */
        bool Update(bool voiced, float sung)
        {
            if (!voiced || fabsf(sung - note) <= kHysteresis)
                return false;
            const int n = int(lroundf(sung));
            if (n == note)
                return false;
            note = n;
            return true;
        }
    };

    /** The note a held key asks for in absolute mode (MIDI numbers).
     *  nearest: the key's pitch class in the octave closest to the sung
     *  reference (-5..+6 semitones from it); otherwise the key's own note. */
    inline int ChordTarget(int key_note, int ref, bool nearest)
    {
        if (!nearest)
            return key_note;
        int d = ((key_note - ref) % 12 + 12) % 12; // 0..11 above ref
        if (d > 6)
            d -= 12;
        return ref + d;
    }

} // namespace chompi
