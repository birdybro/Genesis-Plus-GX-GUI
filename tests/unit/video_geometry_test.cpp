#include "genplusgx/video/video_geometry.h"

#include <cmath>
#include <cstdint>
#include <iostream>

namespace {

bool check(bool condition, const char* message)
{
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

} // namespace

int main()
{
  using genplusgx::video::AspectMode;
  using genplusgx::video::ScaleMode;
  using genplusgx::video::calculateVideoLayout;

  if (!check(!calculateVideoLayout(0U, 224U, 1280, 720,
          AspectMode::native, ScaleMode::fit).valid(),
        "Zero-width video produced a layout") ||
      !check(!calculateVideoLayout(320U, 224U, 0, 720,
          AspectMode::native, ScaleMode::fit).valid(),
        "Zero-width destination produced a layout")) {
    return 1;
  }

  const auto native = calculateVideoLayout(
    320U, 224U, 1280, 720, AspectMode::native, ScaleMode::fit);
  const auto fourThree = calculateVideoLayout(
    320U, 224U, 1280, 720, AspectMode::fourThree, ScaleMode::fit);
  const auto stretch = calculateVideoLayout(
    320U, 224U, 1280, 720, AspectMode::stretch, ScaleMode::fit);
  const auto integer = calculateVideoLayout(
    320U, 224U, 1280, 720, AspectMode::native, ScaleMode::integer);
  const auto integerFourThree = calculateVideoLayout(
    320U, 224U, 1280, 720, AspectMode::fourThree, ScaleMode::integer);

  if (!check(native.x == 126 && native.y == 0 &&
          native.width == 1028 && native.height == 720 &&
          native.integerScale == 0U,
        "Native fit layout was incorrect") ||
      !check(fourThree.x == 160 && fourThree.y == 0 &&
          fourThree.width == 960 && fourThree.height == 720,
        "Forced 4:3 layout was incorrect") ||
      !check(stretch.x == 0 && stretch.y == 0 &&
          stretch.width == 1280 && stretch.height == 720,
        "Stretch layout did not fill the destination") ||
      !check(integer.x == 160 && integer.y == 24 &&
          integer.width == 960 && integer.height == 672 &&
          integer.integerScale == 3U,
        "Native integer layout was incorrect") ||
      !check(integerFourThree.x == 192 && integerFourThree.y == 24 &&
          integerFourThree.width == 896 && integerFourThree.height == 672 &&
          integerFourThree.integerScale == 3U,
        "Forced 4:3 integer layout was incorrect")) {
    return 2;
  }

  const auto undersized = calculateVideoLayout(
    320U, 224U, 200, 100, AspectMode::native, ScaleMode::integer);
  if (!check(undersized.valid() && undersized.integerScale == 0U &&
          undersized.width <= 200 && undersized.height <= 100,
        "Undersized integer request did not fall back to fit")) {
    return 3;
  }

  for (std::uint32_t sourceWidth : {160U, 256U, 320U, 640U}) {
    for (std::uint32_t sourceHeight : {144U, 192U, 224U, 240U, 448U}) {
      for (std::int32_t destinationWidth : {1, 199, 640, 1920}) {
        for (std::int32_t destinationHeight : {1, 113, 480, 1080}) {
          for (const auto aspect : {AspectMode::native, AspectMode::fourThree}) {
            for (const auto scale : {ScaleMode::fit, ScaleMode::integer}) {
              const auto layout = calculateVideoLayout(
                sourceWidth, sourceHeight, destinationWidth, destinationHeight,
                aspect, scale);
              if (!check(layout.valid() && layout.x >= 0 && layout.y >= 0 &&
                    layout.x + layout.width <= destinationWidth &&
                    layout.y + layout.height <= destinationHeight,
                  "Property layout escaped or collapsed its destination")) {
                return 4;
              }
              if (layout.integerScale > 0U && aspect == AspectMode::native &&
                  !check(layout.width == static_cast<std::int32_t>(
                      sourceWidth * layout.integerScale) &&
                    layout.height == static_cast<std::int32_t>(
                      sourceHeight * layout.integerScale),
                    "Native integer scaling lost whole-pixel multiples")) {
                return 5;
              }
            }
          }
        }
      }
    }
  }

  return 0;
}
