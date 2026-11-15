# Tests check the Candidate against itself: effect and equivalence between Cases

Until now a render Test had one way to pass: its image is within SSIM 0.70 /
PSNR 20 of the Reference device's. That fails three ways. The thresholds are
loose enough that a device ignoring a parameter passes (replacing each Case's
Ground truth with a sibling's still passed 176 of 258 times on halcyon);
tight enough that a legitimately different shading model fails (helide vs
halcyon Ground truth fails on PSNR 16-19.9 with matching depth); and an
extension the Reference device lacks can't be tested at all. Meanwhile
`CONTEXT.md` defined a Permutation as an Axis whose values produce *different*
output and a Variant as one whose values produce *identical* output, but
nothing checked either on the Candidate: Variants shared a Ground-truth file
that `generate` overwrote once per Variant Case, last one wins.

So a Test now asserts on the Candidate's own output. Every device, the
Reference device included, is judged the same way, and Ground truth stays only
where appearance has no analytic answer.

## Decisions

1. **Assertions relate Cases of one Test, through its axes.** There is no
   second build hook and no relation between Tests; a Case stays one render
   with one Sidecar.
   *Rejected:* `.equivalentTo(buildFn)` (a render that isn't a Case has no
   Sidecar, filter name or report entry); relations across Tests (they couple
   `--filter` and feature gating across Tests).

2. **Effect is the meaning of a Permutation axis, checked by default.** Cases
   that differ only in that axis must render differently. A Test opts an axis
   out with `.noEffectCheck("axis", "reason")`; the reason is recorded in the
   Sidecar (an unevaluated `effect` entry) and shown in the report. Opting out
   is for axes that can't be judged by image difference; an axis whose values
   the spec says must *not* change the image is a Variant, not an opt-out.
   *Rejected:* opt-in (`.expectEffect`): a Test whose author forgets it
   verifies nothing, silently, which is the failure being fixed.

3. **Equivalence is the meaning of a Variant axis.** Every Variant Case is
   compared with the Case holding that axis's first value (other axis values
   equal), on the Candidate's own renders, for every device. Existing
   `.variant()` axes need no edits.

4. **Which Cases are compared.** A *group* is the set of Cases that differ only
   in one axis. Simplified expansion gives one group per axis (the baseline and
   that axis's other Cases); cartesian expansion gives one group per
   combination of the other axes, and the effect must show in *every* group (a
   Test where one axis legitimately masks another uses `simplified`, drops the
   combination, or opts the axis out with that reason). An effect is checked
   for **all pairs** within a group, so two *set* values that render alike are
   caught, not only set vs unset. A pair is attributed to its later Case (axis
   value order) and names its partner, so the baseline isn't failed by every
   axis at once. A Case that is only ever the earlier partner of evaluated
   pairs carries no entry of its own but counts as self-checked.

5. **One metric: changed fraction.** The share of pixels whose largest
   per-channel difference exceeds a per-pixel tolerance τ, on the 8-bit
   comparison images (RGB with alpha composited over white, as for SSIM/PSNR;
   `changedFraction()` in `Metrics.cpp` is the only implementation, ADR-0004).
   Unchanged background adds nothing to the count, so no subject mask is
   needed. An effect passes when the fraction is at least a minimum on *any* of
   the Test's Channels; an equivalence when it is at most a maximum on *all* of
   them. ID channels use τ = 0. Per-axis overrides change the numbers, never
   the metric: `.effectMargin("axis", minFraction)`,
   `.equivalenceTolerance("axis", maxFraction, tau)`. Images of different sizes
   are not comparable, and such a pair fails with that detail. Tolerances are
   fixed: they never adapt to a measured noise floor.
   *Rejected:* masked mean absolute difference (needs a correct mask per
   Test); whole-frame SSIM/PSNR (diluted by background: `ambientColor` black
   vs white went undetected).

6. **The verdict is the conjunction** of the Ground-truth comparison (if the
   Test has one and it ran) and every evaluated assertion attributed to the
   Case. Each part is recorded separately.

7. **Ground truth is declared.** `.groundTruth("reason")` says the Test needs a
   Reference device comparison and why; `.noGroundTruth()` says it is fully
   self-checked. A Test that says neither still compares against Ground truth
   (unchanged behaviour); ticket 04 flips that default. `generate` skips
   `.noGroundTruth()` Tests, as it skips behaviour Tests. The SSIM/PSNR
   comparison and its thresholds are untouched.

8. **When something can't be evaluated.**
   - A relation partner excluded by `--filter` (which now also matches
     `<category>/<test>/<case>`) is rendered anyway as a *support render*: its
     images are written; it is not scored, gets no Sidecar and isn't counted.
   - A partner that fails to render fails the relation: "partner `<case>` did
     not render: `<reason>`". Never a pass.
   - Ground truth declared but missing: the self-checks still run and are
     recorded. A failed self-check fails the Case; if they pass, the Case is
     `passed` with `groundTruthEvaluated: false`, counted on its own in the
     summary ("passed, Ground truth not evaluated"). A Case with nothing
     evaluated (a Test with no relation and no Ground truth) is skipped, as
     before.

9. **`groundTruthKey` stays, deterministic.** For Tests that keep Ground truth,
   Variant Cases still share one file, written by `generate` from the Case
   holding every Variant axis's first value only. `generate` also runs the
   equivalence checks on the Reference device and reports a disagreement
   between its own Variants (a warning per pair and a count; it doesn't fail
   `generate`). For `.noGroundTruth()` Tests the Sidecar's key is empty.

10. **`behaviorCheck` is untouched.** It stays for behaviour that isn't a
    property of one rendered image; a behaviour Test has no relational
    assertions.

11. **Two phases per Test.** Per Case: build, render, compare Ground truth,
    producing a `CaseResult` (`Runner::runCase`); this is the unit a child
    process could run. Per Test: a **pure function**
    (`evaluateRelations()` in `Relations.cpp`) takes the Test, its Cases and
    their 8-bit comparison images and returns the assertions, which the runner
    merges into the Sidecars before publishing them. It takes its images from
    memory or from the results tree (`loadCaseRender()`), so a Case rendered in
    a child process can take part; no raw channel values cross that boundary.

12. **No image files** are added to the repository or fetched (ADR-0005
    stands). Unit tests generate their images in code
    (`tests/unit/cts_test_images.h`).

## Calibrated defaults

τ = 8/255 (8 levels), equivalence ≤ 0.2 %, effect ≥ 1 % (`SelfCheckDefaults`
in `Relations.h`). The gap is deliberate: a pair between the two fails either
kind of check.

Measured on 2026-11-15, full Catalog at 256x256, halcyon (default renderer)
and helide, no Ground truth:

- Equivalence: every Variant pair scored exactly 0 % on both devices, except
  halcyon's `normal` Variant (`set` vs `unset`), at 37.2 %
  (`quad_normal_tangent`) and 14.3 % (`triangle_normal_tangent`): a real
  difference, not noise (ticket 21).
- Effect: the scores are bimodal. Of 362 halcyon pairs, 155 scored exactly 0
  and 190 scored ≥ 2 %; the 17 in between all belong to two unconverted light
  Tests whose footprint is tiny (`light/quad` at 0.03-0.19 %, `light/ring` at
  0.2-1.6 %). Of 316 helide pairs, 199 scored 0 and 117 ≥ 2 %, none in between.
  The reworked `light/spot` scores 7-14 % per axis on halcyon.

Both SDK devices' default renderers are deterministic, so τ has no noise to
absorb here; 8 levels leaves room for a stochastic device's residual noise
once it is converged (ticket 18 guards against one that isn't). The numbers
were not moved: no legitimate pair lands in or near the gap.

## Consequences

- The Sidecar schema is v4 (ADR-0003): an `assertions` array per Case (kind,
  axis, related Cases, Channel, score, threshold, τ, evaluated, passed, detail,
  partner image, changed-pixel mask for a failure), `resultImages`, and
  `usesGroundTruth` / `groundTruthEvaluated` / `groundTruthReason`. `verdict`
  keeps its three values. Readers keep assertion kinds they don't know, so
  tickets 16-18 can add `exact` and `convergence` without another break.
- The report counts failures by kind (effect, equivalence, Ground truth,
  other) and "passed, Ground truth not evaluated" on its own line; a failed
  Case's line names the axis and the partner. The per-extension matrix
  classifies from verdicts and the Ground-truth flags, not the skip reason's
  wording: an extension reads `passed` only when every Case of it was fully
  scored, `passed, Ground truth not evaluated` when some passed on self-checks
  alone, and `advertised, partly scored` when some passed and some were
  skipped.
- Until ticket 04 converts them, many existing Permutations fail their effect
  checks (113 Cases on halcyon, 115 on helide). That is the finding the audit
  made, now visible per Case; the defaults are not loosened and no axis is
  opted out to hide it. CI runs only `synchronization/` and the unit tests, so
  it stays green.
- A run renders a Test's Cases even when its Ground truth is missing, if the
  Test has relations; a Test without relations and without Ground truth is
  still skipped without rendering.
- Until ticket 18 lands, a noisy Candidate can pass effect checks on noise
  alone; the SDK devices' default renderers are deterministic.

## Derived expectations and per-Case frame size (ticket 16)

Equivalence as "same image as the first value" can't express three needs of
later Tests: an `imageRegion` Case must equal a *crop* of the full frame
(transformed); stereo `sideBySide` must equal `left` and `right` concatenated,
while `left` and `right` aren't equivalent to each other (many-to-one); and
with a light's `visible = false` the lit surface is unchanged while the
light's footprint becomes background (partial). Two of these, and the
default-`aspect` Test, also need Cases of one Test to render at different
frame sizes.

13. **The derived expectation is the single equivalence primitive.** For a
    named value of an axis, the Test supplies a pure image function over its
    sibling Cases' renders (the Cases holding the same values on every other
    axis); it returns the expected image, and the Case is compared with it
    using the equivalence tolerance (decision 5, including a per-axis
    `.equivalenceTolerance`).
    ```cpp
    .permute("stereoMode", {"left", "right", "sideBySide"})
    .expectDerived("stereoMode", "sideBySide", "left and right, side by side",
        [](const Siblings &s) { return hconcat(s.at("left"), s.at("right")); })
    ```
    Values are named by their string form, as in Case ids (`"none"` for
    unset). The function runs once per Channel. A sibling it reads that
    didn't render (or was skipped) fails the assertion with the reason; the
    assertion names the siblings it read.

14. **Variant is its identity shorthand.** A Variant axis's non-first values
    carry the expectation "the first value's image, unchanged", evaluated by
    the same code. Nothing changed for the existing Variants.

15. **The function is free-form C++** over `Image`, with a required one-line
    description, composed from helpers next to `Image`: `crop` (with
    `regionPixels` for a normalized region), `hconcat`, `vconcat`,
    `maskedBy`. The runner writes the expected image of the scored Channel to
    the results tree (`<case>.equivalence-<n>.expected.png`) and the
    assertion records it with the description (`expectation`,
    `expectedImage`), so a report shows expected, actual and description
    without understanding the function.
    *Rejected:* a closed set of declared transforms (`.cropOf`, `.concatOf`):
    tickets 09-13 keep producing new shapes and each would be a harness
    change.

16. **Partial equivalence:** the function may return a mask with the image
    (`maskedBy(image, mask)`; a pixel is selected where any RGB channel of the
    mask is non-zero). Only selected pixels are counted, and the changed
    fraction is of the selected pixels. A mask that isn't the image's size,
    or selects nothing, fails the assertion.

17. **A value with a derived expectation is exempt from its axis's effect
    pairs** (and from `check-discrimination`'s pairs): the expectation says
    exactly what it must look like, and its image may differ in size from its
    siblings'. The axis's other values still form effect pairs.

18. **Frame size is per Case, relative to the run's size.**
    ```cpp
    .frameSize([](BuildContext &ctx, FrameSize run) -> CaseFrameSize { ... })
    ```
    No hook means the run's size; `--width`/`--height` keep working, and no
    Test hard-codes 256. Everything that reads a size uses the Case's: the
    default camera's aspect, the frame, `behaviorCheck`'s `width`/`height`,
    and the Sidecar (`frameSize`). A hook that can't produce whole pixels from
    the run's size skips the Case with a reason (`regionFrameSize` does this
    for a normalized region); a relation that needs a skipped Case fails.
    *Rejected:* a per-Test size (the `imageRegion` and stereo equivalences
    need sibling Cases at different sizes); resampling one side (forces a
    loose tolerance).

19. **Grouping by value is strict.** A Case value that matches none of its
    axis's values (by string form), or more than one, is an error naming the
    Test, the Case and the value, never silently the first value. The run
    fails that Test's Cases with it.

`camera/camera_general` is the proof: its `imageRegion` Case renders the
middle half of the sensor, `(0.25, 0.25)-(0.75, 0.75)`, at the crop's pixel
size and must equal the crop of the full frame; it is self-checked only
(`.noGroundTruth()`). On 2026-11-15 at 256x256 it scored a changed fraction
of exactly 0 on halcyon and helide, and `direction` and `up` kept their
effect checks (27.1 % and 10.6 %).

The new Sidecar fields (`frameSize`, and per assertion `expectation`,
`expectedImage`, `expectedMaskImage`) are additive to schema v4 and read with
defaults (ADR-0003): no version bump.

