// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#include "Runner.h"
#include "BuildContext.h"
#include "DeviceFailure.h"
#include "DeviceProbe.h"
#include "Expansion.h"
#include "FrameFormats.h"
#include "FrameReadback.h"
#include "Metrics.h"
#include "Sidecar.h"
#include "Value.h"
#include "ViewBuilder.h"
#include "WorldBuilder.h"
// anari
#include "anari/frontend/anari_device_introspection.hpp"
// std
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace anari {
namespace cts {

namespace {

const char *channelFrameParam(Channel ch)
{
  switch (ch) {
  case Channel::Color:
    return "channel.color";
  case Channel::Depth:
    return "channel.depth";
  case Channel::Albedo:
    return "channel.albedo";
  case Channel::Normal:
    return "channel.normal";
  case Channel::PrimitiveId:
    return "channel.primitiveId";
  case Channel::ObjectId:
    return "channel.objectId";
  case Channel::InstanceId:
    return "channel.instanceId";
  }
  return "channel.color";
}

// Render one channel of an already-assembled world with a caller-owned camera
// and renderer. chFmt is the buffer format the Case requested for this channel
// (the comparison image is always 8-bit RGBA). depthScale deterministically
// maps depth to gray.
Image renderChannel(anari::Device d,
    anari::World world,
    anari::Camera camera,
    anari::Renderer renderer,
    Channel ch,
    uint32_t w,
    uint32_t h,
    float depthScale,
    ANARIDataType chFmt,
    uint32_t accumulationFrames)
{
  UniqueAnariObject<anari::Frame> frame(d, anari::newObject<anari::Frame>(d));
  const auto f = frame.get();
  anari::setParameter(d, f, "size", anari::math::vec<uint32_t, 2>(w, h));
  // The frame always carries a color buffer; only the channel being read needs
  // the Case's requested format, so non-color channels keep a plain color
  // buffer.
  anari::setParameter(d,
      f,
      "channel.color",
      ch == Channel::Color ? chFmt : ANARI_UFIXED8_RGBA_SRGB);
  if (ch != Channel::Color)
    anari::setParameter(d, f, channelFrameParam(ch), chFmt);
  anari::setParameter(d, f, "renderer", renderer);
  anari::setParameter(d, f, "camera", camera);
  anari::setParameter(d, f, "world", world);
  // Depth, normal, albedo, and id channels do not benefit from accumulation.
  const bool accumulate = accumulationFrames > 1 && ch == Channel::Color;
  if (accumulate)
    anari::setParameter(d, f, "accumulation", true);
  anari::commitParameters(d, f);

  // The frame is created fresh each call, so accumulation starts at sample 0;
  // rendering the same frame N times refines the image (matches
  // tests/frame.cpp's progressive-rendering check).
  if (accumulate) {
    for (uint32_t i = 0; i < accumulationFrames; ++i) {
      anari::render(d, f);
      anari::wait(d, f);
    }
  } else {
    anari::render(d, f);
    anari::wait(d, f);
  }

  auto readback = readFrameChannel(d, f, ch, w, h, depthScale);
  if (!readback)
    throw std::runtime_error(readback.detail);
  return std::move(readback.image);
}

// The default render camera: a perspective camera framing the world bounds.
anari::Camera defaultCamera(
    anari::Device d, const scenes::Bounds &bounds, uint32_t w, uint32_t h)
{
  const auto camDesc = cameraFromBounds(bounds);
  PerspectiveCameraOptions camOpts;
  if (h != 0)
    camOpts.aspect = static_cast<float>(w) / static_cast<float>(h);
  return makePerspectiveCamera(d, camDesc, camOpts);
}

// Create the selected renderer subtype and apply the runner's baseline. The
// caller lets a Test override parameters before committing it once.
anari::Renderer configuredRenderer(
    anari::Device d, const std::string &subtype, float ambientRadiance)
{
  UniqueAnariObject<anari::Renderer> renderer(
      d, anari::newObject<anari::Renderer>(d, subtype.c_str()));
  if (renderer)
    anari::setParameter(d, renderer.get(), "ambientRadiance", ambientRadiance);
  return renderer.release();
}

CaseResult baseResult(const TestDef &test,
    const Case &c,
    const DeviceSpec &device,
    const std::vector<std::string> &catalogCovered)
{
  CaseResult r;
  r.category = c.category;
  r.test = c.testName;
  r.description = test.description;
  r.caseId = c.id();
  // A fully self-checked Test has no Ground truth to name (ADR-0010).
  r.usesGroundTruth = test.usesGroundTruth();
  r.groundTruthReason = test.groundTruthReason;
  if (r.usesGroundTruth)
    r.groundTruthKey = c.groundTruthKey();
  r.device = device;
  r.requiredFeatures = test.requiredFeatures;
  r.coveredFeatures = test.coveredFeatures;
  r.catalogCoveredFeatures = catalogCovered;
  for (const auto &cv : c.values)
    r.axisValues.emplace_back(cv.axisName, anyToString(cv.value));
  return r;
}

RunSummary workdirFailureSummary(const Catalog &catalog, const Filter &filter)
{
  RunSummary summary;
  for (const TestDef *test : catalog.filter(filter)) {
    const int cases = static_cast<int>(expand(*test).size());
    summary.total += cases;
    summary.failed += cases;
  }
  return summary;
}

// Per-pixel absolute RGB difference between two equal-sized images, with opaque
// alpha so the result is viewable. Returns an invalid Image if the sizes differ
// (then the caller skips the debug images).
Image makeDiffImage(const Image &reference, const Image &candidate)
{
  if (!reference.valid() || reference.width != candidate.width
      || reference.height != candidate.height)
    return {};
  Image diff;
  diff.width = reference.width;
  diff.height = reference.height;
  diff.rgba.resize(reference.rgba.size());
  const size_t n = reference.pixelCount();
  for (size_t i = 0; i < n; ++i) {
    for (int c = 0; c < 3; ++c) {
      const int d =
          int(reference.rgba[i * 4 + c]) - int(candidate.rgba[i * 4 + c]);
      diff.rgba[i * 4 + c] = static_cast<uint8_t>(d < 0 ? -d : d);
    }
    diff.rgba[i * 4 + 3] = 255;
  }
  return diff;
}

// A black/white mask of a diff image: white where any RGB channel differs by
// more than `threshold` (0..255), black otherwise.
Image makeThresholdImage(const Image &diff, uint8_t threshold)
{
  Image out;
  out.width = diff.width;
  out.height = diff.height;
  out.rgba.resize(diff.rgba.size());
  const size_t n = diff.pixelCount();
  for (size_t i = 0; i < n; ++i) {
    const bool over = diff.rgba[i * 4 + 0] > threshold
        || diff.rgba[i * 4 + 1] > threshold || diff.rgba[i * 4 + 2] > threshold;
    const uint8_t v = over ? 255 : 0;
    out.rgba[i * 4 + 0] = v;
    out.rgba[i * 4 + 1] = v;
    out.rgba[i * 4 + 2] = v;
    out.rgba[i * 4 + 3] = 255;
  }
  return out;
}

// Diff intensity (0..255) above which the threshold mask marks a pixel: ~0.05
// of full range, matching the legacy debug-image cutoff.
constexpr uint8_t kDiffThreshold8 = 13;

} // namespace

Runner::Runner(anari::Device device, Workdir workdir, RunOptions options)
    : Runner(device, std::move(workdir), std::move(options), nullptr)
{}

Runner::Runner(anari::Device device,
    Workdir workdir,
    RunOptions options,
    std::shared_ptr<ArtifactWriter> artifactWriter)
    : m_device(device),
      m_workdir(std::move(workdir)),
      m_artifacts(m_workdir, std::move(artifactWriter)),
      m_options(std::move(options))
{}

const std::string &Runner::probeOnce()
{
  if (!m_deviceFailure) {
    m_deviceFailure = (!m_device || !m_options.onlyCase.empty())
        ? std::string()
        : probeDevice(
              m_device, m_options.device.renderer, m_options.deviceErrors);
  }
  return *m_deviceFailure;
}

bool Runner::selected(const Case &c) const
{
  return m_options.onlyCase.empty() || c.qualifiedId() == m_options.onlyCase;
}

bool Runner::selected(
    const Filter &filter, const TestDef &test, const Case &c) const
{
  return selected(c) && matches(filter, test, c);
}

void Runner::setCaseIsolation(std::shared_ptr<CaseIsolation> isolation)
{
  m_isolation = std::move(isolation);
}

void Runner::resolveCapabilities(const std::set<std::string> &features)
{
  m_effectiveAccumulationFrames =
      (m_options.accumulationFrames > 1
          && features.count("ANARI_KHR_FRAME_ACCUMULATION"))
      ? m_options.accumulationFrames
      : 1;
  m_denoiseEnabled = m_options.denoise;
  // Every sidecar records what the device advertised (schema v3), so the
  // report can tell "not advertised" from "advertised, but failed".
  m_options.device.extensions.assign(features.begin(), features.end());
  resolveRendererParamTypes();
}

void Runner::resolveRendererParamTypes()
{
  m_rendererParamTypes.clear();
  if (m_options.rendererParams.empty())
    return;

  introspection::QueryOptions qo;
  qo.typeFilter = "RENDERER";
  qo.includeParameters = true;
  const auto info = introspection::queryDeviceInfo(m_device, qo);
  for (const auto &obj : info.objects) {
    if (obj.type != ANARI_RENDERER || obj.subtype != m_options.device.renderer)
      continue;
    for (const auto &p : obj.parameters)
      m_rendererParamTypes[p.name] = p.type;
  }
}

void Runner::applyRendererParams(anari::Renderer renderer)
{
  for (const auto &param : m_options.rendererParams) {
    const auto it = m_rendererParamTypes.find(param.name);
    const ANARIDataType declared =
        it != m_rendererParamTypes.end() ? it->second : ANARI_UNKNOWN;
    const auto parsed = parseParamValue(declared, param.value);
    if (!parsed) {
      std::cerr << "warning: could not parse renderer parameter '" << param.name
                << "=" << param.value << "'; skipping\n";
      continue;
    }
    anariSetParameter(
        m_device, renderer, param.name.c_str(), parsed->type, parsed->data());
  }
}

CaseFrameSize Runner::frameSizeOf(const TestDef &test, const Case &c) const
{
  return resolveFrameSize(
      test, c, FrameSize{m_options.width, m_options.height}, m_device);
}

Runner::SceneObjects Runner::buildScene(
    const TestDef &test, const Case &c, FrameSize size)
{
  SceneObjects scene;
  scene.size = size;
  if (!test.build)
    return scene;

  BuildContext ctx(m_device);
  for (const auto &cv : c.values)
    ctx.set(cv.axisName, cv.value);

  scene.world = UniqueAnariObject<anari::World>(m_device, test.build(ctx));
  if (!scene.world)
    return scene;

  scene.bounds = worldBounds(m_device, scene.world.get());
  scene.camera = UniqueAnariObject<anari::Camera>(m_device,
      test.cameraBuild
          ? test.cameraBuild(ctx, scene.bounds)
          : defaultCamera(m_device, scene.bounds, size.width, size.height));
  scene.renderer = UniqueAnariObject<anari::Renderer>(m_device,
      configuredRenderer(
          m_device, m_options.device.renderer, m_options.ambientRadiance));
  if (scene.renderer) {
    applyRendererParams(scene.renderer.get());
    if (test.rendererConfig)
      test.rendererConfig(ctx, scene.renderer.get());
    if (m_denoiseEnabled)
      anari::setParameter(m_device, scene.renderer.get(), "denoise", true);
    anari::commitParameters(m_device, scene.renderer.get());
  }
  return scene;
}

void Runner::writeFeatureSkip(const TestDef &test,
    const Case &c,
    const std::set<std::string> &features,
    RunSummary &summary)
{
  CaseResult result = baseResult(test, c, m_options.device, m_catalogCovered);
  result.verdict = Verdict::Skipped;
  result.missingFeatures = missingFeatures(test, features);
  result.skipReason = "device is missing a required feature:";
  for (size_t i = 0; i < result.missingFeatures.size(); ++i)
    result.skipReason += (i ? ", " : " ") + result.missingFeatures[i];
  recordResult(c, result, summary);
}

void Runner::recordResult(const Case &c,
    const CaseResult &result,
    RunSummary &summary,
    const std::vector<ImageArtifact> &images)
{
  if (!m_artifacts.publishResult(c, result, images)) {
    summary.failed++;
    return;
  }
  tally(result, summary);
}

void Runner::tally(const CaseResult &result, RunSummary &summary)
{
  switch (result.verdict) {
  case Verdict::Passed:
    summary.passed++;
    if (result.passedWithoutGroundTruth())
      summary.passedWithoutGroundTruth++;
    break;
  case Verdict::Failed:
    summary.failed++;
    break;
  case Verdict::Skipped:
    summary.skipped++;
    break;
  }
}

void Runner::writeCaseFailure(const TestDef &test,
    const Case &c,
    const std::string &detail,
    RunSummary &summary)
{
  CaseResult result = baseResult(test, c, m_options.device, m_catalogCovered);
  result.verdict = Verdict::Failed;
  result.detail = detail;
  recordResult(c, result, summary);
}

std::vector<Image> Runner::renderCase(const TestDef &test, const Case &c)
{
  const CaseFrameSize size = frameSizeOf(test, c);
  if (!size.resolved())
    return {};
  return renderImages(test, c, size.size);
}

std::vector<Image> Runner::renderImages(
    const TestDef &test, const Case &c, FrameSize size)
{
  SceneObjects scene = buildScene(test, c, size);
  if (!scene.valid())
    return {};

  // Deterministic depth scale derived from the (shared) scene bounds: the
  // default camera sits within one bounds diagonal of the center, so 2x is a
  // conservative bound that covers the whole near..far range.
  const float depthScale =
      2.0f * anari::math::length(scene.bounds[1] - scene.bounds[0]);

  std::vector<Image> images;
  images.reserve(test.channels.size());
  for (Channel ch : test.channels) {
    const ANARIDataType chFmt = caseChannelFormat(c, ch);
    images.push_back(renderChannel(m_device,
        scene.world.get(),
        scene.camera.get(),
        scene.renderer.get(),
        ch,
        size.width,
        size.height,
        depthScale,
        chFmt,
        m_effectiveAccumulationFrames));
  }
  return images;
}

RunSummary Runner::generate(const Catalog &catalog,
    const Filter &filter,
    const std::set<std::string> &referenceFeatures)
{
  resolveCapabilities(referenceFeatures);
  if (!m_workdir.writeGitignore())
    return workdirFailureSummary(catalog, filter);
  RunSummary summary;
  summary.deviceFailure = probeOnce();
  if (!summary.deviceFailure.empty())
    return summary;
  for (const TestDef *test : catalog.filter(filter))
    generateTest(*test, filter, referenceFeatures, summary);
  return summary;
}

RunSummary Runner::run(const Catalog &catalog,
    const Filter &filter,
    const std::set<std::string> &candidateFeatures)
{
  resolveCapabilities(candidateFeatures);
  // Every sidecar records what the whole Catalog covers, not just the
  // filtered slice, so the report can tell an extension with no Test from one
  // this run didn't select, from the results tree alone (ADR-0008).
  const auto covered = coveredExtensions(catalog);
  m_catalogCovered.assign(covered.begin(), covered.end());
  if (!m_workdir.writeGitignore())
    return workdirFailureSummary(catalog, filter);
  RunSummary summary;
  // A device that can't render is one run-level failure, not one per Case.
  summary.deviceFailure = probeOnce();
  if (!summary.deviceFailure.empty()) {
    if (!writeDeviceFailure(
            m_workdir, {m_options.device, summary.deviceFailure}))
      std::cerr << "warning: could not write "
                << m_workdir.runRecordPath().string() << "\n";
    return summary;
  }
  if (m_options.onlyCase.empty())
    clearDeviceFailure(m_workdir);
  for (const TestDef *test : catalog.filter(filter)) {
    if (test->behaviorCheck)
      runBehaviorTest(*test, filter, candidateFeatures, summary);
    else
      runRenderTest(*test, filter, candidateFeatures, summary);
  }
  return summary;
}

Runner::CaseOutcome Runner::runCase(
    const TestDef &test, const Case &c, bool renderWithoutGroundTruth)
{
  CaseOutcome out;
  CaseResult &result = out.result;
  result = baseResult(test, c, m_options.device, m_catalogCovered);

  // Per-case crash isolation (ADR-0003): a throwing build helper or fatal
  // fails this Case and the run continues. Handles built mid-case are
  // released by buildScene/renderCase as they unwind.
  auto fail = [&](const std::string &detail) {
    result.verdict = Verdict::Failed;
    result.detail = detail;
    result.channels.clear();
    result.resultImages.clear();
    result.groundTruthEvaluated = false;
    out.artifacts.clear();
    out.render = {};
    out.render.state = CaseRender::State::Failed;
    out.render.error = detail;
  };

  try {
    // The Case's frame size; one that isn't whole pixels at the run's size
    // skips the Case (and fails the relations that need it).
    const CaseFrameSize size = frameSizeOf(test, c);
    if (!size.resolved()) {
      result.verdict = Verdict::Skipped;
      result.skipReason = size.skipReason;
      out.render.state = CaseRender::State::Skipped;
      out.render.error = size.skipReason;
      return out;
    }
    result.frameWidth = size.size.width;
    result.frameHeight = size.size.height;

    // Ground truth for every channel, when the Test compares against it.
    bool haveGroundTruth = test.usesGroundTruth();
    std::vector<Image> groundTruth;
    for (Channel ch : test.channels) {
      if (!haveGroundTruth)
        break;
      auto gt = loadPNG(m_workdir.groundTruthImagePath(c, ch).string());
      if (!gt.valid()) {
        haveGroundTruth = false;
        groundTruth.clear();
        break;
      }
      groundTruth.push_back(std::move(gt));
    }
    if (!haveGroundTruth && test.usesGroundTruth()
        && !renderWithoutGroundTruth) {
      // Nothing would be evaluated: don't render.
      result.verdict = Verdict::Skipped;
      result.skipReason = kNoGroundTruthReason;
      return out;
    }

    const auto start = std::chrono::steady_clock::now();
    auto images = renderImages(test, c, size.size);
    const auto end = std::chrono::steady_clock::now();
    result.durationMs =
        std::chrono::duration<double, std::milli>(end - start).count();

    if (images.size() != test.channels.size()) {
      fail("render produced no image");
      return out;
    }

    bool allPassed = true;
    out.artifacts.reserve(test.channels.size() * 3);
    for (size_t i = 0; i < test.channels.size(); ++i) {
      const Channel ch = test.channels[i];
      out.artifacts.push_back({m_workdir.resultImagePath(c, ch), images[i]});
      result.resultImages.emplace_back(
          ch, m_workdir.relativeToRoot(m_workdir.resultImagePath(c, ch)));
      if (!haveGroundTruth)
        continue;

      ChannelResult cr;
      cr.channel = ch;
      const double ssimThreshold =
          test.thresholdFor(ch, "ssim", m_options.ssimThreshold);
      const double psnrThreshold =
          test.thresholdFor(ch, "psnr", m_options.psnrThreshold);
      const double ssimScore = ssim(groundTruth[i], images[i]);
      const double psnrScore = psnr(groundTruth[i], images[i]);
      cr.metrics = {{"ssim", ssimScore}, {"psnr", psnrScore}};
      cr.thresholds = {{"ssim", ssimThreshold}, {"psnr", psnrThreshold}};
      cr.passed = metricPassed(ssimScore, ssimThreshold)
          && metricPassed(psnrScore, psnrThreshold);
      cr.resultImage =
          m_workdir.relativeToRoot(m_workdir.resultImagePath(c, ch));
      cr.groundTruthImage =
          m_workdir.relativeToRoot(m_workdir.groundTruthImagePath(c, ch));

      // Debug images alongside the result: the absolute per-pixel
      // difference and a thresholded mask of it, to localize a mismatch.
      const Image diffImg = makeDiffImage(groundTruth[i], images[i]);
      if (diffImg.valid()) {
        out.artifacts.push_back({m_workdir.diffImagePath(c, ch), diffImg});
        out.artifacts.push_back({m_workdir.thresholdImagePath(c, ch),
            makeThresholdImage(diffImg, kDiffThreshold8)});
        cr.diffImage = m_workdir.relativeToRoot(m_workdir.diffImagePath(c, ch));
        cr.thresholdImage =
            m_workdir.relativeToRoot(m_workdir.thresholdImagePath(c, ch));
      }

      allPassed = allPassed && cr.passed;
      result.channels.push_back(std::move(cr));
    }
    result.groundTruthEvaluated = haveGroundTruth;

    // Record the effective render settings additively in the existing
    // detail field (no schema bump). generate writes no sidecars
    // (ADR-0005), so this note is run-only; it makes a mixed-capability
    // diff (e.g. an accumulating candidate vs. a single-render ground
    // truth) interpretable.
    std::string note =
        "accumulation=" + std::to_string(m_effectiveAccumulationFrames)
        + ", denoise=" + (m_denoiseEnabled ? "on" : "off");
    result.detail = result.detail.empty() ? note : result.detail + "; " + note;

    // Provisional until the relational pass (settleVerdict).
    result.verdict = allPassed ? Verdict::Passed : Verdict::Failed;
    out.render.state = CaseRender::State::Rendered;
    out.render.images = std::move(images);
  } catch (const std::exception &e) {
    fail(std::string("exception during run: ") + e.what());
  } catch (...) {
    fail("unknown exception during run");
  }
  return out;
}

void Runner::runRenderTest(const TestDef &test,
    const Filter &filter,
    const std::set<std::string> &candidateFeatures,
    RunSummary &summary)
{
  const std::vector<Case> cases = expand(test);
  std::vector<bool> chosen(cases.size(), false);
  bool any = false;
  for (size_t i = 0; i < cases.size(); ++i)
    any = (chosen[i] = selected(filter, test, cases[i])) || any;
  if (!any)
    return;

  if (!isSupported(test, candidateFeatures)) {
    for (size_t i = 0; i < cases.size(); ++i) {
      if (!chosen[i])
        continue;
      summary.total++;
      writeFeatureSkip(test, cases[i], candidateFeatures, summary);
    }
    return;
  }

  // Phase 1, per Case: the selected Cases and the partners their relations
  // need (support renders).
  const bool relations = hasRelations(test);
  std::vector<bool> toRender = chosen;
  if (relations) {
    try {
      toRender = renderSet(test, cases, chosen);
    } catch (const std::invalid_argument &e) {
      // The Cases can't be grouped by their values (caseValueIndices()).
      for (size_t i = 0; i < cases.size(); ++i) {
        if (!chosen[i])
          continue;
        summary.total++;
        writeCaseFailure(test,
            cases[i],
            std::string("cannot relate the Test's Cases: ") + e.what(),
            summary);
      }
      return;
    }
  }
  std::vector<CaseOutcome> outcomes(cases.size());
  std::vector<CaseRender> renders(cases.size());
  for (size_t i = 0; i < cases.size(); ++i) {
    if (!toRender[i])
      continue;
    outcomes[i] = runCase(test, cases[i], relations);
    renders[i] = outcomes[i].render;
  }

  // Phase 2, per Test: the pure relational pass over the Candidate's images,
  // merged into the selected Cases' Sidecars.
  std::vector<bool> participated(cases.size(), false);
  for (auto &rel : evaluateRelations(test, cases, renders, m_selfChecks)) {
    if (rel.assertion.evaluated)
      for (size_t p : rel.partners)
        participated[p] = true;
    if (!chosen[rel.caseIndex])
      continue;
    CaseOutcome &outcome = outcomes[rel.caseIndex];
    const Case &c = cases[rel.caseIndex];
    const size_t index = outcome.result.assertions.size();
    Assertion a = std::move(rel.assertion);
    if (rel.channelIndex >= 0 && !rel.partners.empty()) {
      const Channel ch = test.channels[size_t(rel.channelIndex)];
      a.partnerImage = m_workdir.relativeToRoot(
          m_workdir.resultImagePath(cases[rel.partners.front()], ch));
    }
    if (rel.mask.valid()) {
      const auto path = m_workdir.assertionMaskPath(c, a.kind, index);
      a.maskImage = m_workdir.relativeToRoot(path);
      outcome.artifacts.push_back({path, std::move(rel.mask)});
    }
    // A derived expectation's expected image (a Variant's is its partner's,
    // partnerImage).
    if (rel.expected.valid()) {
      const auto path = m_workdir.expectedImagePath(c, a.kind, index);
      a.expectedImage = m_workdir.relativeToRoot(path);
      outcome.artifacts.push_back({path, std::move(rel.expected)});
    }
    if (rel.expectedMask.valid()) {
      const auto path = m_workdir.expectedMaskPath(c, a.kind, index);
      a.expectedMaskImage = m_workdir.relativeToRoot(path);
      outcome.artifacts.push_back({path, std::move(rel.expectedMask)});
    }
    outcome.result.assertions.push_back(std::move(a));
  }

  for (size_t i = 0; i < cases.size(); ++i) {
    if (chosen[i]) {
      summary.total++;
      settleVerdict(outcomes[i].result, participated[i]);
      recordResult(
          cases[i], outcomes[i].result, summary, outcomes[i].artifacts);
    } else if (toRender[i] && renders[i].state == CaseRender::State::Rendered) {
      // A support render: its images are written so a report can show the
      // partner; it is neither scored nor counted.
      std::vector<ImageArtifact> images;
      for (size_t k = 0; k < test.channels.size(); ++k)
        images.push_back({m_workdir.resultImagePath(cases[i], test.channels[k]),
            renders[i].images[k]});
      if (!m_artifacts.publishImages(images))
        std::cerr << "warning: could not write the support render of "
                  << cases[i].qualifiedId() << "\n";
    }
  }
}

void Runner::generateTest(const TestDef &test,
    const Filter &filter,
    const std::set<std::string> &referenceFeatures,
    RunSummary &summary)
{
  const std::vector<Case> cases = expand(test);
  std::vector<bool> chosen(cases.size(), false);
  int count = 0;
  for (size_t i = 0; i < cases.size(); ++i)
    count += (chosen[i] = matches(filter, test, cases[i])) ? 1 : 0;
  summary.total += count;
  // Behavioural and fully self-checked Tests have no Ground truth.
  if (!test.usesGroundTruth() || !isSupported(test, referenceFeatures)) {
    summary.skipped += count;
    return;
  }

  // A Ground-truth file shared by Variants is written from the Case holding
  // every Variant axis's first value, so it doesn't depend on which Variant
  // rendered last (ADR-0010, decision 9).
  std::vector<std::vector<size_t>> indices;
  try {
    indices = caseValueIndices(test, cases);
  } catch (const std::invalid_argument &e) {
    std::cerr << "error: cannot relate the Cases of " << test.id() << ": "
              << e.what() << "\n";
    summary.failed += count;
    return;
  }
  auto writerOf = [&](size_t i) {
    auto key = indices[i];
    for (size_t a = 0; a < test.axes.size(); ++a)
      if (test.axes[a].kind == AxisKind::Variant)
        key[a] = 0;
    for (size_t k = 0; k < cases.size(); ++k)
      if (indices[k] == key)
        return k;
    return i;
  };
  // Render the selected Cases, the writers of their Ground truth, and the
  // Cases their equivalences compare against.
  std::vector<bool> toRender = chosen;
  for (const auto &group : relationGroups(test, cases)) {
    if (test.axes[group.axis].kind != AxisKind::Variant)
      continue;
    for (size_t k = 1; k < group.cases.size(); ++k)
      if (chosen[group.cases[k]])
        toRender[group.cases.front()] = true;
  }
  for (size_t i = 0; i < cases.size(); ++i)
    if (chosen[i])
      toRender[writerOf(i)] = true;

  std::vector<CaseRender> renders(cases.size());
  for (size_t i = 0; i < cases.size(); ++i) {
    if (!toRender[i])
      continue;
    const CaseFrameSize size = frameSizeOf(test, cases[i]);
    if (!size.resolved()) {
      renders[i].state = CaseRender::State::Skipped;
      renders[i].error = size.skipReason;
      continue;
    }
    // A throwing build helper (or fatal) loses only this Case's Ground
    // truth; the rest of the generate run continues (ADR-0003).
    try {
      auto images = renderImages(test, cases[i], size.size);
      if (images.size() == test.channels.size()) {
        renders[i].state = CaseRender::State::Rendered;
        renders[i].images = std::move(images);
      } else {
        renders[i].state = CaseRender::State::Failed;
        renders[i].error = "render produced no image";
      }
    } catch (const std::exception &e) {
      renders[i].state = CaseRender::State::Failed;
      renders[i].error = e.what();
    } catch (...) {
      renders[i].state = CaseRender::State::Failed;
      renders[i].error = "unknown exception";
    }
  }

  // Write each Ground-truth file once, from its writer.
  std::vector<int> written(cases.size(), -1); // -1 unknown, 0 failed, 1 ok
  for (size_t i = 0; i < cases.size(); ++i) {
    if (!chosen[i])
      continue;
    const size_t w = writerOf(i);
    if (renders[w].state == CaseRender::State::Skipped) {
      summary.skipped++;
      continue;
    }
    if (written[w] < 0) {
      bool ok = renders[w].state == CaseRender::State::Rendered;
      if (ok) {
        std::vector<ImageArtifact> artifacts;
        for (size_t k = 0; k < test.channels.size(); ++k)
          artifacts.push_back(
              {m_workdir.groundTruthImagePath(cases[w], test.channels[k]),
                  renders[w].images[k]});
        ok = m_artifacts.publishGroundTruth(artifacts);
      }
      written[w] = ok ? 1 : 0;
    }
    if (written[w] > 0)
      summary.passed++;
    else
      summary.failed++;
  }

  // The Reference device's own Variants must agree. (Derived expectations
  // on Permutation axes are the Candidate's business: their siblings aren't
  // rendered here.)
  for (const auto &rel :
      evaluateRelations(test, cases, renders, m_selfChecks)) {
    const bool variant = test.isVariant(rel.assertion.axis);
    const bool partnersRendered =
        std::all_of(rel.partners.begin(), rel.partners.end(), [&](size_t p) {
          return renders[p].state == CaseRender::State::Rendered;
        });
    if (rel.assertion.kind != kEquivalenceAssertion || !variant
        || rel.assertion.passed || !chosen[rel.caseIndex] || !partnersRendered)
      continue;
    summary.variantDisagreements++;
    std::cerr << "warning: the Reference device's Variants disagree: "
              << cases[rel.caseIndex].qualifiedId() << " ("
              << rel.assertion.axis << "): " << rel.assertion.detail << "\n";
  }
}

void Runner::runBehaviorTest(const TestDef &test,
    const Filter &filter,
    const std::set<std::string> &candidateFeatures,
    RunSummary &summary)
{
  for (const Case &c : expand(test)) {
    if (!selected(filter, test, c))
      continue;
    summary.total++;

    if (!isSupported(test, candidateFeatures)) {
      writeFeatureSkip(test, c, candidateFeatures, summary);
      continue;
    }

    if (test.timeout.count() > 0 && m_isolation) {
      runIsolatedCase(test, c, summary);
      continue;
    }

    // Per-case crash isolation (ADR-0003): a throwing build hook or behavior
    // check becomes a failed sidecar and the run continues; SceneObjects owns
    // every handle and releases partial or complete builds on scope exit.
    try {
      CaseResult result =
          baseResult(test, c, m_options.device, m_catalogCovered);

      const CaseFrameSize size = frameSizeOf(test, c);
      if (!size.resolved()) {
        result.verdict = Verdict::Skipped;
        result.skipReason = size.skipReason;
        recordResult(c, result, summary);
        continue;
      }
      result.frameWidth = size.size.width;
      result.frameHeight = size.size.height;

      SceneObjects scene = buildScene(test, c, size.size);
      if (!scene.valid()) {
        result.verdict = Verdict::Failed;
        result.detail = "world/camera/renderer build failed";
        recordResult(c, result, summary);
        continue;
      }

      const DeviceErrorLog *errors =
          test.failOnDeviceErrors ? m_options.deviceErrors : nullptr;
      const size_t errorMark = errors ? errors->size() : 0;
      const auto start = std::chrono::steady_clock::now();
      const BehaviorResult br = test.behaviorCheck(m_device,
          scene.world.get(),
          scene.camera.get(),
          scene.renderer.get(),
          scene.size.width,
          scene.size.height);
      const auto end = std::chrono::steady_clock::now();
      result.durationMs =
          std::chrono::duration<double, std::milli>(end - start).count();
      result.verdict = br.passed ? Verdict::Passed : Verdict::Failed;
      result.detail = br.detail;
      if (errors) {
        const auto reported = errors->since(errorMark);
        if (!reported.empty()) {
          result.verdict = Verdict::Failed;
          result.detail += "; the device reported "
              + std::to_string(reported.size())
              + " error(s), the first: " + reported.front();
        }
      }
      recordResult(c, result, summary);
    } catch (const std::exception &e) {
      writeCaseFailure(test,
          c,
          std::string("exception during behavior test: ") + e.what(),
          summary);
    } catch (...) {
      writeCaseFailure(
          test, c, "unknown exception during behavior test", summary);
    }
  }
}

void Runner::runIsolatedCase(
    const TestDef &test, const Case &c, RunSummary &summary)
{
  // The isolated process writes the Case's sidecar; one left by an earlier run
  // must not stand in for it.
  const auto sidecar = m_workdir.sidecarPath(c);
  std::error_code ec;
  std::filesystem::remove(sidecar, ec);

  const auto start = std::chrono::steady_clock::now();
  const ProcessResult ended = m_isolation->run(c, test.timeout);
  const auto end = std::chrono::steady_clock::now();

  CaseResult written;
  const bool haveResult = readSidecar(sidecar, written);
  if (ended.status == ProcessResult::Status::Exited && ended.exitCode == 0
      && haveResult) {
    tally(written, summary);
    return;
  }

  std::string why;
  switch (ended.status) {
  case ProcessResult::Status::TimedOut:
    why = "timed out after " + durationText(test.timeout)
        + ": killed the isolated process (the device hung)";
    break;
  case ProcessResult::Status::NotStarted:
    why = "could not start the isolated process: " + ended.detail;
    break;
  case ProcessResult::Status::Crashed:
    why = "the isolated process crashed: " + ended.detail;
    break;
  case ProcessResult::Status::Exited:
    why = ended.exitCode == 0
        ? "the isolated process exited without writing a result"
        : "the isolated process " + ended.detail;
    break;
  }

  CaseResult result = haveResult
      ? written
      : baseResult(test, c, m_options.device, m_catalogCovered);
  if (haveResult) {
    why += std::string("; before that, its check had ")
        + verdictName(written.verdict)
        + (written.detail.empty() ? "" : ": " + written.detail);
  } else {
    result.durationMs =
        std::chrono::duration<double, std::milli>(end - start).count();
  }
  result.verdict = Verdict::Failed;
  result.detail = why;
  recordResult(c, result, summary);
}

} // namespace cts
} // namespace anari
