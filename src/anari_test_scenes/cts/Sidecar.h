// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Channel.h"
// std
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace anari {
namespace cts {

// The per-Case result the runner writes next to the rendered images. The
// results tree of these sidecars is the cross-language contract (ADR-0003);
// the Python layer consumes it by globbing and never renders. The schema is
// versioned so the reader can stay in step. New *optional* fields (read with a
// default by the Python layer) are additive and do not bump this version; a
// breaking change to an existing field's shape would.
//
// v2 adds the `device` object identifying which device produced the run, so a
// two-device diff can label runs by device rather than by workdir name.
// `description` is additive optional catalog metadata; readers treat its
// absence in older v2 workdirs as an empty description.
//
// v3 adds the extensions behind a Case, so a report can say what a device is
// missing per extension: `requiredFeatures` (all the Test needs),
// `coveredFeatures` (the primary ones it is evidence for) and `missingFeatures`
// (the required ones the device lacks; non-empty exactly when the Case was
// skipped for them), plus `device.extensions`, the extensions the device
// advertised for the run, and `catalogCoveredFeatures`, the extensions some
// Test in the run's Catalog covers (whether or not the run selected it), so
// the report can tell "no Test" from "not run" from the results tree alone
// (ADR-0008). All read as empty from older sidecars.
//
// v4 adds the self-checks (ADR-0010): an `assertions` array per Case (effect
// and equivalence relations between Cases of one Test; later tickets add
// kinds, so readers keep entries of a kind they don't know), and
// `resultImages` (the rendered image per Channel, with or without a
// comparison), and `usesGroundTruth` / `groundTruthEvaluated` /
// `groundTruthReason`, so a Case
// that passed its self-checks without a Ground-truth comparison is told apart
// from one that passed both. From older sidecars `usesGroundTruth` reads true
// and `groundTruthEvaluated` reads true exactly when there are channel scores.
//
// Additive to v4, read with defaults (ADR-0010, derived expectations):
// `frameSize`, the Case's own frame size (0x0 when unrecorded), and per
// assertion `expectation` (what a derived expectation's image is),
// `expectedImage` and `expectedMaskImage`.
//
// Additive to v4, read with defaults (ADR-0010, exact checks): assertions of
// kind `exact`, carrying `label` and either `pixels` (offending and checked
// counts, the first offender) or `measurement` (value, bound, limit). No
// earlier sidecar holds an `exact` entry, so their absence can't be misread.
constexpr int kSidecarSchemaVersion = 4;

// The skipReason of a Case skipped because it has no Ground truth. The report
// tells this skip apart from others by it.
inline constexpr const char *kNoGroundTruthReason =
    "no ground truth (run generate first)";

// A Case's pass/fail outcome.
enum class Verdict
{
  Passed,
  Failed,
  Skipped,
};

const char *verdictName(Verdict v);

// Parse a verdict name; unknown names read as Skipped (best-effort, matching
// the Python reader's default).
Verdict verdictFromName(const std::string &name);

// Which device produced a run, by the names the CLI loaded it with: the ANARI
// library, the device subtype, and the selected renderer subtype. For a suite
// whose purpose is comparing devices, this is what labels a run.
struct DeviceSpec
{
  std::string library;
  std::string device{"default"};
  std::string renderer{"default"};
  // The extensions the device advertised for the run (schema v3), sorted.
  // Empty when unrecorded (older sidecars). Not part of the identity.
  std::vector<std::string> extensions;
};

// One channel's comparison against ground truth.
struct ChannelResult
{
  Channel channel{Channel::Color};
  std::map<std::string, double> metrics; // metric name -> score
  std::map<std::string, double> thresholds; // metric name -> threshold
  bool passed{false};
  std::string resultImage; // path relative to the workdir root
  std::string groundTruthImage; // path relative to the workdir root
  // Optional per-channel debug images written when a comparison runs: the
  // absolute per-channel difference and a thresholded mask of it. Empty when
  // not emitted. Paths are relative to the workdir root, like the others.
  std::string diffImage;
  std::string thresholdImage;
};

// The kinds of Assertion this schema version writes. A later ticket adds
// "convergence"; a reader keeps any kind as a string.
inline constexpr const char *kEffectAssertion = "effect";
inline constexpr const char *kEquivalenceAssertion = "equivalence";
inline constexpr const char *kExactAssertion = "exact";

// Where an exact check's pixel finding first failed: the pixel (Image
// convention, row 0 at the bottom) with the value it held and, in words or
// as a number, the value it should have held.
struct FirstOffender
{
  uint32_t x{0};
  uint32_t y{0};
  std::string got;
  std::string expected;
};

// An exact check's pixel finding: how many of the pixels it looked at
// offend, and the first that did.
struct PixelCounts
{
  uint64_t offending{0};
  uint64_t checked{0};
  std::optional<FirstOffender> first;
};

// An exact check's scalar measurement: a named number (the assertion's
// `expectation`) with its bound.
struct Measurement
{
  double value{0.0};
  double bound{0.0};
  std::string limit; // ">=" or "<="
};

// One self-check attributed to a Case (ADR-0010): for an effect, the relation
// between this Case and a partner Case of the same Test that differs from it
// only in `axis`; for an equivalence, between this Case and the image derived
// from its siblings on `axis` (for a Variant, the first value's Case).
struct Assertion
{
  std::string kind; // kEffectAssertion, kEquivalenceAssertion, ...
  std::string axis; // empty for an exact check
  // An exact check's label (`.expectExact(label, fn)`); its `expectation`
  // names the finding, and `pixels` or `measurement` holds what it measured.
  std::string label;
  std::optional<PixelCounts> pixels;
  std::optional<Measurement> measurement;
  // The partner Case ids (Case::id(), same Test): an effect's partner, or
  // the siblings an equivalence's expected image was derived from.
  std::vector<std::string> relatedCases;
  // An equivalence's expectation, in one line (a derived expectation's
  // description; for a Variant, the identity).
  std::string expectation;
  // The Channel the score was taken on: for an effect the one that changed
  // most, for an equivalence the one that changed most (the worst). Empty
  // when no Channel was compared (a partner that did not render).
  std::string channel;
  // Changed fraction, in [0, 1]; NaN when not computed.
  double score{0.0};
  // The bound on score: a minimum for an effect, a maximum for an
  // equivalence.
  double threshold{0.0};
  int tau{0}; // per-pixel tolerance of the score's Channel, in 8-bit levels
  // False for an axis opted out of its effect check (`detail` has why);
  // such an entry neither passes nor fails the Case.
  bool evaluated{true};
  bool passed{false};
  std::string detail;
  // Paths relative to the workdir root; empty when not written. The partner
  // Case's result image for `channel`, and (for a failed check) the mask of
  // changed pixels. For a derived expectation, the expected image of
  // `channel` and the mask it compared under, if any (a Variant's expected
  // image is its partner's, `partnerImage`). Each is one of the Case's own
  // result files.
  std::string partnerImage;
  std::string maskImage;
  std::string expectedImage;
  std::string expectedMaskImage;
};

// The full per-Case result.
struct CaseResult
{
  std::string category;
  std::string test;
  std::string description; // optional human-readable summary of the Test
  std::string caseId;
  std::string groundTruthKey;
  DeviceSpec device; // which device produced this result (schema v2)
  std::vector<std::pair<std::string, std::string>> axisValues; // name -> value
  // The Test's extensions (schema v3): all it requires, the primary ones it
  // covers, and the required ones the device lacks.
  std::vector<std::string> requiredFeatures;
  std::vector<std::string> coveredFeatures;
  std::vector<std::string> missingFeatures;
  // What the run's whole Catalog covers (schema v3), sorted: the same in every
  // sidecar of a run.
  std::vector<std::string> catalogCoveredFeatures;
  Verdict verdict{Verdict::Skipped};
  std::string skipReason; // empty unless skipped
  std::string detail; // human-readable note (e.g. a behavioral check's outcome)
  double durationMs{0.0};
  // The size the Case rendered at (its Test's frame-size hook applied to the
  // run's size); 0x0 when unrecorded.
  uint32_t frameWidth{0};
  uint32_t frameHeight{0};
  // The Ground-truth comparison (schema v4 adds the flags). `channels` holds
  // its per-Channel scores when it ran.
  bool usesGroundTruth{true}; // the Test compares against Ground truth
  bool groundTruthEvaluated{false}; // ... and the comparison ran
  std::string groundTruthReason; // why the Test needs it, when declared
  std::vector<ChannelResult> channels;
  // The Case's rendered image per Channel (schema v4), relative to the
  // workdir root, whether or not a Ground-truth comparison ran.
  std::vector<std::pair<Channel, std::string>> resultImages;
  // Self-checks attributed to this Case (schema v4).
  std::vector<Assertion> assertions;

  // Whether it passed without the Ground-truth comparison its Test asks for
  // (the Ground truth was missing): its self-checks alone decided it.
  bool passedWithoutGroundTruth() const
  {
    return verdict == Verdict::Passed && usesGroundTruth
        && !groundTruthEvaluated;
  }
};

// Serialize a CaseResult to its sidecar JSON text (pretty-printed).
std::string toJson(const CaseResult &result);

// Write the sidecar through a temporary sibling and atomically rename it into
// place, creating parent directories. False on failure.
bool writeSidecar(const std::filesystem::path &path, const CaseResult &result);

// Parse sidecar JSON text into a CaseResult. Returns false when the text is not
// a sidecar (invalid JSON or missing the `verdict` field). Optional fields read
// with defaults, so older schema versions parse best-effort; when
// `schemaMismatch` is non-null it is set true if the text's schemaVersion
// differs from kSidecarSchemaVersion.
bool fromJson(
    const std::string &text, CaseResult &out, bool *schemaMismatch = nullptr);

// Read and parse a sidecar file. False when unreadable or not a sidecar.
bool readSidecar(const std::filesystem::path &path,
    CaseResult &out,
    bool *schemaMismatch = nullptr);

} // namespace cts
} // namespace anari
