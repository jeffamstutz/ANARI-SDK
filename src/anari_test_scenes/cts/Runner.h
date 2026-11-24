// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "AnariObject.h"
#include "ArtifactPublication.h"
#include "CameraProjection.h"
#include "Catalog.h"
#include "ConvergenceGuard.h"
#include "DeviceErrorLog.h"
#include "ExactCheck.h"
#include "Image.h"
#include "Isolation.h"
#include "Relations.h"
#include "RendererParams.h"
#include "Sidecar.h"
#include "TestDef.h"
#include "Workdir.h"
// anari
#include "anari/anari_cpp.hpp"
// std
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace anari {
namespace cts {

struct RunOptions
{
  uint32_t width{256};
  uint32_t height{256};
  double ssimThreshold{0.70}; // ctsUtility defaults; overridden per-test
  double psnrThreshold{20.0};
  // Color-channel renders per frame when accumulation is supported.
  uint32_t accumulationFrames{1};
  // Baseline renderer ambient light. A renderer Test may override this.
  float ambientRadiance{1.f};
  bool denoise{false};
  // Device-agnostic renderer parameter overrides (CLI NAME=VALUE), applied to
  // every Case's renderer over the baseline but before the Test's own renderer
  // config, so a renderer Test that sets the same name still wins. Types
  // resolve against the device's declared parameter metadata.
  std::vector<RendererParam> rendererParams;
  // The device's identity. Its `rendererExtensions`, when the caller fills
  // them (queryRendererExtensions()), are what Cases gate on instead of the
  // device's list; `extensions` is set from the list run()/generate() get.
  DeviceSpec device;
  // generate: clear ground_truth/ first, so a different Reference device
  // replaces the recorded one instead of being refused (ADR-0005).
  bool replaceGroundTruth{false};
  // When set, run() runs only the Case with this Case::qualifiedId() (in
  // this process: this is how an isolated Case runs, ADR-0009).
  std::string onlyCase;
  // The device's recorded ERROR messages, for behavior checks that fail on
  // them (TestDef::failOnDeviceErrors). Not owned; may be null.
  const DeviceErrorLog *deviceErrors{nullptr};
};

struct RunSummary
{
  int total{0}; // selected Cases; support renders are not counted
  int passed{0}; // generated, or passed comparison
  int failed{0};
  int skipped{0};
  // Of `passed`: Cases that passed their self-checks while the Ground truth
  // their Test asks for was missing (ADR-0010).
  int passedWithoutGroundTruth{0};
  // Of `skipped`: Cases with no Ground truth because the Reference device
  // lacked an extension they need (CaseResult::referenceMissingFeatures).
  int referenceLacks{0};
  // generate: equivalences the Reference device's own Variants failed.
  int variantDisagreements{0};
  // Why the device could not render the probe frame (DeviceProbe.h); empty
  // when it could. When set, no Case ran and the counts are zero.
  std::string deviceFailure;
  // generate: why it refused to write (Reference.h referenceConflict()): the
  // workdir's Ground truth is from a different device and
  // RunOptions::replaceGroundTruth was not set. When set, nothing was written
  // and the counts are zero.
  std::string referenceConflict;
  // generate: why the Reference record (ground_truth/reference.json) couldn't
  // be written. When set, no Case was rendered and every selected Case counts
  // as failed; it is set even when the filter selects none, so the caller can
  // still fail.
  std::string referenceRecordFailure;
};

// Build, render, and score Cases against ground truth, writing images and
// per-case sidecars under a Workdir (ADR-0001/0003). One Runner wraps one
// device: `generate` is driven with the reference device, `run` with a
// candidate. Cameras are framed from world bounds.
struct Runner
{
  Runner(anari::Device device, Workdir workdir, RunOptions options = {});
  Runner(anari::Device device,
      Workdir workdir,
      RunOptions options,
      std::shared_ptr<ArtifactWriter> artifactWriter);

  // Run each Case of a timed behavior Test (TestDef::timeout) through
  // 'isolation' (ADR-0009). Without it, such Cases run in this process and
  // the timeout isn't enforced.
  void setCaseIsolation(std::shared_ptr<CaseIsolation> isolation);

  // Render every selected Case and save its channels under ground_truth/,
  // and record the Reference device (RunOptions::device, with
  // `referenceFeatures`, the extensions the device advertises) beside them
  // (Reference.h). Cases gate on the renderer's own extensions when
  // RunOptions::device lists any (RendererExtensions.h), else on
  // `referenceFeatures`. Refuses, writing nothing,
  // with RunSummary::referenceConflict set, when the record names a different
  // device, unless RunOptions::replaceGroundTruth clears ground_truth/ first.
  // Fails every selected Case, rendering none, with
  // RunSummary::referenceRecordFailure set, if the record can't be written.
  // Does not score or write sidecars (ADR-0005). A shared (Variant) Ground
  // truth file is written from its first Variant value's Case only, and the
  // device's own Variants are checked for equivalence (a disagreement is
  // reported, RunSummary::variantDisagreements). Skips `.noGroundTruth()`
  // Tests. Stops before the first Case, with RunSummary::deviceFailure set,
  // if the device can't render the probe frame.
  RunSummary generate(const Catalog &catalog,
      const Filter &filter,
      const std::set<std::string> &referenceFeatures);

  // Render every selected Case, compare each channel against ground truth,
  // evaluate the self-checks between the Cases of each Test (ADR-0010), and
  // write result images plus a sidecar. A partner of a selected Case that the
  // filter excludes is rendered as a support render (images only). Cases
  // whose required features are absent (from the renderer's own extensions
  // when RunOptions::device lists any, else from `candidateFeatures`, the
  // device's; RendererExtensions.h), or that have nothing to evaluate (no
  // ground truth yet and no self-check), are recorded as skipped. If the
  // device can't render the probe frame, stops before the first Case with
  // RunSummary::deviceFailure set and records that in the results tree
  // instead (results/run.json); a run whose device renders it removes that
  // record.
  RunSummary run(const Catalog &catalog,
      const Filter &filter,
      const std::set<std::string> &candidateFeatures);

  // Render a single Case's channels (in TestDef::channels order) at its frame
  // size (resolveFrameSize()). Empty if the world could not be built or the
  // Case has no frame size. Public for testing and reuse.
  std::vector<Image> renderCase(const TestDef &test, const Case &c);

  // The optional renderer parameters of `test` this Runner sets: those the
  // selected renderer subtype lists (`TestBuilder::rendererParam`). What the
  // Sidecar records.
  std::vector<RendererParam> appliedRendererParams(const TestDef &test);

 private:
  // The committed render objects for one Case: the world plus the camera and
  // renderer (with the Test's optional camera build / renderer configuration
  // hooks applied). `world` is null when the Test has no build function or
  // build() returned null.
  struct SceneObjects
  {
    UniqueAnariObject<anari::World> world;
    UniqueAnariObject<anari::Camera> camera;
    UniqueAnariObject<anari::Renderer> renderer;
    anari::scenes::Bounds bounds{};
    FrameSize size; // the Case's frame size
    // What the camera does to a world point, for exact checks: recorded for
    // the runner's camera, or by the Test's camera hook (may be empty).
    std::optional<CameraProjection> projection;

    // A renderable scene needs all three objects; a build hook (or default
    // camera/renderer) returning null leaves it invalid.
    bool valid() const
    {
      return world && camera && renderer;
    }
  };

  // The per-Case phase's outcome (ADR-0010, decision 11): the Case's result
  // with its Ground-truth comparison, its images to publish, and its render
  // for the relational pass.
  struct CaseOutcome
  {
    CaseResult result;
    std::vector<ImageArtifact> artifacts;
    CaseRender render;
    // The convergence guard's measurement of the Case's color image, when
    // runCase was asked to guard it and the Case rendered.
    std::optional<ConvergenceMeasurement> convergence;
  };

  // The per-Case phase: build, render, compare against Ground truth.
  // `renderWithoutGroundTruth`: render even when the Ground truth is missing
  // (the Case takes part in self-checks). `guard`: measure the color image's
  // convergence after it is read (renderChannels). Never throws.
  CaseOutcome runCase(const TestDef &test,
      const Case &c,
      bool renderWithoutGroundTruth,
      bool guard = false);

  // Run a render Test: the per-Case phase for its selected Cases and their
  // partners, then the relational pass, then publication.
  void runRenderTest(
      const TestDef &test, const Filter &filter, RunSummary &summary);

  // generate for one render Test.
  void generateTest(
      const TestDef &test, const Filter &filter, RunSummary &summary);

  // Build a Case's world and camera (framed for `size`), then configure its
  // renderer. SceneObjects owns every handle and releases partial or complete
  // builds on scope exit.
  SceneObjects buildScene(const TestDef &test, const Case &c, FrameSize size);

  // A Case's rendered channels (with the raw depth and ID values, for its
  // exact checks; kept in the per-Case phase only) and its camera's
  // projection. `channels` is empty if the scene could not be built.
  struct RenderedChannels
  {
    std::vector<ChannelValues> channels;
    std::optional<CameraProjection> camera;
    // With `guard`: the color image's convergence measurement, and the time
    // the guard's extra frames took (not the Case's render time).
    std::optional<ConvergenceMeasurement> convergence;
    double guardMs{0.0};
  };

  // Render a Case's channels at `size`. With `guard` (the run accumulates
  // several frames), the color frame keeps accumulating for as many frames
  // again once its image is read, for the convergence guard (ADR-0010): the
  // Case's own image is the one measured.
  RenderedChannels renderChannels(
      const TestDef &test, const Case &c, FrameSize size, bool guard = false);
  // ... their comparison images only.
  std::vector<Image> renderImages(
      const TestDef &test, const Case &c, FrameSize size);

  // The Case's frame size: its Test's hook applied to the run's size.
  CaseFrameSize frameSizeOf(const TestDef &test, const Case &c) const;
  // The Case's frame size, recorded in `result`; or empty, with `result`
  // skipped for the reason, when it isn't whole pixels at the run's size.
  std::optional<FrameSize> sizeOrSkip(
      const TestDef &test, const Case &c, CaseResult &result) const;

  // Record the device's advertised extensions in RunOptions::device, resolve
  // what Cases gate on (m_gatingExtensions: the renderer's own extensions when
  // it reported any) and the renderer's subtype, and resolve the requested
  // accumulation/denoise options against m_gatingExtensions. Accumulation is
  // gated off when ANARI_KHR_FRAME_ACCUMULATION is absent; denoise is applied
  // unconditionally (see RunOptions::denoise). Called once at the start of
  // generate()/run().
  void resolveCapabilities(const std::set<std::string> &deviceFeatures);

  // Query the selected renderer subtype's declared parameter types once (on
  // first use), so CLI overrides and a Test's optional parameters parse into
  // the device's expected type and the latter are set only when listed.
  // Populates m_rendererParamTypes; leaves it empty when introspection yields
  // nothing.
  const std::map<std::string, ANARIDataType> &rendererParamTypes();

  // `test` with the effect-check opt-outs that hold on this run's device
  // (`TestBuilder::noEffectCheckOn`) added to its `noEffectCheck`.
  TestDef forRunDevice(const TestDef &test) const;

  // Apply the CLI renderer parameter overrides to `renderer` (does not commit).
  // Each value parses into its device-declared type, falling back to inference
  // when the device does not report the parameter; a value that cannot be
  // parsed is skipped with a warning.
  void applyRendererParams(anari::Renderer renderer);

  // Set `params` (a Test's optional renderer parameters, already filtered to
  // the ones the renderer lists) on `renderer`; does not commit.
  void setRendererParams(
      anari::Renderer renderer, const std::vector<RendererParam> &params);

  // Why Case `c` is skipped for the required extensions absent from
  // m_gatingExtensions: the device's, or the renderer's (named by its subtype).
  std::string featureSkipReason(const TestDef &test, const Case &c) const;

  // Write a "missing required feature" skip sidecar for a Case, naming the
  // required features absent from m_gatingExtensions and which of them the
  // renderer lacks, and tally it.
  void writeFeatureSkip(
      const TestDef &test, const Case &c, RunSummary &summary);

  // A Case's result before it runs: its Test's and the run's identity, and
  // the Reference device the workdir records.
  CaseResult caseResult(const TestDef &test, const Case &c);

  // Publish a Case's complete result and tally its verdict. A publication
  // failure always tallies as failed, regardless of the intended verdict.
  void recordResult(const Case &c,
      const CaseResult &result,
      RunSummary &summary,
      const std::vector<ImageArtifact> &images = {});

  // Write a failed sidecar for a Case whose build/render threw, recording the
  // reason in `detail`, and tally it. Per-case crash isolation (ADR-0003): a
  // throwing case is contained so the run continues.
  void writeCaseFailure(const TestDef &test,
      const Case &c,
      const std::string &detail,
      RunSummary &summary);

  // Run a behavioral Test (TestDef::behaviorCheck set): one Case at a time,
  // feature-gating then invoking the check and writing its verdict + detail.
  void runBehaviorTest(
      const TestDef &test, const Filter &filter, RunSummary &summary);

  // Run one Case of a timed behavior Test through m_isolation and record how
  // it ended: the sidecar the isolated process wrote, or a failure if it
  // timed out, crashed or wrote none.
  void runIsolatedCase(const TestDef &test, const Case &c, RunSummary &summary);

  // Render the probe frame (probeDevice()) the first time it is called, and
  // return why the device can't render it, or empty. There is nothing to probe
  // without a device, and an isolated Case (RunOptions::onlyCase) runs after
  // its parent probed.
  const std::string &probeOnce();

  // Whether run() runs Case 'c' (RunOptions::onlyCase), and the filter
  // selects it.
  bool selected(const Case &c) const;
  bool selected(const Filter &filter, const TestDef &test, const Case &c) const;

  // Tally a result without publishing anything.
  static void tally(const CaseResult &result, RunSummary &summary);

  anari::Device m_device{nullptr};
  Workdir m_workdir;
  ArtifactPublisher m_artifacts;
  RunOptions m_options;
  std::shared_ptr<CaseIsolation> m_isolation;
  SelfCheckDefaults m_selfChecks;

  // What Cases gate on (RendererExtensions.h gatingExtensions()), and the
  // renderer subtype a skip names; set by resolveCapabilities().
  std::set<std::string> m_gatingExtensions;
  std::string m_rendererSubtype;
  uint32_t m_effectiveAccumulationFrames{1};
  // Whether the renderer lists KHR_FRAME_ACCUMULATION (the device, when the
  // renderer reports no list): the guard runs only then, and only when the
  // run accumulates more than one frame (notGuardedReason()).
  bool m_accumulationSupported{false};
  // Qualified ids of the Cases this Runner scored, so a later support render
  // doesn't overwrite their images.
  std::set<std::string> m_scored;
  bool m_denoiseEnabled{false};
  // coveredExtensions() of the Catalog being run, sorted; every sidecar
  // records it (schema v3).
  std::vector<std::string> m_catalogCovered;
  // Declared parameter types for the selected renderer subtype (name -> type),
  // resolved once from device introspection to type the CLI overrides.
  std::map<std::string, ANARIDataType> m_rendererParamTypes;
  bool m_rendererParamTypesResolved{false};
  // The Reference device the workdir records (Reference.h), read by run().
  std::optional<DeviceSpec> m_reference;
  // probeOnce()'s result, once probed.
  std::optional<std::string> m_deviceFailure;
};

} // namespace cts
} // namespace anari
