// Local transforms (P1-21): the Studio's `tweak` ops, score IR in, score IR out, with no model call.
// Semantics: docs/bridge-spec.md (`tweak`) and docs/ir-spec.md (Part `density`, `humanize`).
//
// A transform changes only the parts it is given (the caller leaves out locked parts), except
// `transpose`, which moves the whole idea's key and so needs every pitched part. It never changes
// the realizer: the same IR and seed still realize to the same bytes.
#pragma once

#include "flowstate/realize.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace flowstate {

enum class TweakOp { Register, Transpose, Humanize, Simplify, Intensify, Revoice };

std::optional<TweakOp> tweakOpFromString(const std::string& s);

struct TweakResult {
    std::string score;                 // the new IR JSON; the input unchanged when nothing changed
    std::vector<std::string> changed;  // ids of the parts that changed, in score order
    std::string error;                 // why nothing changed (shown to the user); empty on success
    bool ok() const { return error.empty(); }
};

// `amount`: register = octaves (whole, nonzero, -4..4), transpose = semitones (whole, nonzero,
// -11..11), humanize = 0..1; ignored by the others.
// - register: moves each part's range and literal notes by octaves (drums are skipped).
// - transpose: moves the tonic, every chord symbol and every literal note; motifs and step tokens
//   are relative, so they follow. Refused unless `partIds` names every pitched part.
// - humanize: sets each part's `humanize`.
// - simplify / intensify: lowers or raises each part's `density` by 0.25 (0..1).
// - revoice: moves each chord and pad block to the next voicing family that sounds different.
// A part the op can't change (or that would sound the same) is skipped; if none changes, `error`
// says why. Throws IrError when the score isn't valid IR.
TweakResult tweakJson(const std::string& scoreJson, TweakOp op, const std::vector<std::string>& partIds,
                      std::optional<double> amount);

// A note in the clip's tick domain (kPpq per quarter note), as the Studio's note editor sees it.
struct ClipNoteEdit {
    std::int64_t tick = 0;
    std::int64_t dur = 1;
    int pitch = 60;
    int vel = 100;
};

struct EditNotesResult {
    std::string score;  // the new IR JSON
    std::string error;  // why nothing changed (shown to the user); empty on success
    bool ok() const { return error.empty(); }
};

// Touch: the note editor's edit of one part, written into the part's `touch` (docs/ir-spec.md,
// "Touch"). `remove` are notes as they appear in the realized clip, so `options` must be the ones
// that clip was realized with; each must still be there. Removing a note an earlier edit added takes
// that addition back instead. `add` are exact notes. A move or resize is a removal plus an addition.
// Every other part, and every field of this one but `touch`, stays exactly as written.
// Throws IrError when the score isn't valid IR.
EditNotesResult editNotesJson(const std::string& scoreJson, const std::string& partId,
                              const std::vector<ClipNoteEdit>& remove, const std::vector<ClipNoteEdit>& add,
                              const RealizeOptions& options);

}  // namespace flowstate
