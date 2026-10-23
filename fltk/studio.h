// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "appearance.h"
#include "desktop-menu.h"
#include "editor.h"
#include "gpu-window.h"
#include "render.h"
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Hold_Browser.H>
#include <FL/Fl_Value_Slider.H>
#include <atomic>
#include <future>
namespace ob {
class Canvas : public Fl_Gl_Window {
  Editor &editor;
  std::unique_ptr<Renderer> renderer;
  GLContext lastContext = nullptr;
  Vec3 target{0, 1.3f, -1.2f};
  float distance = 1.4f, yaw = 0, pitch = 0;
  int lastX = 0, lastY = 0, dragButton = 0;
  bool painting = false;
  Point point(int x, int y) const;
  Quat orientation() const;
  void sample(int x, int y);
  void draw() override;

public:
  Canvas(int x, int y, int w, int h, Editor &editor);
  ~Canvas();
  int handle(int event) override;
  void reset();
  Transform referencePlacement() const;
  void fit();
  bool capture(const std::filesystem::path &path);
};
class Studio : public dx::GpuWindow {
  Editor editor;
  DesktopMenuBar menu{0, 0, 1100, 30};
  Fl_Group controls{8, 38, 210, 710};
  Fl_Choice tool{8, 62, 210, 28, "Tool"}, brush{8, 118, 210, 28, "Brush"};
  Fl_Button color{8, 160, 210, 30, "Color..."};
  Fl_Value_Slider size{8, 220, 210, 32, "Brush size (mm)"},
      opacity{8, 278, 210, 32, "Opacity"};
  Fl_Check_Button mirror{8, 322, 210, 28, "Mirror across X=0"};
  Fl_Hold_Browser layerList{8, 382, 210, 170, "Layers"};
  Fl_Button addLayer{8, 560, 100, 28, "Add layer"};
  Fl_Check_Button visibleLayer{114, 560, 104, 28, "Visible"};
  Fl_Button xrButton{8, 614, 210, 36, "Start XR painting"};
  Fl_Button undoButton{8, 660, 100, 30, "Undo"},
      redoButton{118, 660, 100, 30, "Redo"};
  Canvas canvas{230, 38, 862, 700, editor};
  Fl_Box status{8, 748, 1084, 30};
  std::unique_ptr<DesktopMenu> desktop;
  std::vector<uint32_t> layerIds;
  std::string layerSignature, currentPath, error;
  struct IoResult {
    Document document;
    std::string path, error;
    bool load = false, exported = false, imported = false;
    ReferenceImage reference;
    uint64_t version = 0;
  };
  std::future<IoResult> io;
  std::future<std::string> xr, recovery;
  std::atomic_bool xrStop{false}, xrSaveRequested{false};
  std::function<void()> afterIo, afterXR;
  std::filesystem::path recoveryPath, recoveredFrom;
  std::chrono::steady_clock::time_point nextRecovery;
  uint64_t lastRevision = 0, lastToolRevision = 0;
  bool closing = false;
  static void timer(void *);
  static void menuAction(Fl_Widget *, void *);
  void poll();
  void layout();
  void syncControls();
  void setControls();
  void quiet(std::function<void()> next);
  void maybeDiscard(std::function<void()> next);
  void openDialog();
  void exportDialog();
  void referenceDialog();
  void referenceProperties();
  bool save(bool saveAs = false);
  void autosave();
  void removeRecovery();

public:
  Studio();
  ~Studio();
  void resize(int x, int y, int w, int h) override;
  void command(const std::string &name);
  void open(const std::string &path);
  bool exportGltf(const std::string &path);
  bool importReference(const std::string &path);
  void startXR(unsigned frames = 0);
  void close();
  void restore();
  Editor &model() { return editor; }
  Canvas &view() { return canvas; }
  DesktopMenuBar &menuBar() { return menu; }
  bool busy() const { return io.valid() || xr.valid(); }
  std::string filePath() const { return currentPath; }
};
int desktopMain(int argc, char **argv);
} // namespace ob
