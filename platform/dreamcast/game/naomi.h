// SEGA NAOMI / NAOMI 2 (KallistiOS KOS_SUBARCH=naomi): what the arcade board
// does differently from the Dreamcast, for game/main.cpp.
//
//   - No CD drive: the ROM images, the sound pack and the default settings
//     are appended to the netboot image and read from the NetDIMM (/dimm,
//     naomi_dimmfs.c), like the Dreamcast reads its disc.
//   - Controls: the cabinet's JVS I/O board through the MIE. Tuned for a
//     Crazy Taxi upright: wheel, accelerator, brake, a two-way shifter that
//     springs back (forward = shift up, back = shift down) and START.
//         START                start
//         ACCELERATOR + START  the next view (VR1, VR2, VR3, VR4, VR1...)
//         TEST / SERVICE       the game's test and service buttons (on the
//                              service panel: test mode, settings)
//         coin chutes          coin
//   - Settings, bookkeeping and records: the game's EEPROM and backup RAM are
//     kept in the board's battery-backed SRAM (32 KB at 0x00200000, from 0x1000).
#pragma once

#include <dc/maple.h>
#include <dc/maple/controller.h>
#include <dc/maple/mie.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include "controls.h"
#include "naomi_dimmfs.h"

namespace naomi {

// Raw JVS values measured on the Crazy Taxi cabinet; the limits widen by
// themselves if a control goes further.
struct Calibration {
    int wheel_min = 8448, wheel_centre = 33792, wheel_max = 60160;
    int accel_min = 11776, accel_max = 47104;
    int brake_min = 14080, brake_max = 47360;
};

class Controls {
public:
    // The cabinet's inputs as a Dreamcast pad for dc::Controls (steering,
    // pedals, gears, start), plus the buttons a pad does not have.
    dc::Input sample() {
        dc::Pad pad;
        maple_device_t *dev = maple_enum_type(0, MAPLE_FUNC_MIE);
        const mie_state_t *st = dev ? static_cast<const mie_state_t *>(maple_dev_status(dev)) : nullptr;
        if (!st) return base_.sample(pad);
        const mie_jvs_inputs_t &j = st->jvs;

        pad.joyx = steer(j.wheel);
        pad.rtrig = pedal(j.accel, cal_.accel_min, cal_.accel_max);
        pad.ltrig = pedal(j.brake, cal_.brake_min, cal_.brake_max);
        const bool start = j.p1.start, accel = pad.rtrig > 0x80;
        if (j.p1.up) pad.buttons |= dc::Up;       // shifter forward: up a gear
        if (j.p1.down) pad.buttons |= dc::Down;   // back: down a gear
        const bool view = start && accel;
        if (start && !view) pad.buttons |= dc::Start;
        dc::Input in = base_.sample(pad);

        // ACCELERATOR + START: the next view button, held while START is
        if (view && !view_held_) view_ = (view_ + 1) % 4;
        view_held_ = view;
        if (view) {
            if (view_ < 3) in.in0 &= uint8_t(~(0x20 << view_)); // VR1-VR3
            else in.in1 &= uint8_t(~0x01);                       // VR4
        }
        if (j.panel.psw.test || j.system.test) in.in0 &= uint8_t(~0x04);
        if (j.panel.psw.service || j.p1.service) in.in0 &= uint8_t(~0x08);
        if (j.coin_pulse) coin_frames_ = 6;       // a coin: the switch, a few frames
        if (coin_frames_ > 0) { --coin_frames_; in.in0 &= uint8_t(~0x01); }
        return in;
    }

private:
    int steer(int raw) {
        cal_.wheel_min = std::min(cal_.wheel_min, raw);
        cal_.wheel_max = std::max(cal_.wheel_max, raw);
        if (raw < cal_.wheel_centre)
            return -((cal_.wheel_centre - raw) * 128) / (cal_.wheel_centre - cal_.wheel_min);
        return ((raw - cal_.wheel_centre) * 127) / (cal_.wheel_max - cal_.wheel_centre);
    }
    static int pedal(int raw, int &lo, int &hi) {
        lo = std::min(lo, raw);
        hi = std::max(hi, raw);
        const int margin = (hi - lo) * 6 / 100;   // rest jitter / full press reaching 255
        const int from = lo + margin, to = hi - margin;
        if (raw <= from) return 0;
        if (raw >= to) return 255;
        return (raw - from) * 255 / (to - from);
    }
    dc::Controls base_;
    Calibration cal_;
    int view_ = 0;
    bool view_held_ = false;
    int coin_frames_ = 0;
};

// The game's EEPROM (128 bytes) and backup RAM (16 KB) in the SRAM, from
// 0x1000 (the first 4 KB are left to Cannonball's settings, so both games
// keep theirs on the same board):
//   0x00 "DTSR", 0x04 u32 eeprom size, 0x08 u32 backup size, 0x0C u32 checksum,
//   0x10 eeprom, then backup RAM. Magic written last.
class Sram {
public:
    bool load(uint8_t *eeprom, size_t esize, uint8_t *backup, size_t bsize) {
        if (16 + esize + bsize > kSize) return false;
        uint8_t hdr[16];
        read(0, hdr, 16);
        if (std::memcmp(hdr, "DTSR", 4) != 0 || le32(hdr + 4) != esize || le32(hdr + 8) != bsize) return false;
        std::vector<uint8_t> data(esize + bsize);
        read(16, data.data(), data.size());
        if (le32(hdr + 12) != checksum(data.data(), data.size())) return false;
        std::memcpy(eeprom, data.data(), esize);
        std::memcpy(backup, data.data() + esize, bsize);
        last_.assign(data.begin(), data.end());
        return true;
    }
    // Writes only when something changed since the last load or save.
    bool save(const uint8_t *eeprom, size_t esize, const uint8_t *backup, size_t bsize) {
        if (16 + esize + bsize > kSize) return false;
        std::vector<uint8_t> data(esize + bsize);
        std::memcpy(data.data(), eeprom, esize);
        std::memcpy(data.data() + esize, backup, bsize);
        if (data == last_) return false;
        uint8_t hdr[16] = {'D', 'T', 'S', 'R'};
        put32(hdr + 4, uint32_t(esize));
        put32(hdr + 8, uint32_t(bsize));
        put32(hdr + 12, checksum(data.data(), data.size()));
        sram()[0] = 0; // no valid magic while writing
        for (size_t i = 0; i < data.size(); i++) sram()[16 + i] = data[i];
        for (size_t i = 15; i > 0; i--) sram()[i] = hdr[i];
        sram()[0] = hdr[0];
        last_.swap(data);
        return true;
    }

private:
    static constexpr size_t kSize = 0x7000;
    static volatile uint8_t *sram() { return reinterpret_cast<volatile uint8_t *>(0xA0201000); }
    static void read(size_t at, uint8_t *out, size_t n) { for (size_t i = 0; i < n; i++) out[i] = sram()[at + i]; }
    static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24; }
    static void put32(uint8_t *p, uint32_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); p[2] = uint8_t(v >> 16); p[3] = uint8_t(v >> 24); }
    static uint32_t checksum(const uint8_t *d, size_t n) {
        uint32_t s = 0x44545352; // "DTSR"
        for (size_t i = 0; i < n; i++) s = (s << 5) + s + d[i];
        return s;
    }
    std::vector<uint8_t> last_;
};

} // namespace naomi
