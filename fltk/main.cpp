// SPDX-License-Identifier: Apache-2.0
#include "studio.h"
#include <iostream>
#include <thread>
int xrMain(int argc, char **argv);
namespace ob {
int desktopMain(int argc, char **argv) {
  bool x11 = false;
  std::string input, capture;
  bool demo = false;
  unsigned xrFrames = 0;
  bool startXR = false;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--x11")
      x11 = true;
    else if (arg == "--open" && i + 1 < argc)
      input = argv[++i];
    else if (arg == "--demo")
      demo = true;
    else if (arg == "--ui-capture" && i + 1 < argc)
      capture = argv[++i];
    else if (arg == "--ui-xr-frames" && i + 1 < argc) {
      startXR = true;
      xrFrames = std::stoul(argv[++i]);
    } else if (arg.rfind("--", 0) == 0)
      throw std::runtime_error("Unknown desktop option: " + arg);
    else
      input = arg;
  }
#if !OPENBRUSH_ENABLE_X11
  if (x11 ||
      (getenv("FLTK_BACKEND") && std::string(getenv("FLTK_BACKEND")) == "x11"))
    throw std::runtime_error(
        "X11 support is a build-time opt-in (-DOPENBRUSH_ENABLE_X11=ON)");
#endif
  setenv("FLTK_BACKEND", x11 ? "x11" : "wayland", 1);
  dx::init();
  fl_register_images();
  Studio studio;
  dx::style(&studio);
  studio.show();
  if (!input.empty())
    studio.open(input);
  else if (demo) {
    Document d;
    for (int j = 0; j < 3; ++j) {
      Stroke s;
      s.brush = brushes()[j].guid;
      s.color = {j == 0 ? 1.f : .2f, j == 1 ? 1.f : .2f, j == 2 ? 1.f : .2f, 1};
      s.size = .02f;
      for (int i = 0; i < 120; ++i) {
        float t = i * .05f;
        s.points.push_back({{std::cos(t + j * 2.1f) * .2f, 1.1f + i * .003f,
                             std::sin(t + j * 2.1f) * .2f - 1.2f},
                            {},
                            1,
                            uint32_t(i * 10)});
      }
      d.strokes.push_back(std::move(s));
    }
    studio.model().load(std::move(d));
    studio.view().fit();
  } else if (capture.empty())
    studio.restore();
  if (startXR)
    studio.startXR(xrFrames);
  if (!capture.empty()) {
    auto until = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    for (int i = 0; i < 20 || studio.busy(); ++i) {
      Fl::wait(.025);
      if (std::chrono::steady_clock::now() > until)
        throw std::runtime_error("UI capture timed out");
    }
    if (!studio.view().capture(capture + ".scene.ppm") ||
        !dx::screenshot(studio, capture.c_str()))
      throw std::runtime_error("UI capture failed");
    studio.hide();
    return 0;
  }
  return Fl::run();
}
} // namespace ob
int main(int argc, char **argv) try {
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--xr" || arg == "--headless" || arg == "--probe" ||
        arg == "--render" || arg == "--palette-capture" || arg == "--version" ||
        arg == "--frames" || arg == "--export-gltf" || arg == "--exclusive")
      return xrMain(argc, argv);
  }
  if (argc > 1 && std::string(argv[1]) == "--help") {
    std::cout
        << "OpenBrush Native: default native FLTK Wayland editor\n  "
           "[artwork.tilt] / --open FILE: open editable artwork\n  --demo: "
           "sample drawing\n  --xr: native stereo OpenXR painting without a "
           "desktop window\n  --probe: inspect OpenXR runtime\n  --render "
           "FILE: diagnostic offscreen GL render\n  --export-gltf FILE.gltf|FILE.glb: export brush geometry\n  --x11: optional X11 "
           "backend when explicitly enabled at build time\n";
    return 0;
  }
  return ob::desktopMain(argc, argv);
} catch (const std::exception &e) {
  std::cerr << "OpenBrush Native: " << e.what() << '\n';
  return 1;
}
