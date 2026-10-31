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
extern USBD_HandleTypeDef hUsbDeviceHS;    // libDaisy's USB device (the CHOMPI's USB-MIDI)
extern "C" void CDC_Set_Rx_Callback_HS(void (*cb)(uint8_t* buf, uint32_t* size));

namespace meter {

// What the page sends, taken straight from the USB packets (in the USB
// interrupt) instead of through libDaisy's MIDI receive: its buffer, parser
// and event queue could stop passing anything on, for good (seen on the
// hardware: data came in, no message came out). CIRCLES only needs SysEx: a
// message is the bytes between F0 and F7; a new F0 or another status byte
// starts over, so a lost byte costs one message, not the rest. Finished
// messages wait in a small queue with one writer (here) and one reader (the
// main loop). The packets are counted for the K6 view.
struct RxMessage { uint8_t d[128]; int n; };
constexpr int kRxQueue = 8;
inline RxMessage g_rx_q[kRxQueue];
inline volatile int g_rx_head = 0, g_rx_tail = 0;
inline uint8_t g_rx_cur[128];
inline int g_rx_n = -1; // -1: not inside a message
inline volatile uint32_t g_usb_rx = 0;

inline void RxByte(uint8_t b) {
    if (b == 0xF0) { g_rx_n = 0; return; }
    if (g_rx_n < 0) return;
    if (b == 0xF7) {
        const int next = (g_rx_head + 1) % kRxQueue;
        if (next != g_rx_tail) { // (full: this one is lost; the page sends again)
            RxMessage& m = g_rx_q[g_rx_head];
            std::memcpy(m.d, g_rx_cur, g_rx_n);
            m.n = g_rx_n;
            __sync_synchronize();
            g_rx_head = next;
        }
        g_rx_n = -1;
        return;
    }
    if (b & 0x80) { g_rx_n = -1; return; } // another status byte: that message is broken
    if (g_rx_n < int(sizeof g_rx_cur)) g_rx_cur[g_rx_n++] = b;
    else g_rx_n = -1; // too long for anything CIRCLES knows
}

inline void UsbRx(uint8_t* buffer, uint32_t* length) {
    g_usb_rx = g_usb_rx + 1;
    // USB-MIDI: 4-byte packets, the low half of the first byte says what
    // follows (4: SysEx goes on, 3 bytes; 5, 6, 7: it ends with 1, 2, 3; F: one byte).
    for (uint32_t i = 0; i + 4 <= *length; i += 4) {
        const uint8_t cin = buffer[i] & 0x0F;
        const int n = cin == 0x4 || cin == 0x7 ? 3 : cin == 0x6 ? 2 : cin == 0x5 || cin == 0xF ? 1 : 0;
        for (int k = 0; k < n; ++k) RxByte(buffer[i + 1 + k]);
    }
}

class Link {
public:
    static constexpr uint8_t kVersion = 1;

    uint32_t lent = 0; // how often the charger had the USB lines (meter_main)

    void Poll(chompi::Hardware& hw, Instrument& in, uint32_t now) {
        // (libDaisy's receive is not used, see UsbRx; listening costs nothing.) A
        // send nobody reads just waits: an earlier version unplugged the CHOMPI
        // then, and that fired when a page said its first hello, which some
        // computers took badly.)
        hw.usb_midi.Listen();
        if (!counting_) { CDC_Set_Rx_Callback_HS(UsbRx); counting_ = true; }
        {
            // The K6 view of the link.
            if (g_usb_rx != rx_seen_) { rx_seen_ = g_usb_rx; rx_t_ = now; }
            const auto* c = static_cast<const USBD_CDC_HandleTypeDef*>(hUsbDeviceHS.pClassData);
            if (c && c->TxState != 0) { if (!busy_t_) busy_t_ = now | 1; } else busy_t_ = 0;
            const uint8_t st = hUsbDeviceHS.dev_state;
            in.SetUsbView({hello_ && now - hello_ < kQuietMs, rx_t_ && now - rx_t_ < 200,
                           busy_t_ && now - busy_t_ > 500, g_rx_tail != g_rx_head,
                           st == USBD_STATE_CONFIGURED ? 2 : st == USBD_STATE_SUSPENDED ? 1 : 0});
        }

        while (g_rx_tail != g_rx_head) {
            const RxMessage& m = g_rx_q[g_rx_tail];
            Receive(in, m.d, m.n, now);
            __sync_synchronize();
            g_rx_tail = (g_rx_tail + 1) % kRxQueue;
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
            // The clock, then how often USB was restarted, how often the
            // charger had the lines and how often the keys were misread (for
            // the page to show).
            const int n = in.DescribeClock(b);
            b[n] = uint8_t(resets_ & 127);
            b[n + 1] = uint8_t(lent & 127);
            b[n + 2] = uint8_t(std::min(in.Misreads(), uint32_t(127)));
            Add(0x45, b, n + 3);
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
    uint32_t resets_ = 0; // (no USB restarts any more: always 0 for the page)
    bool counting_ = false;
    uint32_t rx_seen_ = 0, rx_t_ = 0, busy_t_ = 0;
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
