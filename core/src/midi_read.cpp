#include "flowstate/midi_read.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace flowstate {

const char* toString(MidiReadErrorCode code) {
    switch (code) {
        case MidiReadErrorCode::Empty: return "empty";
        case MidiReadErrorCode::TooLarge: return "too_large";
        case MidiReadErrorCode::Malformed: return "malformed";
        case MidiReadErrorCode::UnsupportedTiming: return "unsupported_timing";
        case MidiReadErrorCode::TooManyTracks: return "too_many_tracks";
        case MidiReadErrorCode::TooManyNotes: return "too_many_notes";
        case MidiReadErrorCode::NoNotes: return "no_notes";
    }
    return "malformed";
}

namespace {

struct Fail {
    MidiReadErrorCode code;
    std::string message;
};

class Reader {
public:
    Reader(const std::vector<std::uint8_t>& d, std::size_t begin, std::size_t end) : d_(d), pos_(begin), end_(end) {}

    bool done() const { return pos_ >= end_; }
    std::size_t pos() const { return pos_; }

    std::uint8_t byte() {
        if (pos_ >= end_) throw Fail{MidiReadErrorCode::Malformed, "unexpected end of track data"};
        return d_[pos_++];
    }
    std::uint8_t peek() const {
        if (pos_ >= end_) throw Fail{MidiReadErrorCode::Malformed, "unexpected end of track data"};
        return d_[pos_];
    }
    std::uint32_t varLen() {
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            std::uint8_t b = byte();
            v = (v << 7) | (b & 0x7F);
            if (!(b & 0x80)) return v;
        }
        throw Fail{MidiReadErrorCode::Malformed, "variable-length number longer than 4 bytes"};
    }
    void skip(std::size_t n) {
        if (n > end_ - pos_) throw Fail{MidiReadErrorCode::Malformed, "event runs past the end of its track"};
        pos_ += n;
    }
    std::vector<std::uint8_t> take(std::size_t n) {
        if (n > end_ - pos_) throw Fail{MidiReadErrorCode::Malformed, "event runs past the end of its track"};
        std::vector<std::uint8_t> out(d_.begin() + static_cast<std::ptrdiff_t>(pos_),
                                      d_.begin() + static_cast<std::ptrdiff_t>(pos_ + n));
        pos_ += n;
        return out;
    }

private:
    const std::vector<std::uint8_t>& d_;
    std::size_t pos_;
    std::size_t end_;
};

std::uint32_t be(const std::vector<std::uint8_t>& d, std::size_t at, int n) {
    std::uint32_t v = 0;
    for (int i = 0; i < n; ++i) v = (v << 8) | d[at + static_cast<std::size_t>(i)];
    return v;
}

struct RawNote {
    std::int64_t on = 0;
    std::int64_t off = -1;
    int pitch = 0;
    int vel = 0;
    int channel = 0;
    int track = 0;
};

}  // namespace

MidiReadResult readSmf(const std::vector<std::uint8_t>& bytes) {
    MidiReadResult result;
    auto fail = [&](MidiReadErrorCode code, std::string message) {
        result.error = MidiReadError{code, std::move(message)};
        return result;
    };
    if (bytes.empty()) return fail(MidiReadErrorCode::Empty, "the file is empty");
    if (bytes.size() > kMaxMidiBytes) return fail(MidiReadErrorCode::TooLarge, "the file is larger than 8 MB");
    if (bytes.size() < 14 || be(bytes, 0, 4) != 0x4D546864u)  // "MThd"
        return fail(MidiReadErrorCode::Malformed, "not a Standard MIDI File (no MThd header)");
    const std::uint32_t headerLen = be(bytes, 4, 4);
    if (headerLen < 6 || 8 + static_cast<std::size_t>(headerLen) > bytes.size())
        return fail(MidiReadErrorCode::Malformed, "the MThd header is truncated");
    const int format = static_cast<int>(be(bytes, 8, 2));
    const int trackCount = static_cast<int>(be(bytes, 10, 2));
    const std::uint32_t division = be(bytes, 12, 2);
    if (format > 1) return fail(MidiReadErrorCode::Malformed, "SMF type 2 (independent sequences) is not supported");
    if (division & 0x8000u) return fail(MidiReadErrorCode::UnsupportedTiming, "SMPTE timing is not supported; the file needs a PPQ time base");
    if (division == 0) return fail(MidiReadErrorCode::Malformed, "the file's PPQ is 0");
    if (trackCount > kMaxMidiTracks) return fail(MidiReadErrorCode::TooManyTracks, "the file has more than 64 tracks");

    MidiFileData data;
    data.sourcePpq = static_cast<int>(division);
    std::vector<RawNote> notes;
    std::int64_t endTick = 0;
    std::int64_t tempoTick = -1, meterTick = -1;

    try {
        std::size_t pos = 8 + headerLen;
        for (int t = 0; t < trackCount; ++t) {
            if (pos + 8 > bytes.size()) {
                data.warnings.push_back("the header promises " + std::to_string(trackCount) + " tracks; the file has " +
                                        std::to_string(t));
                break;
            }
            const std::uint32_t id = be(bytes, pos, 4);
            const std::uint32_t len = be(bytes, pos + 4, 4);
            pos += 8;
            if (static_cast<std::size_t>(len) > bytes.size() - pos)
                throw Fail{MidiReadErrorCode::Malformed, "track " + std::to_string(t + 1) + " runs past the end of the file"};
            if (id != 0x4D54726Bu) {  // not "MTrk": skip unknown chunks
                pos += len;
                --t;
                continue;
            }
            Reader r(bytes, pos, pos + len);
            std::int64_t tick = 0;
            std::uint8_t running = 0;
            // Open note-ons per channel and pitch (a stack, so overlapping same-pitch notes pair FIFO).
            std::array<std::vector<std::size_t>, 16 * 128> open;
            bool ended = false;
            while (!r.done() && !ended) {
                tick += r.varLen();
                std::uint8_t status = r.peek();
                if (status & 0x80) {
                    r.byte();
                    if (status < 0xF0) running = status;
                } else {
                    if (running == 0) throw Fail{MidiReadErrorCode::Malformed, "running status with no previous status"};
                    status = running;
                }
                if (status == 0xFF) {
                    const std::uint8_t type = r.byte();
                    const auto payload = r.take(r.varLen());
                    if (type == 0x2F) {
                        ended = true;
                    } else if (type == 0x51 && payload.size() == 3) {
                        const double usPerQuarter = (payload[0] << 16) | (payload[1] << 8) | payload[2];
                        if (usPerQuarter > 0 && (tempoTick < 0 || tick < tempoTick)) {
                            data.tempo = 60'000'000.0 / usPerQuarter;
                            tempoTick = tick;
                        }
                    } else if (type == 0x58 && payload.size() >= 2) {
                        const int num = payload[0];
                        const int den = 1 << std::min<int>(payload[1], 6);
                        if (num > 0 && (meterTick < 0 || tick < meterTick)) {
                            data.meterNumerator = num;
                            data.meterDenominator = den;
                            meterTick = tick;
                        }
                    } else if (type == 0x03) {
                        data.trackNames.emplace_back(payload.begin(), payload.end());
                    }
                } else if (status == 0xF0 || status == 0xF7) {
                    r.skip(r.varLen());
                } else {
                    const int kind = status & 0xF0;
                    const int channel = status & 0x0F;
                    const int a = r.byte() & 0x7F;
                    const int b = (kind == 0xC0 || kind == 0xD0) ? 0 : (r.byte() & 0x7F);
                    auto& stack = open[static_cast<std::size_t>(channel * 128 + a)];
                    if (kind == 0x90 && b > 0) {
                        if (notes.size() >= static_cast<std::size_t>(kMaxMidiNotes))
                            throw Fail{MidiReadErrorCode::TooManyNotes, "the file has more than 4096 notes"};
                        stack.push_back(notes.size());
                        notes.push_back(RawNote{tick, -1, a, b, channel, t});
                    } else if (kind == 0x80 || kind == 0x90) {
                        if (!stack.empty()) {
                            notes[stack.front()].off = tick;
                            stack.erase(stack.begin());
                        }
                    }
                }
            }
            endTick = std::max(endTick, tick);
            for (const auto& stack : open)
                for (std::size_t i : stack) {
                    notes[i].off = tick;
                    data.warnings.push_back("a note-on without a note-off (pitch " + std::to_string(notes[i].pitch) +
                                            ") ends at the end of its track");
                }
            pos += len;
        }
    } catch (const Fail& f) {
        return fail(f.code, f.message);
    }

    if (notes.empty()) return fail(MidiReadErrorCode::NoNotes, "the file has no notes");

    const auto scale = [&](std::int64_t t) {
        return static_cast<Tick>(std::llround(static_cast<double>(t) * kPpq / data.sourcePpq));
    };
    for (const auto& n : notes) {
        MidiNote m;
        m.tick = scale(n.on);
        m.dur = std::max<Tick>(1, scale(n.off) - m.tick);
        m.pitch = n.pitch;
        m.vel = n.vel;
        m.channel = n.channel;
        m.track = n.track;
        data.notes.push_back(m);
        endTick = std::max(endTick, n.off);
    }
    std::stable_sort(data.notes.begin(), data.notes.end(), [](const MidiNote& x, const MidiNote& y) {
        return x.tick != y.tick ? x.tick < y.tick : x.pitch < y.pitch;
    });
    data.endTick = scale(endTick);
    result.data = std::move(data);
    return result;
}

}  // namespace flowstate
