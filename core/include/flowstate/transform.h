// Local transforms (P1-21): the Studio's `tweak` ops, score IR in, score IR out, with no model call.
// Semantics: docs/bridge-spec.md (`tweak`) and docs/ir-spec.md (Part `density`, `humanize`).
//
// A transform changes only the parts it is given (the caller leaves out locked parts), except
// `transpose`, which moves the whole idea's key and so needs every pitched part. It never changes
// the realizer: the same IR and seed still realize to the same bytes.
#pragma once

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

}  // namespace flowstate
