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
  stated margin below the failure the Test targets.

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
    a highlight, a small part of the frame, so the lobe axes carry an effect
    margin of 0.3 % (about 200 pixels; a render's noise changes none).
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
