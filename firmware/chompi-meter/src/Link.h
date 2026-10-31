#pragma once
// The link: a page on a computer (the tutorial, later a display) follows the
// instrument over USB-MIDI and can guide it. SysEx both ways, see PROTOCOL.md.
//
// It only talks after the page has said hello, and stops 3 s after the last
// hello, so a CHOMPI without a page sends nothing. Everything runs in the main
// loop: sending over USB waits, which the audio callback must never do.
//
// A send can get lost (the USB stack drops a message while the last one is
// still on its way), so the state goes out again now and then, whether it
// changed or not: the page always catches up.

#include "Meter.h"
#include "board/hardware.h"

#include <cstring>

#include "usbd_cdc.h"
extern USBD_HandleTypeDef hUsbDeviceHS; // libDaisy's USB device (the CHOMPI's USB-MIDI)

namespace meter {

class Link {
public:
    static constexpr uint8_t kVersion = 1;

    void Poll(chompi::Hardware& hw, Instrument& in, uint32_t now) {
        // USB that has got stuck comes back by itself. A receive the USB stack
        // switched off (an overflow) starts again; a send that never finishes
        // (the cable was pulled while one was on its way, and the USB core never
        // heard of it) restarts the USB device, as TAPE does when a send fails:
        // the computer then finds the CHOMPI anew.
        hw.usb_midi.Listen();
        const auto* cdc = static_cast<const USBD_CDC_HandleTypeDef*>(hUsbDeviceHS.pClassData);
        if (hw.usb_midi_active && cdc && cdc->TxState != 0) {
            if (!stuck_t_) stuck_t_ = now | 1;
            else if (now - stuck_t_ > kStuckMs && now - reset_t_ > kResetMs) {
                hw.ResetUSBMidi();
                hw.usb_midi.Listen();
                resets_++;
                reset_t_ = now;
                stuck_t_ = 0;
                active_ = false;
            }
        } else {
            stuck_t_ = 0;
        }

        while (hw.usb_midi.HasEvents()) {
            daisy::MidiEvent e = hw.usb_midi.PopEvent();
            if (e.type == daisy::SystemCommon && e.sc_type == daisy::SystemExclusive)
                Receive(in, e.sysex_data, e.sysex_message_len, now);
        }

        if (!active_ || !hw.usb_midi_active) return;
        if (now - hello_ > kQuietMs) { active_ = false; in.SetHint(0); return; }
        if (now - sent_ < kSendMs) return;
        sent_ = now;

        // What happened since the last send, then the state where it changed
        // (or hasn't gone out for a while), then a few channels.
        uint8_t e[3];
        while (n_ < kOut - 64 && in.PopEvent(e)) Add(0x44, e, 3);

        uint8_t b[Instrument::kDescribeMax];
        int len = in.Describe(b);
        // (The last three bytes, the load, change all the time: they ride along.)
        if (len != state_len_ || std::memcmp(b, state_, len - 3) || now - state_t_ > kAgainMs) {
            Add(0x42, b, len);
            std::memcpy(state_, b, len);
            state_len_ = len;
            state_t_ = now;
        }

        if (now - clock_t_ >= kClockMs) {
            clock_t_ = now;
            // The clock, then how often USB was restarted (for the page to show).
            const int n = in.DescribeClock(b);
            b[n] = uint8_t(resets_ & 127);
            Add(0x45, b, n + 1);
        }

        int sent = 0;
        for (int i = 0; i < kChannels && sent < kChannelsPerSend; ++i) {
            const int c = (next_ch_ + i) % kChannels;
            len = in.DescribeChannel(c, b);
            const bool due = now - ch_t_[c] > kAgainMs * 4;
            if (!due && len == ch_len_[c] && !std::memcmp(b, ch_[c], len)) continue;
            if (n_ + len + 8 > kOut) break;
            Add(0x43, b, len);
            std::memcpy(ch_[c], b, len);
            ch_len_[c] = len;
            ch_t_[c] = now;
            sent++;
            next_ch_ = (c + 1) % kChannels;
        }

        if (n_) hw.usb_midi.SendMessage(out_, n_);
        n_ = 0;
    }

private:
    static constexpr uint32_t kQuietMs = 3000; // no hello for so long: the page is gone
    static constexpr uint32_t kStuckMs = 1500; // a send still on its way after so long: stuck (a busy
                                               // computer can take a few hundred ms to read)
    static constexpr uint32_t kResetMs = 2000; // at most one USB restart so often
    static constexpr uint32_t kSendMs = 10;    // a message bundle at most so often
    static constexpr uint32_t kAgainMs = 500;  // the state at least so often
    static constexpr int kChannelsPerSend = 3;
    static constexpr uint32_t kClockMs = 100;  // where everyone is, so often
    static constexpr int kOut = 600;           // a bundle; USB-MIDI fits 768 bytes in one go

    void Receive(Instrument& in, const uint8_t* d, int n, uint32_t now) {
        if (n < 4 || d[0] != 0x7D || d[1] != 0x4D || d[2] != 0x54) return; // 7D "MT"
        const uint8_t cmd = d[3];
        if (cmd == 0x01) { // hello: talk (again), everything from the start
            if (!active_) {
                state_len_ = 0;
                std::fill(ch_len_, ch_len_ + kChannels, 0);
            }
            active_ = true;
            hello_ = now;
            const uint8_t r[] = {kVersion, uint8_t(kChannels), uint8_t(kNumMalletPresets)};
            Add(0x41, r, sizeof r);
        } else if (cmd == 0x02 && n >= 9) { // the controls to light, 5 x 7 bits
            uint64_t m = 0;
            for (int i = 0; i < 5; ++i) m |= uint64_t(d[4 + i] & 127) << (7 * i);
            in.SetHint(m);
        } else if (cmd >= 0x03 && cmd <= 0x05) { // for the instrument, at its next block
            for (int wait = 0; !in.Post(d + 3, n - 3) && wait < 20; ++wait) daisy::System::Delay(1);
        }
    }

    void Add(uint8_t cmd, const uint8_t* p, int len) {
        if (n_ + len + 6 > kOut) return;
        out_[n_++] = 0xF0;
        out_[n_++] = 0x7D;
        out_[n_++] = 0x4D;
        out_[n_++] = 0x54;
        out_[n_++] = cmd;
        for (int i = 0; i < len; ++i) out_[n_++] = p[i] & 127;
        out_[n_++] = 0xF7;
    }

    bool active_ = false;
    uint32_t stuck_t_ = 0, reset_t_ = 0, resets_ = 0;
    uint32_t hello_ = 0, sent_ = 0, state_t_ = 0, clock_t_ = 0;
    uint8_t out_[kOut];
    int n_ = 0;
    uint8_t state_[Instrument::kDescribeMax];
    int state_len_ = 0;
    uint8_t ch_[kChannels][Instrument::kDescribeMax];
    int ch_len_[kChannels] = {};
    uint32_t ch_t_[kChannels] = {};
    int next_ch_ = 0;
};

} // namespace meter
