# Conformance Test Suite (CTS)

The CTS validates an ANARI device implementation by rendering a known battery of
scenes, checking each Test's Cases against each other (effect and equivalence,
ADR-0010) and, where appearance has no analytic answer, comparing the output
against ground-truth images. This context covers
the C++ test catalog, the C++ runner that executes it, and the C++ reporting
that summarizes runs (with a Python layer retained only for PDF).

## Language

### Authoring a test

**Test**:
A single registered definition in the C++ catalog: a short human-readable
description, world-build function, axes, required features, thresholds, and
bounds tolerance. A Test expands into one or more Cases.
_Avoid_: scene, test scene (for this concept)

**Primary extension**:
An extension a Test is evidence for (`covers(...)`): its Cases count toward
that extension in the Report's per-extension matrix. Always also required.

**Support extension**:
An extension a Test needs only to build and render its scene, required but not
covered: the geometry and material its surface is made of, and the camera it
renders with (perspective, unless the Test brings its own camera). An
incidental light is not required. An extension only some of a Test's Cases need
(such as an image sampler a material Case binds) is declared per axis value
(`requireFeatureFor`), not for the Test: a device lacking it skips exactly the
Cases holding the value, and they take no part in their Test's effect or
equivalence checks. An Assertion that needed such a skipped Case as its
partner is recorded as not evaluated (not failed), so a Case left with nothing
else to evaluate is skipped rather than read as discriminated.

**Axis**:
A named parameter dimension a Test is expanded over. Every Axis is either a
Permutation or a Variant.
_Avoid_: dimension, parameter list

**Permutation**:
An Axis whose values must produce *different* output. Each value gets its own
ground-truth image, and Cases differing only in it are checked for an Effect on
the Candidate itself, unless the Test opts the axis out
(`.noEffectCheck(axis, reason)`) because its effect can't be judged by image
difference.

**Variant**:
An Axis whose values must produce *identical* output (e.g. `soup` vs `indexed`).
Each Case is checked for Equivalence with the Case holding the axis's first
value, on the Candidate itself: the identity Derived expectation. All values
share one ground-truth image, written from the first value's Case.

**Derived expectation**:
What one value of an axis must render, as a pure image function over its
sibling Cases (same values on every other axis) with a one-line description:
`.expectDerived(axis, value, description, fn)`, e.g. `sideBySide` is `left`
and `right` side by side, or `imageRegion` set is the crop of the unset
Case's frame. The Case is checked for Equivalence with the image (only the
masked pixels, when the function returns a mask), and the expected image is
written to the Results tree. The value takes no part in its axis's Effect
pairs. A Variant is the identity case (ADR-0010).
_Avoid_: transform, expected-image check

**Case**:
One concrete combination of axis values produced by expanding a Test. A Case is
what actually gets rendered, compared with its Ground truth (if its Test has
one), and related to the other Cases of its Test by Assertions. Variant values
of the same Case map to a shared ground-truth image; Permutation values map to
distinct ones.
_Avoid_: instance, permutation (as a noun for the result)

**Assertion**:
A self-check recorded in a Case's Sidecar (its `assertions`): an Effect or an
Equivalence relating it to partner Cases of the same Test that differ from it
only in one axis (an Effect's partner, or the siblings a Derived expectation
read), with the score (changed fraction), the bound, pass/fail and a detail; or
one finding of an Exact check on the Case alone. A failed pair is attributed to
the later Case in the axis's value order. A Case's verdict is the conjunction
of its Ground-truth comparison and its evaluated Assertions (ADR-0010).
_Avoid_: check (too vague), relation (the pair, not the record)

**Effect**:
What a Permutation axis asserts: Cases differing only in it render
differently, i.e. their changed fraction (share of pixels whose largest
per-channel difference exceeds τ) is at least a minimum on some Channel. Every
pair within a group is checked.

**Equivalence**:
What a Derived expectation (and so a Variant axis) asserts: a Case renders
like the image derived from its siblings (for a Variant, the Case holding the
axis's first value), i.e. their changed fraction is at most a maximum on every
Channel.

**Exact check**:
A per-Case assertion over the Case's raw channel values, for answers that are
analytic: the ID at a pixel, the depth of a known surface, "pixels outside
this region are background". `.expectExact(label, fn)`: a free-form function
given the Case's axis values, frame size, typed channel views (colour, float
depth, `uint32_t` IDs) and `project()` (world point to pixel through the
Case's camera), reporting through helpers (`allPixels`, `matches`,
`backgroundOutside`, `measure`). Each helper records a finding (offending
and checked pixel counts, the first offender with got and expected values,
a mask; or a named number with its bound), recorded as an `exact` Assertion
(ADR-0010).
_Avoid_: pixel test, predicate check, behaviour check (that replaces
rendering; an Exact check judges the rendered Case)

**Timed behaviour test**:
A Test whose check (not an image) decides pass/fail, and which declares a
timeout: `anariCts run` runs each of its Cases in a child process and records a
Case that doesn't finish in time as failed, so a hung device loses that Case
only. The form of the synchronization rules (ADR-0009).
_Avoid_: stress test, watchdog test

### Running and comparing

**Reference device**:
The device whose renders define ground truth: halcyon's `preview` renderer
by default, or helide when halcyon can't be loaded (ADR-0005). `generate`
records which one it used; a Case needing an extension it lacks has no Ground
truth, and the run says so.
_Avoid_: golden device

**Candidate**:
The device being tested: against itself (its Cases' Assertions) and, for a
Test that has Ground truth, against the Reference device's images.
_Avoid_: device under test, SUT

**Advertised extensions**:
The extensions a device reports (`anariGetDeviceExtensions`): what the
Report's matrix calls "advertised". A Case runs only when the run's
renderer has every extension it requires: the extensions the selected
renderer reports of itself when it reports any, else the device's. A
renderer can honour less than its device (halcyon's `preview` lacks depth of
field and accumulation that the device advertises for `quality`); a Case
needing such an extension is skipped as the renderer's gap ("renderer
preview lacks ..."), not the device's (ADR-0005).
_Avoid_: features (in prose; the code's `requiredFeatures` predates it)

**Ground truth**:
The expected reference image(s) a Case is compared against, produced by the
Reference device. Optional per Test: declared with `.groundTruth(reason)`
where appearance has no analytic answer, absent with `.noGroundTruth()` for a
fully self-checked Test (undeclared means self-checked only). A Case
that passed its Assertions while its Ground truth was missing is "passed,
Ground truth not evaluated".
_Avoid_: reference image, golden image, baseline

**Indistinguishable Ground truth**:
Two Cases differing in one Permutation value whose Ground truth images pass as
each other's under the Test's own thresholds, so the Ground-truth comparison
can't tell those values apart. `anariCts check-discrimination` finds them and
fails unless the pair is on the allow-list next to the Tests, with a reason.
Distinct from an Effect, which is checked on the Candidate's own renders.

**Support render**:
A Case rendered only because a selected Case's Assertion needs it as a partner
(the `--filter` excluded it): its images are written so the partner can be
shown, but it is not scored, gets no Sidecar and is not counted.

**Channel**:
A frame output buffer (color, depth, albedo, normal, primitive/object/instance
id) that can be compared independently.

**Probe frame**:
The small frame of an empty world that `run` and `generate` render, and whose
color channel they map, before the first Case. A device that can't render it
stops the command with one error naming the device's own ERROR messages,
instead of failing every Case.
_Avoid_: smoke test (that is the `sink` build check)

**Run record**:
`results/run.json`, which a run that stopped at the Probe frame leaves in the
Results tree: the device and why it could not render. Not a Sidecar (it has no
verdict); a run whose device renders the Probe frame removes it.

**Sidecar**:
The machine-readable JSON file the C++ runner writes next to a Case's images:
which axis values it ran, its per-channel metric scores and pass/fail verdict,
its Assertions and whether its Ground truth was evaluated,
any skip reason, timing, image paths, the producing device's identity, the
extensions it advertised and those its renderer reported, the Test's
required, primary and missing extensions (and which of them only the
renderer lacked),
and the extensions the run's whole Catalog has a Test for. One per Case.
_Avoid_: manifest, results file

**Results tree**:
The `results/` subtree of a Workdir — the full set of a run's Sidecars and
images, plus `run.json` when the run stopped because the device could not
render a probe frame (then there are no Sidecars from that run). It is the
cross-language contract: every reader (the C++ `report`
command and the Python PDF layer) consumes it by globbing, and none of them
render or open a device.
_Avoid_: manifest

**Report**:
The human-facing summary of one run, produced by `anariCts report` from the
Results tree: a text summary (default) or an interactive HTML report (`--html`).
Both include a per-extension matrix: for every canonical KHR extension, whether
the device advertised it, the passed/failed/skipped Cases it is primary for, and
a state (`passed` only when every Case was fully scored, `passed, Ground truth
not evaluated`, `advertised, partly scored`, `failed`, `not advertised`,
`advertised, no Ground truth`, `advertised, Reference device lacks it`,
`advertised, renderer lacks it`, ...; `no Test` when the run's Catalog, as its
Sidecars record it, has no Test covering it), classified from the Cases'
verdicts and Ground-truth flags. A leading `core` row counts the Cases of Tests
that cover no extension. The text summary counts failures by kind (effect,
equivalence, Ground truth, other). A PDF is an optional extra still produced by `ctsReport.py`.
_Avoid_: manifest

### Tooling

**Catalog**:
The in-C++ registry of all Tests, living in the internal `anari_cts_core`
target. `anariCts list` enumerates it; `anariCts run` expands and executes a
filtered slice of it. Distinct from the pre-existing scene registry
(`registerScene`) used by example viewers.
_Avoid_: test registry, scene registry (that name is the other one)

**BuildContext**:
The object the harness hands to a Test's `build()` carrying the current Case's
resolved axis values, read typed-by-name (`get<int>("count", 1)`) or as a raw
`Any` (`value("color")`) for helpers that dispatch on the value's ANARI type.
Backed by `helium::ParameterizedObject`.

**Build hook**:
An optional per-Case builder a Test supplies beyond `build()`. `cameraBuild`
(`.camera(...)`) replaces the runner's bounds-framed camera. The related
`rendererConfig` hook (`.renderer(...)`) instead mutates the runner-created
renderer after its selected subtype and baseline parameters are applied
(ADR-0006). A Test can also declare optional renderer parameters
(`.rendererParam(name, value)`) that the runner sets only when the selected
renderer lists them and records in the Sidecar. The third, `frameSize` (`.frameSize(...)`), gives the Case's frame
size relative to the run's (`--width`/`--height`); a size that isn't whole
pixels skips the Case (ADR-0010).

**Layout**:
The deterministic, human-viewable placement of a Test's primitives — their
positions, orientations, and sizes — so a rendered Case reads as orderly geometry
(a near-square grid of identical canonical primitives facing the camera) rather
than a random pile. Owned by the `anari::cts` layer and generated by the pure,
typed functions in `GeometryLayout`; distinct from ANARI publication in
`GeometryBuilder` and from the viewer-only random `PrimitiveGenerator`.
_Avoid_: arrangement, placement (as standalone terms)

**Workdir**:
The single root directory a run operates under, containing the conventional
subdirs `results/`, `ground_truth/`, and `assets/`. Files mirror the Catalog
hierarchy under each (`<sub>/<category>/<test>/<case>...`).
