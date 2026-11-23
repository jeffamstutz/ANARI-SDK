// Copyright 2021-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// The camera's optics: depth of field (KHR_CAMERA_DEPTH_OF_FIELD) and stereo
// (KHR_CAMERA_STEREO), both on the perspective camera. Every check is a
// self-check (ADR-0010, 51-54).
//
// The spec gives each extension two parameters and a line of description, no
// model. So the checks hold under every reasonable reading: a thin lens keeps
// the plane at `focusDistance` sharp and blurs a plane away from it, however
// it samples its aperture and in whatever unit; an eye `interpupillaryDistance`
// / 2 to the camera's left or right sees a nearer subject offset from a
// farther one by a disparity that doesn't depend on the convergence (parallel,
// off-axis or toed in), so the Tests look at that offset rather than at where
// either subject lands. Each eye's half of a `sideBySide` or `topBottom` frame
// is judged the same way, as the spec doesn't say how an eye's image is fitted
// to its half.

#include "../BuildContext.h"
#include "../ExactCheck.h"
#include "../LightBuilder.h"
#include "../ParameterBinding.h"
#include "../SurfaceBuilder.h"
#include "../TestBuilder.h"
#include "../WorldBuilder.h"
#include "Categories.h"
#include "OpticsScene.h"
// std
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace anari {
namespace cts {

namespace {

using anari::math::float2;
using anari::math::float3;
using V = std::vector<Any>;

constexpr const char *kDepthOfField = "ANARI_KHR_CAMERA_DEPTH_OF_FIELD";
constexpr const char *kStereo = "ANARI_KHR_CAMERA_STEREO";

// The scenes /////////////////////////////////////////////////////////////////

// Two triangles per rectangle [x0, x1] x [y0, y1] in the plane z.
void appendRect(
    std::vector<float3> &v, float x0, float y0, float x1, float y1, float z)
{
  const float3 a(x0, y0, z), b(x1, y0, z), c(x1, y1, z), e(x0, y1, z);
  v.insert(v.end(), {a, b, c, a, c, e});
}

// A matte surface of `color` made of the triangles `vertices` (no index).
anari::Surface triangleSurface(
    anari::Device d, const std::vector<float3> &vertices, float3 color)
{
  auto geom = anari::newObject<anari::Geometry>(d, "triangle");
  anari::setParameterArray1D(
      d, geom, "vertex.position", vertices.data(), vertices.size());
  anari::commitParameters(d, geom);
  auto mat = makeMatteMaterial(d, color);
  auto surface = makeSurface(d, geom, mat);
  anari::release(d, geom);
  anari::release(d, mat);
  return surface;
}

// The surfaces, lit from the camera so a plate is evenly lit.
anari::World platesWorld(
    anari::Device d, const std::vector<anari::Surface> &surfaces)
{
  auto light = makeDirectionalLight(d, float3(0.f, 0.f, -1.f));
  WorldContents wc;
  wc.surfaces = surfaces;
  wc.lights = {light};
  auto world = assembleWorld(d, wc);
  for (auto s : surfaces)
    anari::release(d, s);
  anari::release(d, light);
  return world;
}

// The depth-of-field scene: each plate a bright square in a dark border. The
// square is a mid grey, so a device's output doesn't clip it and its blurred
// edges keep their width.
anari::World focusWorld(BuildContext &ctx)
{
  std::vector<float3> bright, dark;
  for (const auto &p : optics::kFocusPlates) {
    const float3 c = p.centre;
    const float h = p.half, o = p.outer;
    appendRect(bright, c.x - h, c.y - h, c.x + h, c.y + h, c.z);
    appendRect(dark, c.x - o, c.y - o, c.x + o, c.y - h, c.z);
    appendRect(dark, c.x - o, c.y + h, c.x + o, c.y + o, c.z);
    appendRect(dark, c.x - o, c.y - h, c.x - h, c.y + h, c.z);
    appendRect(dark, c.x + h, c.y - h, c.x + o, c.y + h, c.z);
  }
  auto d = ctx.device();
  return platesWorld(d,
      {triangleSurface(d, bright, float3(0.4f)),
          triangleSurface(d, dark, float3(0.f))});
}

// The stereo scene: a red near plate above a blue far one.
anari::World stereoWorld(BuildContext &ctx)
{
  auto d = ctx.device();
  std::vector<anari::Surface> surfaces;
  for (const auto *p : {&optics::kStereoNear, &optics::kStereoFar}) {
    std::vector<float3> v;
    const float3 c = p->centre;
    appendRect(
        v, c.x - p->half, c.y - p->half, c.x + p->half, c.y + p->half, c.z);
    surfaces.push_back(triangleSurface(d,
        v,
        p == &optics::kStereoNear ? optics::kStereoNearColor
                                  : optics::kStereoFarColor));
  }
  return platesWorld(d, surfaces);
}

// Set the Case's value of `name` on the camera, unless it is unset.
void setCameraParam(BuildContext &ctx, anari::Camera c, const char *name)
{
  const Any v = ctx.value(name);
  if (v.valid())
    applyParameterValue(ctx.device(), c, name, v.raw());
}

// Depth of field /////////////////////////////////////////////////////////////

// A `lens` axis value: the camera's aperture and focus (each unset when
// empty) and which plates (near, far) it must show sharp.
struct Lens
{
  const char *name;
  std::optional<float> apertureRadius;
  std::optional<float> focusDistance;
  std::array<bool, 2> sharp;
};

// The first is the Test's baseline, which the convergence guard renders: one
// that samples the aperture.
const std::array<Lens, 6> kLenses = {{
    {"focus_near", optics::kApertureRadius, optics::kNearDepth, {true, false}},
    {"focus_far", optics::kApertureRadius, optics::kFarDepth, {false, true}},
    {"focus_one",
        optics::kDefaultFocusApertureRadius,
        optics::kDefaultFocusDistance,
        {false, false}},
    {"focus_default",
        optics::kDefaultFocusApertureRadius,
        std::nullopt,
        {false, false}},
    {"pinhole", std::nullopt, std::nullopt, {true, true}},
    {"aperture_zero", 0.f, optics::kFarDepth, {true, true}},
}};

const Lens *lensNamed(const std::string &name)
{
  for (const auto &l : kLenses)
    if (name == l.name)
      return &l;
  return nullptr;
}

anari::Camera focusCamera(BuildContext &ctx)
{
  auto d = ctx.device();
  auto camera = ctx.newCamera(optics::camera());
  if (const Lens *lens = lensNamed(ctx.getString("lens", ""))) {
    if (lens->apertureRadius)
      anari::setParameter(d, camera, "apertureRadius", *lens->apertureRadius);
    if (lens->focusDistance)
      anari::setParameter(d, camera, "focusDistance", *lens->focusDistance);
  }
  anari::commitParameters(d, camera);
  return camera;
}

// A sharp edge rises from 10 % to 90 % of the way from the border to the
// bright square within this many pixels (antialiasing alone spreads it over
// about one).
constexpr double kSharpRise = 2.5;
// A blurred edge rises over at least this share of the bright square's half
// extent (0.2: 4.4 pixels at 256; a thin lens of the Test's aperture spreads
// it over about 10).
constexpr double kBlurredRise = 0.2;
// The bright square's centre, halfway between where its two edges are
// halfway risen, is within this many pixels of where the pinhole puts it: a
// thin lens blurs a point around its pinhole image, so the blurred square
// stays symmetric about its centre whatever the device's output transfer
// (sRGB, tone mapping or clipping move each edge's halfway point, by the
// same amount either way).
constexpr double kCentrePlacement = 1.5;
// The bright square is this many 8-bit levels above its border, or the edge
// isn't there to measure.
constexpr double kMinEdgeContrast = 40.0;

// One vertical edge of a bright square, measured across: the mean brightness
// of each column over the square's middle rows, from the dark side to the
// bright one.
struct EdgeMeasure
{
  double contrast{0.0}; // levels from the dark plateau to the bright one
  double rise{0.0}; // pixels from 10 % to 90 % of the way
  double midpoint{0.0}; // pixels from the edge to where it is 50 % of the way
};

// The edge at column position `edgeX` (continuous pixels), dark on the left
// when `darkLeft`, of a square `halfPx` pixels in half extent centred on row
// `centreY`. The profile spans 0.9 half extents either side of the edge (inside
// the square and its border); each end's fifth is its plateau.
std::optional<EdgeMeasure> measureEdge(const ChannelView<Rgba8> &color,
    float edgeX,
    float centreY,
    float halfPx,
    bool darkLeft)
{
  const float span = 0.9f * halfPx;
  std::vector<double> pos, level;
  for (int px = int(std::floor(edgeX - span));
       px <= int(std::ceil(edgeX + span));
       ++px) {
    const float cx = px + 0.5f;
    if (px < 0 || px >= int(color.width) || std::fabs(cx - edgeX) > span)
      continue;
    double sum = 0.0;
    size_t n = 0;
    for (int py = int(std::floor(centreY - 0.5f * halfPx));
         py <= int(std::ceil(centreY + 0.5f * halfPx));
         ++py) {
      if (py < 0 || py >= int(color.height)
          || std::fabs(py + 0.5f - centreY) > 0.5f * halfPx)
        continue;
      const Rgba8 v = color.at(uint32_t(px), uint32_t(py));
      sum += (double(v[0]) + v[1] + v[2]) / 3.0;
      ++n;
    }
    if (n == 0)
      continue;
    // Signed distance from the edge, the dark side negative.
    pos.push_back(darkLeft ? cx - edgeX : edgeX - cx);
    level.push_back(sum / double(n));
  }
  if (pos.size() < 5)
    return std::nullopt;
  if (!darkLeft) {
    std::reverse(pos.begin(), pos.end());
    std::reverse(level.begin(), level.end());
  }
  const size_t k = std::max<size_t>(1, pos.size() / 5);
  double lo = 0.0, hi = 0.0;
  for (size_t i = 0; i < k; ++i) {
    lo += level[i];
    hi += level[level.size() - 1 - i];
  }
  lo /= double(k);
  hi /= double(k);
  EdgeMeasure e;
  e.contrast = hi - lo;
  if (!(e.contrast > 0.0))
    return e;
  // Where the profile first reaches `f` of the way up, interpolated.
  auto crossing = [&](double f) {
    for (size_t i = 0; i < pos.size(); ++i) {
      const double q = (level[i] - lo) / e.contrast;
      if (q < f)
        continue;
      if (i == 0)
        return pos[0];
      const double q0 = (level[i - 1] - lo) / e.contrast;
      return pos[i - 1] + (f - q0) / (q - q0) * (pos[i] - pos[i - 1]);
    }
    return pos.back();
  };
  e.rise = crossing(0.9) - crossing(0.1);
  e.midpoint = crossing(0.5);
  return e;
}

// The depth-of-field check: each bright square's left and right edges are
// sharp or blurred as the Case's lens says, and the square is centred where
// the pinhole puts it.
void focusCheck(ExactCheck &x)
{
  const Lens *lens = lensNamed(x.values().getString("lens", ""));
  if (!lens) {
    x.measure("the Case names a lens", 0.0, Limit::AtLeast, 1.0);
    return;
  }
  const auto color = x.rgba(Channel::Color);
  const char *plateNames[2] = {"near plate", "far plate"};
  for (size_t i = 0; i < optics::kFocusPlates.size(); ++i) {
    const optics::Plate &p = optics::kFocusPlates[i];
    const bool sharp = lens->sharp[i];
    const std::string plate =
        std::string(plateNames[i]) + (sharp ? " (sharp)" : " (out of focus)");
    const auto left = x.project(p.centre - float3(p.half, 0.f, 0.f));
    const auto right = x.project(p.centre + float3(p.half, 0.f, 0.f));
    if (!left || !right) {
      x.measure(plate + " projects into the frame", 0.0, Limit::AtLeast, 1.0);
      continue;
    }
    const float halfPx = 0.5f * (right->x - left->x);
    std::array<std::optional<double>, 2> halfway; // left, right; in pixels
    for (const bool leftEdge : {true, false}) {
      const std::string edge =
          plate + (leftEdge ? ", left edge" : ", right edge");
      const auto e = measureEdge(
          color, leftEdge ? left->x : right->x, left->y, halfPx, leftEdge);
      if (!e) {
        x.measure(edge + " lies in the frame", 0.0, Limit::AtLeast, 1.0);
        continue;
      }
      if (!x.measure(edge + ": levels from the border to the square",
              e->contrast,
              Limit::AtLeast,
              kMinEdgeContrast))
        continue;
      if (sharp)
        x.measure(edge + ": pixels from 10 % to 90 % of the rise (sharp)",
            e->rise,
            Limit::AtMost,
            kSharpRise);
      else
        x.measure(edge + ": pixels from 10 % to 90 % of the rise (blurred)",
            e->rise,
            Limit::AtLeast,
            kBlurredRise * halfPx);
      if (leftEdge)
        halfway[0] = left->x + e->midpoint;
      else
        halfway[1] = right->x - e->midpoint;
    }
    if (halfway[0] && halfway[1])
      x.measure(plate
              + ": pixels from the square's centre (halfway between its "
                "edges' halfway points) to where the pinhole puts it",
          std::fabs(0.5 * (*halfway[0] + *halfway[1])
              - 0.5 * (left->x + right->x)),
          Limit::AtMost,
          kCentrePlacement);
  }
}

// Stereo /////////////////////////////////////////////////////////////////////

anari::Camera stereoCamera(BuildContext &ctx)
{
  auto d = ctx.device();
  auto camera = ctx.newCamera(optics::camera());
  const std::string mode = ctx.getString("stereoMode", "unset");
  if (mode != "unset")
    anari::setParameter(d, camera, "stereoMode", mode);
  setCameraParam(ctx, camera, "interpupillaryDistance");
  anari::commitParameters(d, camera);
  return camera;
}

// How strongly a pixel shows a plate: how many 8-bit levels its colour's
// channel (red for the near plate, blue for the far one) is above the larger
// of the other two. On a neutral background this is proportional to how
// much of the pixel the plate covers, so it weights an antialiased edge
// pixel by its share.
int plateStrength(const Rgba8 &v, bool nearPlate)
{
  const size_t channel = nearPlate ? 0 : 2;
  int others = 0;
  for (size_t c = 0; c < 3; ++c)
    if (c != channel)
      others = std::max(others, int(v[c]));
  return int(v[channel]) - others;
}

// A pixel shows a plate when its strength is at least this.
constexpr int kColourMargin = 40;
// A half that shows fewer pixels of a plate than this doesn't show it.
constexpr size_t kMinPlatePixels = 16;

// Where a plate shows inside [x0, x1) x [y0, y1): how many pixels show it,
// and the centroid of every pixel with any strength, weighted by it
// (continuous pixel coordinates), which antialiasing leaves to a fraction of
// a pixel.
struct Sighting
{
  size_t count{0};
  double x{0.0};
  double y{0.0};
};

Sighting sight(const ChannelView<Rgba8> &color,
    uint32_t x0,
    uint32_t y0,
    uint32_t x1,
    uint32_t y1,
    bool nearPlate)
{
  Sighting s;
  double weight = 0.0;
  for (uint32_t y = y0; y < y1; ++y)
    for (uint32_t px = x0; px < x1; ++px) {
      const int strength = plateStrength(color.at(px, y), nearPlate);
      if (strength <= 0)
        continue;
      if (strength >= kColourMargin)
        ++s.count;
      weight += strength;
      s.x += strength * (px + 0.5);
      s.y += strength * (y + 0.5);
    }
  if (weight > 0.0) {
    s.x /= weight;
    s.y /= weight;
  }
  return s;
}

// What the stereo check knows of the Case: the near and far plates' centres
// through the mono camera, and the left eye's disparity (the near plate's
// offset from the far one, minus the mono offset) in a frame of the Case's
// size: positive, the near plate to the right of the far one. The right
// eye's is its negative.
struct StereoGeometry
{
  float2 nearAt;
  float2 farAt;
  double disparity{0.0};
};

std::optional<StereoGeometry> stereoGeometry(const ExactCheck &x, float ipd)
{
  const auto mono = x.camera();
  if (!mono)
    return std::nullopt;
  CameraProjection eye = *mono;
  const float3 right = math::normalize(math::cross(mono->direction, mono->up));
  eye.position = mono->position - 0.5f * ipd * right;
  const auto nearAt = x.project(optics::kStereoNear.centre);
  const auto farAt = x.project(optics::kStereoFar.centre);
  const auto nearEye =
      projectToPixel(eye, x.frameSize(), optics::kStereoNear.centre);
  const auto farEye =
      projectToPixel(eye, x.frameSize(), optics::kStereoFar.centre);
  if (!nearAt || !farAt || !nearEye || !farEye)
    return std::nullopt;
  StereoGeometry g;
  g.nearAt = *nearAt;
  g.farAt = *farAt;
  g.disparity = (nearEye->x - farEye->x) - (nearAt->x - farAt->x);
  return g;
}

// How far a measured disparity may be from the parallel eyes' (pixels):
// centroids are good to a fraction of a pixel, and eyes toed in towards a
// convergence point see the near plate's offset grow by a few percent.
double disparityTolerance(double disparity)
{
  return 0.75 + 0.15 * std::fabs(disparity);
}

// How far a plate's centroid row may be from the mono camera's (pixels): the
// eyes are apart along the camera's right vector, so the rows don't move,
// beyond a few percent of the distance from the middle row for toed-in eyes.
double rowTolerance(double rowFromMiddle)
{
  return 1.0 + 0.05 * std::fabs(rowFromMiddle);
}

// How an eye's image fills the pixels the stereo check looks at.
enum class Fit
{
  Frame, // the whole frame
  Half, // a half of the frame, at a scale of 1/2 or 1 across the eyes' offset
  HalfEitherEye, // the same, holding either eye
};

// One eye's image in the pixels `box` ([x0, y0, x1, y1)): both plates show,
// at the rows `row(y)` maps the mono camera's to, and the near plate is
// offset from the far one by `disparity` (Frame) or, in a half, by between
// half and all of it the same way (an eye's image may be fitted to its half
// by squeezing it or at the full frame's scale; the spec doesn't say), or
// for HalfEitherEye either way. Returns the measured offset.
std::optional<double> checkEye(ExactCheck &x,
    const ChannelView<Rgba8> &color,
    const std::string &where,
    std::array<uint32_t, 4> box,
    const StereoGeometry &g,
    double disparity,
    Fit fit,
    const std::function<double(double)> &row)
{
  const Sighting nearPlate = sight(color, box[0], box[1], box[2], box[3], true);
  const Sighting farPlate = sight(color, box[0], box[1], box[2], box[3], false);
  bool shown = x.measure("pixels showing the near (red) plate in " + where,
      double(nearPlate.count),
      Limit::AtLeast,
      double(kMinPlatePixels));
  shown = x.measure("pixels showing the far (blue) plate in " + where,
              double(farPlate.count),
              Limit::AtLeast,
              double(kMinPlatePixels))
      && shown;
  if (!shown)
    return std::nullopt;
  const double middle = 0.5 * x.frameSize().height;
  x.measure(
      "pixels from the near plate's row in " + where + " to the mono camera's",
      std::fabs(nearPlate.y - row(g.nearAt.y)),
      Limit::AtMost,
      rowTolerance(g.nearAt.y - middle));
  x.measure(
      "pixels from the far plate's row in " + where + " to the mono camera's",
      std::fabs(farPlate.y - row(g.farAt.y)),
      Limit::AtMost,
      rowTolerance(g.farAt.y - middle));
  const double offset = nearPlate.x - farPlate.x;
  const double tolerance = disparityTolerance(disparity);
  if (fit == Fit::Frame) {
    x.measure("pixels from the near plate's offset from the far one in " + where
            + " to the eye's disparity (" + std::to_string(disparity) + ")",
        std::fabs(offset - disparity),
        Limit::AtMost,
        tolerance);
  } else {
    const double signedOffset = fit == Fit::HalfEitherEye ? std::fabs(offset)
        : disparity < 0.0                                 ? -offset
                                                          : offset;
    const double magnitude = std::fabs(disparity);
    const std::string way = fit == Fit::HalfEitherEye
        ? "either way"
        : (disparity < 0.0 ? "to the left" : "to the right");
    x.measure("pixels the near plate is offset from the far one in " + where
            + ", " + way + ": at least half the eye's disparity ("
            + std::to_string(magnitude) + "), less the tolerance",
        signedOffset,
        Limit::AtLeast,
        0.5 * magnitude - tolerance);
    x.measure("pixels the near plate is offset from the far one in " + where
            + ", " + way + ": at most the eye's disparity, plus the "
            + "tolerance",
        signedOffset,
        Limit::AtMost,
        magnitude + tolerance);
  }
  return offset;
}

// The stereo check: the frame (`none`, `left`, `right`) or each of its halves
// (`sideBySide`, `topBottom`) shows its eye's image, by the near plate's
// offset from the far one (ADR-0010 53).
void stereoCheck(ExactCheck &x)
{
  const std::string mode = x.values().getString("stereoMode", "unset");
  const float ipd = x.get<float>(
      "interpupillaryDistance", optics::kDefaultInterpupillaryDistance);
  const auto g = stereoGeometry(x, ipd);
  if (!g) {
    x.measure("the plates project into the frame", 0.0, Limit::AtLeast, 1.0);
    return;
  }
  const auto color = x.rgba(Channel::Color);
  const uint32_t w = x.frameSize().width, h = x.frameSize().height;
  const auto same = [](double y) { return y; };
  if (mode == "none" || mode == "unset" || mode == "left" || mode == "right") {
    const double disparity = mode == "left" ? g->disparity
        : mode == "right"                   ? -g->disparity
                                            : 0.0;
    checkEye(x,
        color,
        "the frame (" + (mode == "unset" ? std::string("mono") : mode) + ")",
        {0, 0, w, h},
        *g,
        disparity,
        Fit::Frame,
        same);
    return;
  }
  if (mode == "sideBySide") {
    // The spec doesn't say which half holds which eye: the halves hold
    // opposite eyes, the near plate offset opposite ways.
    const auto inLeft = checkEye(x,
        color,
        "the left half",
        {0, 0, w / 2, h},
        *g,
        g->disparity,
        Fit::HalfEitherEye,
        same);
    const auto inRight = checkEye(x,
        color,
        "the right half",
        {w - w / 2, 0, w, h},
        *g,
        g->disparity,
        Fit::HalfEitherEye,
        same);
    if (inLeft && inRight)
      x.measure(
          "the near plate's offset in the left half times that in the "
          "right half (negative: the halves hold opposite eyes)",
          *inLeft * *inRight,
          Limit::AtMost,
          0.0);
    return;
  }
  if (mode == "topBottom") {
    // The left eye at the top half (rows h/2 up; row 0 is the bottom), each
    // half the frame's rows at half scale.
    checkEye(x,
        color,
        "the top half (left eye)",
        {0, h - h / 2, w, h},
        *g,
        g->disparity,
        Fit::Half,
        [h](double y) { return double(h - h / 2) + 0.5 * y; });
    checkEye(x,
        color,
        "the bottom half (right eye)",
        {0, 0, w, h / 2},
        *g,
        -g->disparity,
        Fit::Half,
        [](double y) { return 0.5 * y; });
    return;
  }
  x.measure("the Case names a stereo mode", 0.0, Limit::AtLeast, 1.0);
}

V strings(std::initializer_list<const char *> values)
{
  V out;
  for (const char *v : values)
    out.emplace_back(Any(v));
  return out;
}

} // namespace

void registerOpticsTests(Catalog &catalog)
{
  V lenses;
  for (const auto &l : kLenses)
    lenses.emplace_back(Any(l.name));
  makeTest("camera", "depth_of_field")
      .description(
          "Checks that a perspective camera with an aperture keeps the plate "
          "at its focus distance sharp and blurs one away from it (the "
          "focus distance picks which, 1 when unset), and that an aperture "
          "radius of 0 is a pinhole.")
      .build(focusWorld)
      .camera([](BuildContext &ctx, const scenes::Bounds &) {
        return focusCamera(ctx);
      })
      .permute("lens", lenses)
      .expectDerived("lens",
          "focus_default",
          "`focus_one`: the spec's default focusDistance is 1",
          [](const Siblings &s) -> ExpectedImage { return s.at("focus_one"); })
      .expectDerived("lens",
          "aperture_zero",
          "`pinhole`: an apertureRadius of 0 is no depth of field",
          [](const Siblings &s) -> ExpectedImage { return s.at("pinhole"); })
      .expectExact(
          "the plate at the focus distance is sharp and one away "
          "from it blurred, each centred where the pinhole puts it",
          focusCheck)
      .noGroundTruth()
      .covers(kDepthOfField)
      .requireFeatures(kSupportMatteTriangle)
      .registerInto(catalog);

  makeTest("camera", "stereo")
      .description(
          "Checks that each stereo mode shows its eye's image (in the frame, "
          "or in each half for sideBySide and topBottom) by the near plate's "
          "offset from the far one, and that an unset stereoMode is `none`.")
      .build(stereoWorld)
      .camera([](BuildContext &ctx, const scenes::Bounds &) {
        return stereoCamera(ctx);
      })
      .permute("stereoMode",
          strings(
              {"none", "left", "right", "sideBySide", "topBottom", "unset"}))
      .permute(
          "interpupillaryDistance", V{Any(optics::kInterpupillaryDistance)})
      .expectDerived("stereoMode",
          "unset",
          "`none`: the spec's default stereoMode",
          [](const Siblings &s) -> ExpectedImage { return s.at("none"); })
      .expectExact(
          "each eye's image shows the near plate offset from the far "
          "one by that eye's disparity",
          stereoCheck)
      .noGroundTruth()
      .covers(kStereo)
      .requireFeatures(kSupportMatteTriangle)
      .registerInto(catalog);

  makeTest("camera", "stereo_zero_ipd")
      .description(
          "Checks that with an interpupillaryDistance of 0 the left and right "
          "eyes see what the mono camera does.")
      .build(stereoWorld)
      .camera([](BuildContext &ctx, const scenes::Bounds &) {
        return stereoCamera(ctx);
      })
      .variant("stereoMode", strings({"none", "left", "right"}))
      .permute("interpupillaryDistance", V{Any(0.f)})
      .expectExact(
          "each eye's image shows the near plate offset from the far "
          "one by that eye's disparity",
          stereoCheck)
      .noGroundTruth()
      .covers(kStereo)
      .requireFeatures(kSupportMatteTriangle)
      .registerInto(catalog);

  makeTest("camera", "stereo_default_ipd")
      .description(
          "Checks that an unset interpupillaryDistance is the spec's default, "
          "0.0635.")
      .build(stereoWorld)
      .camera([](BuildContext &ctx, const scenes::Bounds &) {
        return stereoCamera(ctx);
      })
      .permute("stereoMode", strings({"left"}))
      .variant("interpupillaryDistance",
          V{Any(optics::kDefaultInterpupillaryDistance), none()})
      .expectExact(
          "each eye's image shows the near plate offset from the far "
          "one by that eye's disparity",
          stereoCheck)
      .noGroundTruth()
      .covers(kStereo)
      .requireFeatures(kSupportMatteTriangle)
      .registerInto(catalog);
}

} // namespace cts
} // namespace anari
