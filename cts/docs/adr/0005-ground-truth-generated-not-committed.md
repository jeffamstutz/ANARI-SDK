# Ground truth is generated on demand, not committed to the repository

Ground-truth images are produced by running the Reference device (helide at first; halcyon now, see the last
update) through
the C++ runner's `generate` mode into the workdir's `ground_truth/` directory,
which is gitignored. Regeneration is a first-class command. The repository
contains scene *code* only — no rendered images.

We chose this over committing ground truth via Git LFS (reproducible and
reviewable, but binary churn, an LFS dependency, and a re-commit on every scene
change) and over fetching versioned artifacts from an external store (lean repo
and version-pinned, but network dependency and hosting to maintain). Generating
on demand keeps the repo lean and guarantees ground truth always matches the
current scene code.

Consequences: every developer and CI job that tests a device must be able to
build and run the Reference device to (re)generate ground truth first. Ground
truth is not version-pinned unless separately archived, so cross-machine
reproducibility depends on the Reference device being deterministic across
platforms (the scene `seed` parameters exist for exactly this reason).

**Update (see ADR-0007):** the `seed` mechanism named above is superseded for CTS
geometry by a deterministic RNG-free Layout; determinism is now by construction.

**Update (see ADR-0010):** Ground truth is now optional per Test. A Test
declares `.groundTruth("reason")` when its appearance has no analytic answer,
or `.noGroundTruth()` when it is fully self-checked (effect and equivalence
between its own Cases); `generate` skips the latter. A Variant's shared file is
written from its first value's Case only. Unit tests still generate their
images in code: nothing is committed or fetched.

**Update (halcyon is the default Reference device):** `generate` defaults to
halcyon's `preview` renderer in its default mode. It is fast and deterministic,
and it renders the light, albedo and normal Tests that helide can't (helide
doesn't advertise those extensions, so 51 image Cases had no Ground truth).
halcyon's `quality` path tracer was not chosen: it is slower and converges by
sampling. halcyon needs ISPC and is off by default (`BUILD_HALCYON_DEVICE`), so
when it can't be loaded `generate` prints a notice and uses helide's default
renderer, and a minimal SDK build still generates. `generate` records the
device, renderer and advertised extensions in `ground_truth/reference.json`.
`run` copies that identity into each Sidecar (`reference`, additive in v4) and
the report shows it next to the Candidate. A Case whose required extension the
Reference lacked is skipped with `no ground truth: reference device (<name>)
lacks <extension>` (`referenceMissingFeatures`; the matrix state `advertised,
Reference device lacks it`), not with "run generate first", which no
`generate` with that device can fix. The main CI workflow builds helide only,
so its CTS ctests run on the fallback.

**Update (one Reference device per workdir):** the record names one device, so
a workdir's Ground truth comes from one device. When
`ground_truth/reference.json` names a different library, device subtype or renderer than this `generate`
would use, `generate` refuses with an error naming both and exits 2 without
writing anything; otherwise the earlier device's files for Cases the new one
doesn't render would stay, and still be scored, under a record naming the new
device. `--replace` clears `ground_truth/` first and then generates, so the
record and the files agree. The same device regenerating (e.g. a `--filter`
slice) adds to and overwrites its own Ground truth as before. A workdir with no
record (older than the record) is not checked.

**Update (one name for halcyon's renderer; the record must be written):**
with no `-r`, `generate` uses halcyon's `preview` renderer whether or not
`--device halcyon` was given (`referenceRenderer()`). halcyon treats
`default` as an alias for `preview`, but the check compares names, so
`generate --device halcyon` recording `default` was refused in a workdir a
plain `generate` had filled. Other devices still default to `default`. If
`reference.json` can't be written, `generate` fails every selected Case and
renders none (exit 1, even when the filter selects no Case) rather than
warning, since a workdir without a record would not be checked by later
`generate`s. With `--replace`, `ground_truth/` has already been cleared by
then, as `--replace` asked.

**Update (Cases gate on the run's renderer):** a renderer may report its own
extensions (object introspection's `extension`, or the renderer's
`extension` property), and may honour less than its device: halcyon's
device list is the union of its renderers', and `preview` doesn't claim
`KHR_CAMERA_DEPTH_OF_FIELD` or `KHR_FRAME_ACCUMULATION`. `run`, `generate`
and `check-properties` gate Cases (and per-Case gating) on the selected
renderer's list when it reports one, and on the device's when it reports
nothing (`RendererExtensions.h`); helide's renderer reports the device's
list, so it behaves as before. Accumulation is gated the same way. A Case
needing an extension the device advertises but the renderer lacks is
skipped as `renderer <subtype> lacks a required feature: <extension>`, with
the subtype resolved (`default` is the device's first renderer when it
doesn't list `default`, so halcyon's reads `preview`); the Sidecar records
the renderer's list (`device.rendererExtensions`) and those Cases' gap
(`rendererMissingFeatures`), both additive in v4, and the matrix state is
`advertised, renderer lacks it`. `generate` decides which Cases the
Reference can render from the Reference renderer's list and records it in
`reference.json` (`rendererExtensions`), so `run` says a Case lacks Ground
truth because the Reference renderer lacked an extension even when its
device advertises it. "Advertised" in the matrix still means the device
advertises it.
