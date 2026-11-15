# Ground truth is generated on demand, not committed to the repository

Ground-truth images are produced by running the Reference device (helide) through
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
