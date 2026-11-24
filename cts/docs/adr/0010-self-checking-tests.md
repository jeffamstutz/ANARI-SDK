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
   self-checked. A Test that says neither is self-checked only (the
   conversion of the existing Tests flipped this default; before it such a
   Test was compared against Ground truth). `generate` skips every
   Test without `.groundTruth`, as it skips behaviour Tests. The SSIM/PSNR
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
- When the harness landed, many existing Permutations failed their effect
  checks (113 Cases on halcyon, 115 on helide). That was the finding the audit
  made, visible per Case; the defaults were not loosened and no axis was opted
  out to hide it. The conversion (below) fixed the Tests; what still fails is
  a device's. CI runs only `synchronization/` and the unit tests, so it
  stays green.
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
    assertion names the siblings it read. It may also read the Case's own
    values on the other axes (`s.values()`, a BuildContext), for an
    expected image that depends on them (ticket 29: the transmittance at
    the Case's `unitDistance`).

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
defaults (ADR-0003). No older Sidecar can be misread without them, so under
`cts/AGENTS.md`'s schema rule they need no bump.


## Exact checks (ticket 17)

Some answers are analytic: which primitive ID a pixel holds, the depth of a
known surface, "pixels outside this region are background". They were
compared against the Reference device's image, and couldn't be stated at all
for an extension it lacks. The readback discarded the raw values (depth was
scaled and quantised, IDs turned into colours), and `behaviorCheck` replaces
rendering for the whole Test and returns only a bool and a string.

20. **An exact check is a free-form per-Case function over raw channel
    values.** `.expectExact("label", fn)`, any number per Test, runs on every
    rendered Case in the per-Case phase (decision 11). `fn` receives an
    `ExactCheck` (`ExactCheck.h`): the Case's axis values, its frame size,
    and typed read-only views of each declared Channel: `ids(channel)`
    (`uint32_t`), `depth()` (`float`), `rgba(channel)` (the 8-bit comparison
    image, for colour and the rest). Reading a Channel the Test doesn't
    declare fails the check.
    *Rejected:* a closed set of declared predicate forms: tickets 09-13 need
    shapes nobody can enumerate now (silhouette covers a union, shear
    direction, penumbra width, edge contrast).

21. **The runner keeps raw values** (`FrameReadbackResult::depth` / `ids`)
    alongside the 8-bit image inside the per-Case phase only. They are not
    written to the results tree and don't cross a process boundary.

22. **Checks report through helpers, not a bare bool.** `allPixels(name,
    view, expected, pred, where)`, `matches(name, view, expected(x, y),
    tolerance, where)` and `backgroundOutside(name, view, subject,
    background, isBackground)`. Each records a finding: the number of
    offending pixels of those checked, the first offender (scanned from row
    0, the bottom, left to right) with its coordinates and got and expected
    values, and a mask (white offending, black held, mid grey not checked)
    written to the results tree on failure. A finding with nothing checked
    fails. The check passes when every finding does; a check that records no
    finding, or throws, fails with that reason (findings recorded before the
    throw are kept).

23. **Scalar measurements use the same result shape:** `measure(name, value,
    Limit::AtLeast | AtMost, bound)` records a named number with its bound
    ("edge contrast 0.31, required >= 0.6"). This is where magnitude is
    checked; changed fraction can't tell a large change from a small one.

24. **`project(worldPoint)`** maps a world-space point to continuous pixel
    coordinates (Image convention, row 0 at the bottom) through the Case's
    camera (`CameraProjection.h`: perspective or orthographic, with its
    image region). The runner records the projection of the camera it
    frames, whose `fovy` it now sets explicitly to the spec's default (pi/3)
    so `project()` doesn't rely on the device's default, and whose aspect is
    the Case's. A Test's
    own `.camera` hook builds its camera with `BuildContext::newCamera(
    projection)`, which creates the camera the projection describes (aspect
    the Case's frame's) and records it in one call, so the camera is
    described once (ticket 09); `setCameraProjection()` remains for a camera
    the projection can't build. Without a recorded projection, `project()`
    fails the check, saying so. `fovy`, `height` and `aspect` describe the whole
    sensor and `imageRegion` the part of it the frame shows.

25. **New pixel assertions use `.expectExact`, not `.behavior`.**
    `behaviorCheck` stays for behaviour that isn't a property of one rendered
    image; existing behaviour Tests are not converted, and a Test can't have
    both (the builder throws).

26. **An evaluation seam for Test authors.** `evaluateSelfChecks(test,
    cases, inputs)` evaluates a Test's exact checks and its relations
    (effect, equivalence, derived expectations) against supplied channel
    data, with no device; `evaluateExactChecks` is its per-Case half, which
    the runner calls. The unit tests' generators (`idChannel`,
    `depthChannel` in `tests/unit/cts_test_images.h`) produce raw buffers
    through the runner's own decoder. A ticket author builds a correct image
    and a wrong one in code and shows the check accepts the first and
    rejects the second. For Tests no SDK device can run (tickets 09-11), a
    synthetic positive and negative per check is the accepted validation; it
    validates the check, not the scene, and each such ticket's answer must
    say so.

A finding is an `exact` assertion in the Sidecar: `label` (the check's),
`expectation` (the finding's name), `channel`, `pixels` (`offending`,
`checked`, `firstOffender` with `x`, `y`, `got`, `expected`) or
`measurement` (`value`, `bound`, `limit`), and `maskImage`. For a pixel
finding `score` is the offending fraction and `threshold` 0; for a
measurement they are the value and the bound. These fields are additive to
schema v4, read with defaults; no earlier Sidecar holds an `exact` entry, so
under `cts/AGENTS.md`'s schema rule they need no bump. A
Test whose Ground truth is missing still renders when it has exact checks,
as it does for relations (decision 8). The text report counts `exact` among
the failure kinds and names a failed finding's check and mask.

`frame/frame_primitiveID_channel` is the proof: eight triangles framed by
the runner's camera; the ID at each triangle's projected centroid must be the
triangle's index, and pixels outside every triangle (grown by 1.5 pixels, so
edge pixels aren't judged) must hold no triangle's index. The 1.2 draft's
section 5.1 says only that the channel holds the "primitiveId attribute of
the first hit" and gives no value for a pixel without a hit, so the
background is checked for not claiming a triangle rather than for a specific
value; halcyon and helide both write `0xffffffff` there. The Test declares
`.noGroundTruth()`. On 2026-11-15 at 256x256 it passed on halcyon and helide
on a workdir where `generate` never ran (8 of 8 centroids, 0 of 50 929
background pixels offending). The evaluation seam's unit test runs the same
check on a rasterized correct buffer (passes) and on wrong ones: two
triangles' IDs swapped, a background of 0, the image upside down (all fail).

## Volume equivalence: frame the scene, don't loosen (ticket 08)

A ray-marched volume samples two equivalent fields at different points:
devices step by the field's spacing and jitter the first step, so two grids
holding one function differ wherever the colour depends on how deep the first
samples land. With a perspective view of a field that varies along every axis,
the cell-centered Test (then a single `volume/structured_regular_cell_centered`)
needed 4 % at τ = 32 to pass on helide and halcyon (1.7 % measured), and still
failed visrtx at 5.66 %. That loosening is not the pattern: an equivalence
between volumes is framed so the default tolerance holds.

- **Orthographic, straight down one axis** of the field, with a fixed camera
  rather than the runner's framing (a field with wrong bounds then renders at
  the wrong size). No pixel sees a side face.
- **Opaque at once:** opacity 1 and a small `unitDistance` (0.001), so the
  image is the first samples' colour.
- **Constant along the view axis:** the field varies only across the view, so
  where along the ray the first samples land doesn't change the colour. A view
  then checks the field's extent and layout across it, not along it.
- **A second view down another axis:** a volume equivalence must cover the
  axis along the view, so it also runs as a second Test looking straight down
  another axis, with the field laid out to be constant along that axis and
  vary in the other two. Between them the two views check all three axes.
  Nothing is compared across views, which is why they are two Tests rather
  than one with a view axis (an axis implies an effect check between views,
  and opting out is only for effects images can't judge). The example is
  `volume/structured_regular_cell_centered_down_z` and `..._down_x`.
- **Silhouette edges on whole pixels:** the orthographic `height` puts the
  bounds' edges on pixel boundaries at the default frame size (the
  cell-centered Tests use `height` 8/3 for bounds [-1, 1], edges at 1/2 ± 3/8
  of a square frame, whole when its size is a multiple of 8). No pixel
  straddles the edge.
- **If a difference remains at the silhouette**, compare outside a thin band
  with a derived expectation (`maskedBy`, or `regionComposite`'s margin), not
  a looser tolerance. The cell-centered Test did not need one.
- A per-axis `.equivalenceTolerance` remains possible (decision 5), but only
  with the measured spread on at least two devices stated beside it and a
  stated margin below the failure the Test targets. `.effectMargin` has the
  mirror rule (item 65).

Measured on 2026-11-17 with the default tolerance (0.2 %, τ = 8), each view
on its own, gating removed on helide and halcyon, which don't implement the
extension: a 17³ grid of the same function in place of the cell-centered
field renders identically to the 9³ vertex-centered one on helide and halcyon
(0 pixels differ at all, at 200, 256 and 512, in both views) and within 1
level on visrtx (1.4-1.7 % of pixels); the cell-centered field read as
vertex-centered (helide, halcyon) changes 56 % of pixels in both views at
256. visrtx, which implements the extension, passes both views at 256, 200
and 512 (2.0-2.4 % of pixels differ, by 1 level). An earlier measurement of
the single view on visrtx saw differences up to 8 levels, exactly τ.

Ticket 11's cross-representation equivalences (structuredRegular,
unstructured, nanovdb; filters; element types) build on this framing,
second view included.

## Convergence guard (ticket 18)

Changed fraction is asymmetric under noise. A noisy Candidate fails equivalence
checks, which is visible, but it also *passes* every effect check, because noise
alone moves more than 1 % of pixels past τ. Effect is the default meaning of a
Permutation, so a path tracer at one sample per pixel would pass them all
without the parameter doing anything.

27. **Tolerances never adapt to the Candidate.** A measured noise floor that
    widens τ lets a noisy device loosen its own bar.
28. **The contract:** relational assertions (effect, equivalence, derived
    expectations) require the Candidate's image to be converged to within τ at
    the run's accumulation count (`--accumulation`). Getting there is the
    operator's job.
29. **The guard:** for each Test with an evaluated relational assertion and a
    color Channel, the runner renders the baseline Case (the first Case in
    expansion order), keeps accumulating for as many frames again, and requires
    the changed fraction between the two color images to be within the
    equivalence maximum (0.2 %, τ = 8). *Rejected:* two fresh renders (many
    renderers seed deterministically, so two fresh renders can be identical
    while still noisy). The guard frame has `accumulation` on even when the run
    renders once, so it can keep refining. It runs only on a device that
    advertises `KHR_FRAME_ACCUMULATION`; one without it passes trivially and
    records nothing. A Test with no color Channel is not guarded (depth and ID
    Channels are never accumulated). Exact checks are not guarded either: they
    carry their own tolerances.
30. **If the guard fails,** every evaluated relational assertion of the Test
    is recorded failed with the reason "not converged at N frames (changed
    fraction X %, ...); raise `--accumulation`", the check's own finding kept
    after it. They are never dropped and never passed. The guard's own
    `convergence` assertion (Sidecar: `score` the changed fraction, `threshold`
    the maximum, `framesRendered`, `refinementProgress` if reported) sits on
    the baseline Case, or on the first selected Case when the filter excludes
    it, and the report counts "convergence" as its own failure kind.
31. **`refinementProgress` is the stop condition, not the proof.** When the
    frame reports it, accumulation stops at 1 or at the run's accumulation
    count, whichever comes first, so `--accumulation` is a cap; when it isn't
    reported, exactly that many frames are rendered. The guard still runs for
    every device. A device that reported 1 and then changed beyond τ has
    contradicted itself, and the detail says so.
32. **Known gap, documented, not fixed:** a device that *stops refining while
    still noisy* (a low sample limit) gives a stable image, passes the guard,
    and can still pass effect checks on noise. Neither continued accumulation
    nor `refinementProgress` (which reads 1) detects it. Configuring the
    Candidate to converge is the operator's responsibility; the guard catches
    "still refining" only.
33. **A support render never overwrites a scored Case's images.** In `run
    --stdin` one Runner handles each line in turn, and a later line may need a
    Case scored earlier as a partner. The Runner remembers the Cases it scored
    and skips the support render's image write for them, so a non-deterministic
    device's images keep matching the scores in their Sidecars.

Measured on 2026-11-17, full Catalog at 256x256 (`anariCts run`, no Ground
truth): halcyon's default renderer and helide change no verdicts (207/121/59
and 160/107/120 passed/failed/skipped, as before). helide doesn't advertise
`KHR_FRAME_ACCUMULATION`, so the guard doesn't run; halcyon's default renderer
passes it on all 68 guarded Tests (0.00 % changed). Wall time, default
`--accumulation 16`: halcyon 5.9 s to 6.4 s, helide 5.1 s to 5.3 s. halcyon's
`quality` renderer fails the guard on 66 Cases at `--accumulation 1` (about 10
to 13 % of pixels change in the next frame), 60 at 16, 26 at 64, 1 at 128 and
none at 256, which is the count that passes it on every Test.

One metric for assertions and the guard; a second for magnitude (decided
2026-11-24). Relational assertions and this guard use changed fraction only
(decision 5). `frame/progressive_rendering`'s accumulation behaviour check
(ticket 14) uses `meanAbsoluteDifference` in `Metrics.cpp` instead, because it
measures how far noise shrinks, which a count of pixels past τ can't see once
the noise falls below τ. That metric is for behaviour checks that measure
magnitude, never for an effect, equivalence, exact check or this guard, so the
two definitions of "converged" stay apart: the guard's (still changing past τ)
and the accumulation check's (mean difference shrinking).

## Converting the existing Tests (ticket 04)

Every Permutation of the catalog now has its effect check judged, and a Test
declares what it compares against Ground truth. What the conversion decided:

34. **The default is flipped.** A Test that declares neither
    `.groundTruth(reason)` nor `.noGroundTruth()` is self-checked only
    (`TestDef::usesGroundTruth()`); one with nothing to evaluate is skipped.
    Ground truth stays, with a reason, on 54 of the 115 Tests whose appearance has no
    analytic answer in the Test (lit sampler, material and light shading,
    volumes, the shape layouts that aren't a plain triangle or quad).
35. **An unset value that the spec defines is not a Permutation value of its
    own.** `filter` (default linear), `wrapMode*` (clampToEdge) and `side`
    (front) left unset render like the explicit default; the unset value is a
    derived expectation of it, so the pair is judged for equality instead of
    failing an effect check, and the default is the baseline. `omnidirectional`
    `layout` (equirectangular) is a Variant. The same holds for `sampler/
    primitive`: a 4-component array's alpha is ignored by a colour, so
    `arrayDim` 4 is derived from 3.
36. **A frame buffer type axis is a Permutation whose float and sRGB values are
    derived.** The float type's 8-bit comparison image equals the linear one's;
    the sRGB type's is the linear colours encoded with the sRGB transfer
    function (the readback copies the buffer's bytes undecoded), so
    `tests/FrameTypeAxis.h` derives both from the linear value. Making these
    Variants would have failed a conforming device. `frame_color_channel` also
    requires mid-tone pixels (its scene is dim), since a saturated frame hides
    the encoding.
37. **Exact checks replace Ground truth where the answer is analytic:** depth
    (the distance to the camera) and albedo at each triangle's centre, object
    and instance IDs (a set id, and `-1u` where none is set, per appendix D),
    volume object IDs (each id covers the frame, id 1 on the left), the
    background colour (sRGB-encoded, with its alpha), `near`/`far` clipping
    (a Case that removes the subject shows one flat colour, one that doesn't
    shows the subject), depth of every triangle and quad at its centre, and
    that attribute colours show on primitives at all. Samples avoid a quad's
    diagonal, where helide and halcyon let a ray through.
38. **Scenes reworked so an axis shows:** directional light directions with
    different cosines, point and area lights close enough and bright enough
    that intensity and power move many levels, quad and ring lights placed to
    face the grid, a ring's baseline opening narrower than the grid (so
    `falloffAngle` and `openingAngle` have an edge to move) and a 2D
    intensity distribution with azimuthal contrast (so `c0` can); the volume
    Test uses a ramp across a fixed orthographic view (the default camera
    re-frames the world's bounds, which hides `origin`); and `volume/volume`'s
    `valueRange` is set as FLOAT32_BOX1: it was a FLOAT32_VEC2, which a device
    reads as no range at all (a Test bug the effect check exposed).
39. **`camera_general`'s region is asymmetric** `(0, 0.5)-(0.5, 1)`, named once
    (`kRegion`), so a device that mirrors or flips it fails. Its edges are
    whole pixels at a run size that is a multiple of 4.
40. **Default `aspect` with an `imageRegion` has no Test.** The 1.2 text says
    an unset `aspect` is the Frame's width over height and that "a potentially
    set image region needs to be regarded as well", but gives no formula. The
    first Tests assumed aspect = frame aspect x region width / region height;
    helide, halcyon and visrtx all ignore the region there, so the Tests
    failed every device on the Test's assumption, not on a spec statement.
    They were deleted (`*_region_default_aspect`); the formula is not in the
    spec and is to be raised with the working group. `perspective_default_fovy`
    equals pi/3.
41. **The allow-list is documentation of the Reference devices' limits.** What
    stays on it (147 pairs, none seeded) is what helide, and halcyon's default
    preview renderer, can't show: a light under halcyon's headlight blend, a PBR
    lobe neither shades, opacity without `alphaCutoff`, ambient light,
    the spatial field's `filter`, and changes within SSIM 0.7 / PSNR 20 on a
    washed-out lit colour. Each entry names which; none is a Test that can't
    tell its values apart on the Candidate.
42. **A Test can declare optional renderer parameters** (`.rendererParam`),
    set only when the renderer subtype lists them (device introspection; the
    run's `default` is the device's first subtype when it isn't listed) and
    recorded in the Sidecar. halcyon's preview renderer and helide pin the
    ambient radiance unless `ignoreAmbientLighting` is false (so the ambient
    and every dark-scene light Test failed on a mode, not on the lobe), and
    halcyon's quality renderer renders the ID channels at a quarter of the
    resolution unless `resolutionDivider` is 1. The ambient, light and
    ID-channel Tests declare them. The command line's `-p` still overrides.
43. **The PBR lobe Tests render a grey dielectric sphere.** `metallic`
    defaults to 1 in the spec, which multiplied every dielectric lobe
    (specular, ior, transmission, most of clearcoat) away, and a flat white
    surface facing the camera saturates and hides a highlight, a sheen or a
    Fresnel change. The scene fixes `metallic 0` (except in the metallic
    Test, where unset must stay the spec default), a mid-grey base, a weak
    ambient term and, where the Test doesn't vary it, `roughness 0.3`;
    `iridescenceThickness` is in nanometres (constant 400, a 100 to 800
    attribute, a sampler scaled to 800 through `outTransform`). A lobe changes
    a highlight, a small part of the frame, so the lobe axes measured below
    1 % carry an effect margin of 0.3 % (about 200 pixels; items 65 and 66).
    `thickness` and the attenuation axes are opted out everywhere, since no
    renderer in the SDK or visrtx shows them on a thin surface.
44. **`noEffectCheckOn(axis, library, renderers, reason)`** opts an axis out
    only on a device known not to render the parameter: helide reads only
    `baseColor` and `opacity`; halcyon's preview only `baseColor`, `metallic`,
    `roughness`, `ior`, `emissive` and `normal`. The assertion reads "not
    evaluated" with the device's reason, so a Candidate that does render the
    parameter is held to the check. The cone and cylinder `caps` axes
    (helide, halcyon: neither reads `caps`) and helide's `ambientColor` are
    opted out the same way, with no device ticket. The near/far and
    spatial-field `filter` axes stay failing on helide and halcyon, as
    cts-device-findings 10 and 11 say.
45. **Ring `c0`** uses an intensity distribution bright in one azimuth, so a
    device that ignores `c0` can't pass by averaging a checker; halcyon's
    quality renderer passes it.

Measured on 2026-11-18 (256x256; helide doesn't advertise
`KHR_FRAME_ACCUMULATION`). 389 Cases (the two region default-aspect Tests are
gone).

| Run | Passed | Failed | Skipped | Wall time |
| --- | ---: | ---: | ---: | ---: |
| halcyon preview, `--accumulation 16`, no Ground truth | 254 | 17 | 118 | 5.4 s |
| halcyon preview vs halcyon Ground truth | 309 | 17 | 63 | 14.9 s |
| halcyon `-r quality --accumulation 256`, no Ground truth | 285 | 20 | 84 | 71.8 s |
| helide, no Ground truth | 176 | 17 | 196 | 4.1 s |
| helide vs helide Ground truth | 248 | 17 | 124 | 10.1 s |

The 17 failures on halcyon preview and helide are the same Cases: the
near/far clip planes (4 Cases; cts-device-findings 10), the spatial field
`filter` (1; 11), and `alphaCutoff` and opacity on matte and physicallyBased
(12; 04 to 07), all devices'. halcyon's quality renderer adds 3 Cases of
`pbr_iridescence`'s `iridescenceThickness` bound to an attribute or a sampler:
it reads a thickness from a texture as glTF does (the green channel, mixed
between 400 and 100 nm), not as nanometres in the value (no ticket yet). There
is no Ground-truth failure. visrtx's quality renderer (`--accumulation 64`)
passes all 62 `material/pbr_*` Cases it runs, which validates the lobe scenes
on a device that renders the lobes.

## Motion and shutter (ticket 09)

The camera shutter, rolling shutter, camera motion, instance motion (matrix
and decomposed keys) and triangle and quad motion deformation are covered by
`tests/motion.cpp`. No SDK device advertises any of them; the Tests are
validated by synthetic images (`tests/unit/test_cts_motion.cpp`) and on
visionaray where it advertises the extension.

46. **Instant shutters at key times.** The spec spaces keys uniformly over
    `time` but doesn't say how they are interpolated. The main check of each
    motion extension renders with a shutter `[t, t]` at the times the keys
    sit at (the start, middle and end of `time`), where any interpolation
    gives the key itself, and requires the static scene built at that key (a
    Variant `subject`). The motion forms include `time` [-1, 1] over five
    keys (the last three at 0, 0.5 and 1), motion keys over a decoy
    `transform` (instances: the keys take precedence), decomposed camera keys
    and `motion.transform` over decoy decomposed ones (the camera: the matrix
    overrides), and for `motionScaleRotationTranslation` a `motionTransform`
    holding the same keys as matrices (gated on its extension).
47. **Between keys, only translation.** Where a check has to know where the
    subject is between keys (open and rolling shutters), it moves by
    translation, which every interpolation agrees on. The open shutter's
    exact check requires the part both end silhouettes share to show the
    subject, nothing outside their hull, and at least a quarter of each part
    only one covers to show it (blended: about half at one sample per pixel,
    so the check holds without accumulation); an instant shutter shows the subject where
    it was. Identical keys render as no motion, with the shutter open.
48. **The rolling shutter is checked only where every schedule agrees.**
    The spec gives `rollingShutterDirection` and `rollingShutterDuration`
    ("the open time per line, clamped to [0, shutter.upper-shutter.lower]")
    but not when each line opens, nor which edge a direction starts from;
    readings differ (visionaray delays each line's start by its share of the
    duration and keeps it open for the whole shutter, `down` starting at the
    bottom row). So the Tests use a duration strictly inside the shutter
    (0.5 of `[0, 1]`), where any reading shears, and check three things that
    hold under any: the bar's centre moves one way across bands of lines at
    10, 50 and 90 % of the frame, by at least 3 % of its travel plus a pixel
    at each step (sheared); `none` moves it by no more than that (no shear);
    and the reversed direction (`up`, `left`) is a derived expectation, the
    first one (`down`, `right`) mirrored across the scan's middle line,
    compared only where the mirror shows background at least 3 pixels from
    the bar, so it must shear the other way and the noisy inside of the smear
    isn't judged. That assumes only that a reversed direction runs the same
    schedule from the other edge (the scene is symmetric across the scan's
    middle line); the CTS can't compare the two Cases' shear directly, as an
    exact check sees one Case. The schedule is a question for the working group; the
    checks tighten once the spec answers. The synthetic tests pass two
    schedules (lines open one after another over the shutter, each for the
    duration, `down` from the top row; and visionaray's) and fail no shear,
    the same shear both ways and a sheared `none`.
49. **A moving camera leaves its frame at the spec's defaults** (at the
    origin, looking down -z, +y up), so whether a device applies the motion
    keys to `position`/`direction`/`up` or uses them as the camera's
    placement, the keys place it the same; the spec doesn't say which. Its
    Cases record the rest camera's projection, and their exact checks say
    where the subject appears from it.
50. **Geometry motion sets `vertex.position` too** (the first key), as the
    triangle and quad require it; the spec doesn't say which wins, so no
    check depends on it. `motion.vertex.tangent` is set alongside the
    normals but has nothing to show without a normal map.

## Depth of field and stereo (ticket 10)

`KHR_CAMERA_DEPTH_OF_FIELD` and `KHR_CAMERA_STEREO` are covered by
`tests/optics.cpp`, on the perspective camera. The spec gives each two
parameters and a line of description, no model, so each check holds under
every reasonable reading. Depth of field was run on the installed devices
that advertise it (halcyon, visrtx, barney, visionaray; results in ticket
10's answer); stereo, which no installed device advertises, is validated by
synthetic images (`tests/unit/test_cts_optics.cpp`). Cases gate on the run's
renderer's own extensions when it reports any (ADR-0005), so halcyon's
`preview`, which doesn't claim depth of field, skips these Tests as
"renderer preview lacks ..."; `-r quality` runs them (6/6 at
`--accumulation 256`).

51. **Depth of field is judged by edge width, unit-free.** A bright square
    in a dark border (so its edges lie in its plane and the background is
    kept away), on a near and a far plate that project to the same size. Per
    edge, the profile across it (mean of the square's middle rows) rises
    from 10 % to 90 % within 2.5 pixels when sharp, and over at least 0.2 of
    the square's half extent (4.4 pixels at 256) when blurred; a thin lens of
    the Test's apertures (0.12, and 0.07 focused at 1), read as a radius in
    world units, spreads it over 10 to 18, and read as a diameter over 5 to
    9. The spec doesn't give `apertureRadius` a unit ("size of the
    aperture"); the apertures are large enough that either reading passes,
    and a much smaller unit would fail (a question for the working group).
    The bound doesn't depend on how the lens is sampled. The square stays centred where
    the pinhole puts it (within 1.5 pixels, halfway between its two edges'
    halfway points): a thin lens blurs a point around its pinhole image, and
    the symmetric measure is unmoved by the output transfer (sRGB, tone
    mapping or clipping each move an edge's halfway point, the same amount
    both ways). The plates sit near the view axis, so a device focusing on a
    sphere of radius `focusDistance` rather than a plane agrees to 0.3 pixels.
52. **Depth-of-field Cases** (one `lens` axis): focused on the near plate,
    on the far one, at 1 (both blurred) and with `focusDistance` unset (a
    derived expectation: the 1 Case), the pinhole (neither parameter set) and
    `apertureRadius` 0 with a focus distance set (a derived expectation: the
    pinhole). The first Case samples the aperture, so the convergence guard
    measures a noisy render. Orthographic and omnidirectional depth of field
    has no Test: the spec lists the parameters among those all cameras
    understand but describes only the perspective camera as a thin lens
    (halcyon blurs orthographic too; visrtx and barney only perspective); a
    question for the working group.
53. **A stereo eye is judged by disparity, whatever the convergence.** An
    eye `interpupillaryDistance` / 2 to the camera's left (or right, along
    `direction` x `up`) sees a near plate offset from a far one by a
    disparity fixed by the eyes' separation alone: a parallel, off-axis or
    toed-in convergence (the spec doesn't say which, nor where it converges)
    moves both plates alike. The plates sit one above the other on the view
    axis, the near one red and the far one blue, so in a mono image their
    centroids share a column, and the near one's offset from the far one is
    the eye's disparity: within 0.75 pixels plus 15 % of the parallel eyes'
    (toed-in eyes add a few percent). Their rows are the mono camera's
    (within a pixel plus 5 % of the distance from the middle row): the eyes
    are apart along the right vector. `none` (and `stereoMode` unset, a
    derived expectation) shows no disparity; `interpupillaryDistance` 0 makes
    `left` and `right` the mono image (Variant); unset, it is 0.0635 (Variant
    with the explicit value; its disparity, 2.3 pixels at 256 against a
    tolerance of 1.1, catches no default or a far one, not a near one such as
    0.065).
54. **`sideBySide` and `topBottom` are checked per half, not as a
    concatenation.** The spec says the left eye is at the top for
    `topBottom` and nothing else: not which half holds which eye for
    `sideBySide`, nor how an eye's image fits its half (squeezed from the
    full frame, rendered at the half's size and aspect, or at the full
    frame's aspect and letterboxed). A derived
    expectation (`hconcat(left, right)` at half width) would hold under one
    fit only. So each half must show both plates, at the mono camera's rows
    (`topBottom`: halved, in its half; `sideBySide` letterboxed: halved about
    the middle row), with the near plate offset from the
    far one by between half and all of the eye's disparity (half: squeezed
    across, letterboxed, or rendered at the half's aspect for `topBottom`); `topBottom`'s
    top half is the left eye's way, and `sideBySide`'s halves are offset
    opposite ways. The synthetic tests pass parallel, off-axis and toed-in
    eyes converging at 1, 2 and 4, the three fits and both `sideBySide` orders,
    and fail a mono device, swapped eyes, eyes apart along `up`, one eye in
    both halves, the right eye on top, `topBottom` laid out side by side, an
    unset `stereoMode` other than `none`, and wrong zero and default
    interpupillary distances. The open questions are the working group's;
    the checks tighten once the spec answers.

## Spatial fields (ticket 11)

`KHR_SPATIAL_FIELD_UNSTRUCTURED`, `KHR_SPATIAL_FIELD_NANOVDB` and
`KHR_SPATIAL_FIELD_STRUCTURED_REGULAR_FILTER_CUBIC` are covered by
`tests/fields.cpp` (volume category, plus `geometry/isosurface_sphere`); the
framing is `tests/VolumeScene.h`, shared with `volume.cpp`'s cell-centered
Tests and the unit tests (`tests/unit/test_cts_volumes.cpp`). No SDK device
implements the three extensions. nanovdb and the isosurface were run on
visrtx, unstructured on barney; cubic filtering, which no installed device
advertises under its spec name, is validated by synthetic images only.

55. **One linear ramp, sampled into each representation, renders the same
    volume.** The field is linear across the view and constant along it
    (`rampValue`: slopes 0.5 and 0.3, so a field mirrored or transposed
    across the view fails), which any interpolation over any cells
    reproduces exactly, so no check depends on how a device interpolates
    inside a cell. The framing is the cell-centered Tests' (orthographic,
    opaque at once, bounds [-1, 1]^3, edges on whole pixels at 256), and each
    Test is a pair, `_down_z` and `_down_x`. A representation is a
    Permutation value with a derived expectation of its reference (a value
    with one is exempt from the effect check), so one Test can hold two
    references that must differ: the ramp and a uniform field.
56. **unstructured:** hexahedra, six tetrahedra and six pyramids per block,
    and a mix of the three, over 2 x 2 x 2 blocks (a unit test checks every
    cell's signed volume is positive and that they fill the box); vertex.data plus
    a different cell.data renders the vertex.data ramp (the spec's
    precedence). cell.data is checked only as a uniform field: the spec
    doesn't say how per-cell values are reconstructed (constant per cell, or
    averaged to vertices and interpolated), and only a uniform field renders
    the same under both. `unstructured_extent` checks the field's extent
    without a structuredRegular field to compare with (decision 60).
57. **nanovdb voxel centering is open, so its checks hold either way.** The
    grid is built in code (`tests/NanoVdbGrid.cpp`) with NanoVDB's own
    headers (`external/nanovdb`, NanoVDB 32.9.0 from OpenVDB 13.0.0, the
    version VisRTX vendors; NanoVDB.h is no longer a single header, so its
    two dependencies come with it): one float grid, breadth-first, with
    NanoVDB tools' world bounds (a voxel covers [ijk, ijk + 1] in index
    space), each voxel holding the ramp at that box's centre. The spec says
    only that the extent is in the grid header; it doesn't say whether a
    voxel's value sits at that centre (visrtx's default) or at its index
    point, the box's corner (NanoVDB's own samplers). A corner reading shifts the
    ramp by half a voxel and the bounds by one, so the grid is fine (32^3
    voxels over [-1, 1]: half a voxel moves the ramp under 3 levels) and the
    equivalence is masked to the interior a voxel in (two for the cubic
    filter's reach; `interiorMask`). `nanovdb_extent` allows the same voxel
    at its edges. `nanovdb_filter` checks that nearest, linear and cubic
    differ on a 4^3 zig-zag grid and that unset is linear; the spec lists
    cubic for nanovdb without an extension, so a device must render it.
58. **Wedges have no Test.** The spec says a wedge's bottom three vertices
    are counterclockwise (as a tetrahedron's bottom triangle and a
    hexahedron's and pyramid's bottom faces, which VTK orders the same way:
    normal toward the top) and that the field is VTK-compatible, but
    VTK_WEDGE orders its bottom triangle the other way (normal away from the
    top). A device that locates a point by face-plane signs renders only one
    order, so no wedge holds under both readings; cell type 13 waits for the
    working group. The other three are ordered so that both readings agree.
59. **Cubic filtering is judged where every cubic agrees.** The spec doesn't
    say which cubic (B-spline, Catmull-Rom, ...). `structured_regular_filter_cubic`
    requires nearest, linear and cubic to differ on a 4^3 zig-zag grid (every
    cubic smooths or overshoots a zig-zag unlike linear), and
    `structured_regular_filter_cubic_ramp_down_z` / `_down_x` that cubic
    renders a 9^3 linear ramp as linear does a cell or more inside the
    bounds, where any cubic that reproduces linear functions agrees and
    clamp-to-edge (the spec's border) can't reach.
60. **Extents through a step transfer function are exact checks.** A field
    ramping along x seen down -z through 256 opacities, 0 below the middle
    of the value range and 1 above, is opaque exactly where its value is
    past the step and background elsewhere (constant along the view, so
    each ray is wholly one or the other). `structured_regular_extent`
    (simplified: `origin` (-1, -1, -1) or unset, `spacing` unset or
    (0.5, 0.75, 1), `valueRange` unset or [0.5, 1]) checks the edges the
    spec's bounds ([origin, origin + (size - 1) x spacing]) and value mapping
    put, 1.5 pixels either side unjudged; `unstructured_extent` and
    `nanovdb_extent` do the same without structuredRegular, so a device
    without it still has those fields checked. structuredRegular `data` in
    each element type the spec allows is a Variant of FLOAT32
    (`structured_regular_data_types_down_z` / `_down_x`; FIXED16 holds
    positive values only, so its signed scaling, 32767 or 32768, is under a
    level).

`geometry/isosurface_sphere` checks the isosurface's depth against the
sphere a distance field (33^3 vertices) describes, from a perspective
camera, where the ray meets it more than 0.05 inside its silhouette: within
0.02 plus what the sphere's depth changes by across the pixel (the spec takes
a pixel's depth as the minimum over its samples, anywhere in it); outside,
no closer than the sphere's farthest point. The isovalue as a FLOAT32 is a
Variant of the one-element array.

`volume/volume` was not reworked: ticket 04 gave each of its axes an effect
check, and all pass on helide and halcyon except `filter`
(cts-device-findings 11). The `unitDistance` transmittance relation was
left to its own Test (ticket 25, below).

Measured on 2026-11-23 at 256 (accumulation 16 unless stated): helide and
halcyon run the 10 data-type and 4 extent Cases and pass them (the rest skip:
neither advertises the three extensions or the isosurface). visrtx passes
`nanovdb_down_z` / `_down_x`, `nanovdb_extent`, the extent and data-type
Cases except FLOAT64 data (renders nothing: it makes a 64-bit float CUDA
texture), and fails `nanovdb_filter`'s cubic (rendered as linear: visrtx maps
every filter but nearest to linear). visrtx and ospray pass
`isosurface_sphere` (`--accumulation 256`); visionaray fails it (its
surface is about 0.09 deeper than the sphere, and a FLOAT32 `isovalue`
renders nothing: it reads only an array). barney renders every
unstructured layout pixel for pixel alike (measured with wedges too), but its volumes keep a noise
floor of about 10 levels per pixel at 4096 frames (the convergence guard
passes), so 19 % of pixels differ from its own structuredRegular render: the
equivalences can't be judged on it.

## A slab's transmittance and the `unitDistance` relation (tickets 25, 29)

`volume/volume`'s effect check fails a device that ignores `unitDistance`,
not one that applies it with the wrong exponent. The spec's one exact
statement about volume opacity is in 5.12.1 (normative): the volume is
absorbing particles whose density opacity and `unitDistance` set, and "the
fraction of light which is absorbed while traveling a `unitDistance` length
through the volume will be `opacity`". Through a uniform slab of thickness L
that makes the transmittance T(u) = (1 - opacity)^(L/u), so halving
`unitDistance` squares it. Appendix C's ray march says the same but is
informative; both checks below stand on 5.12.1.

61. **Transmittance is solved per pixel from two backgrounds.** A pixel
    through the slab is E + T B in linear colour, whatever the slab emits
    (E, which depends on how the device shades a volume) and whatever the
    background (B) is, so a render over black and one over white give T =
    P(white) - P(black) per pixel and colour component, with no assumption
    about emission. `volume/unit_distance_transmittance` renders a uniform
    field over [-1, 1]^3 (opacity 0.5, colour (0.4, 0.2, 0.1)) with the
    volume equivalences' framing, down -z (thickness 2), over each
    background, at `unitDistance` u and u/2 (the `render` axis), for u = 2,
    3 and 4 (T(u) 0.5, 0.63 and 0.71; T(u/2) 0.25, 0.40 and 0.5). The u/2
    render over white is a derived expectation of the other three: its
    render over black plus T(u)^2, in linear colour (the Test's frame is
    sRGB-encoded 8 bits, decoded and re-encoded), judged a few pixels inside
    the silhouette (`interiorMask`), where no pixel is partly slab.
62. **The relation holds whatever the march.** It holds for any thickness
    and step count a device marches, as long as they don't depend on
    `unitDistance` (T(u) = a^(1/u) for any a), so it doesn't depend on how a
    device clips the march to the bounds or jitters the first sample. The
    absolute T does, and is checked too (64), with a tolerance that allows
    for it. One view is enough: the field is uniform, so there is no layout
    across or along the view to check (the two-view rule is about a field's
    layout).
63. **Default tolerance, justified by the quantisation.** With each of the
    three input pixels anywhere within half a level of its true value, the
    expected pixel is off by at most 2.2 levels at these T and colours,
    under τ = 8. The dim colour keeps the emission from swamping T over
    white: on its most affected component (the changed fraction takes a
    pixel's largest per-channel difference), unitDistance ignored (T(u/2) =
    T(u)) misses the expected image by 27-41 levels and absorbance scaled
    linearly (1 - T(u/2) = 2 (1 - T(u))) by 13-65 levels (ranges over u).
    Unit tests (`tests/unit/test_cts_volumes.cpp`) run those two and a
    square root in the exponent as negatives on synthetic images, and a
    slab of another thickness as a positive.

64. **The absolute T: the u render over white is derived from the one over
    black.** Its expected image is P(u, black) + (1 - opacity)^(L/u) per
    pixel and colour component in linear colour (L = 2, opacity 0.5: 0.5,
    0.63 and 0.71 at u = 2, 3 and 4), in the same interior mask. That is T
    solved per pixel against the spec's value; T(u/2) follows from it and
    the relation. The default tolerance holds here too: with the slab's
    emission c (1 - T) in both renders, τ = 8 levels on the most affected
    component lets T through within 0.054 below and 0.057 above T_spec at
    u = 2 (the tightest; 0.060 / 0.059 at 3, 0.060 / 0.066 at 4), which is
    - the slab marched up to 0.31 short or 0.33 long at u = 2 (about 15 %
      of its thickness, more than a cell, 0.25), so a device that clips the
      march to the bounds or jitters its first sample by up to a step
      passes;
    - about 7 times the spread of the deterministic devices (within 0.003 of
      T_spec, quantisation) and 1.8 times halcyon `quality`'s per-pixel
      Monte Carlo spread at `--accumulation 256` (0.028);
    - well under visionaray's bias at u = 2 and 3 (0.104 and 0.084; measured
      below), and just under it at 4 (0.069 against 0.066): a synthetic
      slab biased as visionaray's misses by 16 and 12 levels at u = 2 and
      3 but only 9 at 4, so the unit test asserts its failure at 2 and 3.
    Quantisation of the black render adds under a level. Unit tests run a
    march half a cell short as a positive, and as negatives a slab marched
    3 thick, T biased as visionaray's (as if 1.45 thick; the relation holds
    for both) and `opacity` read as absorbed per world unit, not per
    `unitDistance` (T = 0.25 at every u).

Measured on 2026-11-24 at 256 (accumulation 16 unless stated): helide and
halcyon (`preview`) pass all 12 Cases with every pixel within 1 level of
the expected image (also at 200 on helide); halcyon `quality` passes at
`--accumulation 256` (within 9 levels, a few pixels over τ); visrtx passes
at 64. helide_gpu fails `2_halfU_white` (11 % of the masked pixels): it
marches volume rays from the camera's position under an orthographic camera,
so near the silhouette they leave the slab through its sides
(cts-device-findings 16). visionaray obeys the relation on average (the
expected image is within 0.3 levels of its render, mean over the slab), but
its T varies from pixel to pixel by about 0.03 across a slab the spec makes
uniform, and is about 0.6 where the spec's is 0.5 at u = 2;
`2_halfU_white` fails with 0.36 % of its pixels over τ (up to 12 levels),
the same at `--accumulation` 1024 and 4096, so it is not noise.

With the absolute T (64), measured on 2026-11-24 at 256: helide, halcyon
`preview` and visrtx (`--accumulation 64`) render the same images, T within
0.003 of the spec's at every u, and pass all 12 Cases; halcyon `quality`
(`--accumulation 256`) has the right mean (within 0.003) with a per-pixel
spread of 0.028 and passes all 12. visionaray (`--accumulation 1024`)
fails `u_white` at every u: its mean T is 0.604, 0.714 and 0.776 against
0.5, 0.63 and 0.71 (67 to 100 % of the masked pixels over τ). helide_gpu
fails `u_white` at every u (63 to 68 %), from the same orthographic rays
as `2_halfU_white` (cts-device-findings 16): in the frame's centre its T is
within 0.012 of the spec's, towards the silhouette up to 0.46 over.

## Effect-margin overrides (ticket 30)

65. **An `.effectMargin` override states its measurements, as an
    `.equivalenceTolerance` override does.** It stands only with two numbers
    written beside it: the smallest effect the axis produces, over every pair
    the Test evaluates, measured on at least two devices that render the
    parameter (converged, at the default frame size); and the noise floor it
    must clear. The noise floor is the larger of what the same Case changes
    between frames N and 2N on those devices (the convergence guard's
    measure, item 29; two fresh renders are no measure, since devices seed
    deterministically) and the equivalence maximum, 0.2 %, which the guard
    lets a Candidate carry. The margin sits between the two, and only the
    axes measured below the default 1 % on some device carry it: an axis
    that clears 1 % everywhere keeps the default. A pair a device fails for
    a filed device bug is left out of the smallest effect and named. Below
    the equivalence maximum a margin would lose the gap between the two
    kinds of check (Calibrated defaults); `tests/unit/test_cts_catalog.cpp`
    checks that every override lies between 0.2 % and 1 % and names the
    overridden axes.
66. **The PBR lobe margin, measured.** On 2026-11-24 at 256x256 and
    `--accumulation 1024`, on halcyon `quality`, visrtx `quality` and
    visrtx's default renderer, the smallest effect per lobe axis over all its
    pairs was:

    | Test | Axis | halcyon `quality` | visrtx `quality` | visrtx `default` |
    | --- | --- | ---: | ---: | ---: |
    | `pbr_clearcoat` | clearcoat | 0.41 % | 1.63 % | 1.25 % |
    | | clearcoatRoughness | 2.05 % | 0.83 % | 0.64 % |
    | | clearcoatNormal | 4.32 % | 17.65 % | 16.27 % |
    | `pbr_ior` | ior | 2.01 % | 3.43 % | 2.15 % |
    | `pbr_iridescence` | iridescence | 0.54 % | 1.70 % | 1.41 % |
    | | iridescenceIor | 1.92 % | 28.17 % | 27.21 % |
    | | iridescenceThickness | 3.30 %\* | 3.51 % | 3.31 % |
    | `pbr_metallic_roughness` | metallic | 10.33 % | 12.69 % | 11.08 % |
    | | roughness | 4.83 % | 7.21 % | 4.88 % |
    | | normal | 26.99 % | 26.04 % | 25.60 % |
    | `pbr_sheen` | sheenColor | 11.57 % | 12.57 % | 11.45 % |
    | | sheenRoughness | 11.15 % | 12.02 % | 11.62 % |
    | `pbr_specular` | specular | 0.81 % | 1.19 % | 1.00 % |
    | | specularColor | 0.61 % | 0.62 % | 0.60 % |
    | `pbr_transmission` | transmission | 18.08 % | 18.39 % | 14.56 % |

    \* halcyon `quality` renders a bound thickness as the constant, so its
    three bound-vs-constant pairs change nothing and fail
    (cts-device-findings 15); 3.30 % is its smallest other pair.

    The same Cases changed 0 pixels between frames N and 2N on both devices
    at `--accumulation` 256 and 1024 (the guard's measure), so the noise
    floor is the guard's 0.2 %. For scale, halcyon `quality` at 16 frames
    differs from its 1024-frame image by up to 0.98 % of pixels on these
    Tests and visrtx `quality` by up to 8.5 %, which the guard fails as not
    converged. The margin stays 0.3 %, between 0.2 % and the smallest effect,
    0.41 %, on the five axes below 1 % somewhere: clearcoat,
    clearcoatRoughness, iridescence, specular and specularColor. The other
    ten (all at least 1.9 %) went back to the default 1 %, which changed no
    verdict on halcyon (`preview`, `quality`), helide (which opts every lobe
    axis out) or visrtx. A highlight shifted by another cause (a different
    light or normal) can still pass a 0.3 % check on these five axes; the
    margin admits what the measurements allow, no more.

