// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Case.h"
#include "Channel.h"
// std
#include <filesystem>
#include <string>

namespace anari {
namespace cts {

// The single root directory a run operates under, with the conventional
// subdirs results/, ground_truth/, and assets/. Files mirror the catalog
// hierarchy: <sub>/<category>/<test>/<leaf>. Result files are keyed by the
// Case id (unique); ground-truth files by the Case's ground-truth key (shared
// by variants). All accessors return absolute paths and create no directories.
struct Workdir
{
  explicit Workdir(std::filesystem::path root);

  const std::filesystem::path &root() const;
  std::filesystem::path resultsDir() const;
  std::filesystem::path groundTruthDir() const;
  std::filesystem::path assetsDir() const;

  // results/<category>/<test>/<caseId>.json
  std::filesystem::path sidecarPath(const Case &c) const;
  // results/<category>/<test>/<caseId>.<channel>.png
  std::filesystem::path resultImagePath(const Case &c, Channel channel) const;
  // results/<category>/<test>/<caseId>.<channel>.diff.png (debug image)
  std::filesystem::path diffImagePath(const Case &c, Channel channel) const;
  // results/<category>/<test>/<caseId>.<channel>.threshold.png (debug image)
  std::filesystem::path thresholdImagePath(
      const Case &c, Channel channel) const;
  // results/<category>/<test>/<caseId>.<kind>-<index>.mask.png: the changed
  // pixels of the Case's index'th assertion (a failed self-check, ADR-0010).
  std::filesystem::path assertionMaskPath(
      const Case &c, const std::string &kind, size_t index) const;
  // results/<category>/<test>/<caseId>.<kind>-<index>.expected.png and
  // .expected-mask.png: the image (and mask) a derived expectation required
  // of the Case's index'th assertion (ADR-0010).
  std::filesystem::path expectedImagePath(
      const Case &c, const std::string &kind, size_t index) const;
  std::filesystem::path expectedMaskPath(
      const Case &c, const std::string &kind, size_t index) const;
  // ground_truth/<category>/<test>/<gtKey>.<channel>.png
  std::filesystem::path groundTruthImagePath(
      const Case &c, Channel channel) const;

  // results/run.json: the run-level record of a device that could not render
  // (DeviceFailure.h). Not a Sidecar.
  std::filesystem::path runRecordPath() const;

  // ground_truth/reference.json: the Reference device the last `generate`
  // used (Reference.h).
  std::filesystem::path referenceRecordPath() const;

  // A path expressed relative to the workdir root (for storing in sidecars).
  std::string relativeToRoot(const std::filesystem::path &p) const;

  // Create the root and drop a ".gitignore" of "*" into it, so the whole
  // workdir (generated ground truth, results, assets) is untracked wherever it
  // lives (ADR-0005). Idempotent; returns false on a filesystem error.
  bool writeGitignore() const;

 private:
  std::filesystem::path m_root;
};

} // namespace cts
} // namespace anari
