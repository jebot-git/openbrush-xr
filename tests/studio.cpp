// SPDX-License-Identifier: Apache-2.0
#include "studio.h"
#include <gio/gio.h>
#include <iostream>
#include <png.h>
#include <unistd.h>
using namespace ob;
template <class F> void wait(F fn) {
  auto end = std::chrono::steady_clock::now() + std::chrono::seconds(8);
  while (!fn() && std::chrono::steady_clock::now() < end)
    Fl::wait(.02);
  if (!fn())
    throw std::runtime_error("Studio UI wait timed out");
}
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
void pointer(Studio &s, int event, int x, int y) {
  Fl::e_x = x;
  Fl::e_y = y;
  Fl::e_keysym = FL_Button + 1;
  Fl::e_state = event == FL_RELEASE ? 0 : FL_BUTTON1;
  Fl::e_clicks = 0;
  Fl::handle(event, &s);
}
int main(int argc, char **argv) try {
  require(argc == 3, "Expected fixture and capture path");
  dx::init();
  Studio s;
  s.show();
  for (int i = 0; i < 10; ++i)
    Fl::wait(.02);
  auto &v = s.view();
  int x = v.x() + v.w() / 2, y = v.y() + v.h() / 2;
  pointer(s, FL_PUSH, x, y);
  pointer(s, FL_DRAG, x + 40, y);
  pointer(s, FL_RELEASE, x + 40, y);
  wait([&] { return s.model().copy().strokes.size() == 1; });
  auto drawn = s.model().copy().strokes[0];
  require(std::abs(drawn.points[0].position.x) < .01f,
          "GL subwindow cursor position must be local");
  require(drawn.points.back().position.x > .01f,
          "Dragging must draw to the right");
  s.command("Undo");
  require(s.model().copy().strokes.empty(), "Undo must remove stroke");
  s.command("Redo");
  require(s.model().copy().strokes.size() == 1, "Redo must restore stroke");
  auto bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
  require(bus, "Private session bus");
  require(!s.menuBar().universal(), "Local menu must remain without a host");
  auto request = g_dbus_connection_call_sync(
      bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
      "org.freedesktop.DBus", "RequestName",
      g_variant_new("(su)", "com.canonical.AppMenu.Registrar", 0), nullptr,
      G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr);
  require(request, "Acquire registrar");
  g_variant_unref(request);
  wait([&] { return s.menuBar().universal(); });
  require(s.menuBar().h() == 0, "Universal menu must reclaim local bar");
  require(v.y() == 8, "Universal-menu viewport must move to the top");
  s.command("Select all");
  s.command("Duplicate selection");
  require(s.model().copy().strokes.size() == 2, "Selection duplicate");
  s.command("Delete selection");
  require(s.model().copy().strokes.size() == 1, "Selection delete");
  auto release = g_dbus_connection_call_sync(
      bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
      "org.freedesktop.DBus", "ReleaseName",
      g_variant_new("(s)", "com.canonical.AppMenu.Registrar"), nullptr,
      G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr);
  g_variant_unref(release);
  wait([&] { return !s.menuBar().universal(); });
  require(v.y() == 38, "Local-menu fallback must restore layout");
  g_object_unref(bus);
  s.open(argv[1]);
  wait([&] { return !s.busy(); });
  require(s.model().copy().strokes.size() == 1,
          "Real artwork must load asynchronously");
  s.command("Select all");
  s.command("Duplicate selection");
  auto imagePath = std::filesystem::path(argv[1]).parent_path() / "trace.png";
  png_image image{};
  image.version = PNG_IMAGE_VERSION;
  image.width = image.height = 2;
  image.format = PNG_FORMAT_RGBA;
  const unsigned char rgba[] = {255, 0, 0,   255, 0,   255, 0, 255,
                                0,   0, 255, 255, 255, 255, 0, 255};
  require(
      png_image_write_to_file(&image, imagePath.c_str(), 0, rgba, 0, nullptr),
      "Create reference fixture");
  auto originalPath = s.filePath();
  require(s.importReference(imagePath.string()),
          "Start async reference import");
  wait([&] { return !s.busy(); });
  require(s.model().copy().references.size() == 1, "Import image into drawing");
  require(s.filePath() == originalPath && s.model().frame().dirty,
          "Import preserves editable destination");
  require(s.model().layerReferenceName(s.model().tools().layer) == "trace.png",
          "New image layer must be selected");
  s.command("Undo");
  require(s.model().copy().references.empty(), "Undo reference import");
  s.command("Redo");
  s.command("Remove reference image");
  require(s.model().copy().references.empty(),
          "Remove reference from current layer");
  s.command("Undo");
  require(s.model().copy().references.size() == 1, "Undo reference removal");
  std::filesystem::remove(imagePath);
  auto exportPath = std::string(argv[1]) + ".glb";
  auto editablePath = s.filePath();
  require(s.model().frame().dirty, "Drawing must be dirty before export");
  require(s.menuBar().find_item("File/Export glTF..."),
          "Export must be available in universal menu commands");
  require(s.exportGltf(exportPath), "Start background glTF export");
  wait([&] { return !s.busy(); });
  require(std::filesystem::file_size(exportPath) > 20,
          "Export must produce geometry");
  require(s.filePath() == editablePath && s.model().frame().dirty,
          "Export must preserve editable path and dirty state");
  s.command("Save");
  wait([&] { return !s.busy(); });
  require(readTilt(argv[1]).strokes.size() == 2,
          "Save must write editable document");
  require(!s.model().frame().dirty, "Successful save must clear dirty state");
  require(readTilt(argv[1]).references.size() == 1,
          "Save embeds reference after source removal");
  // Recovery must open as unsaved artwork, never make the temporary recovery
  // path the implicit destination of an ordinary Save command.
  auto recoveryFile = std::filesystem::path(getenv("XDG_STATE_HOME")) /
                      "openbrush-native/recovery-2147483000.tilt";
  writeTilt(s.model().copy(), recoveryFile);
  Fl::add_timeout(
      .15,
      [](void *) {
        auto dialog = Fl::modal();
        if (dialog) {
          Fl::e_keysym = FL_Enter;
          Fl::e_state = 0;
          Fl::handle(FL_KEYDOWN, dialog);
        }
      },
      nullptr);
  s.restore();
  wait([&] { return !s.busy(); });
  require(s.filePath().empty(),
          "Recovery must require a real save destination");
  require(s.model().frame().dirty, "Recovered drawing must remain unsaved");
  require(s.model().copy().references.size() == 1,
          "Recovery restores embedded reference");
  s.view().capture(std::string(argv[2]) + ".scene.ppm");
  dx::screenshot(s, argv[2]);
  s.hide();
  std::cout << "Studio UI: native pointer painting, undo/redo, selection, "
               "universal-menu collapse/fallback, async open and save passed\n";
  return 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
