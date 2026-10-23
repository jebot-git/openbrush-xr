// SPDX-License-Identifier: Apache-2.0
#include "studio.h"
#include "gltf.h"
#include "xr.h"
#include <FL/Fl_Color_Chooser.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <signal.h>
#include <sys/stat.h>
namespace ob {
namespace {
std::filesystem::path statePath() {
  const char *state = getenv("XDG_STATE_HOME");
  return (state ? std::filesystem::path(state)
                : std::filesystem::path(getenv("HOME") ? getenv("HOME")
                                                       : "/tmp") /
                      ".local/state") /
         "openbrush-native";
}
} // namespace
void Studio::menuAction(Fl_Widget *w, void *p) {
  auto bar = dynamic_cast<Fl_Menu_ *>(w);
  if (bar && bar->mvalue())
    static_cast<Studio *>(p)->command(bar->mvalue()->label());
}
Studio::Studio() : dx::GpuWindow(1100, 790, "OpenBrush Native") {
  xclass("org.domed.OpenBrushNative");
  controls.end();
  end();
  add(canvas);
  add(status);
  // Members are constructed sequentially, so explicitly establish the panels.
  for (auto *w : std::vector<Fl_Widget *>{
           &tool, &brush, &color, &size, &opacity, &mirror, &layerList,
           &addLayer, &visibleLayer, &xrButton, &undoButton, &redoButton})
    controls.add(w);
  for (auto item : std::vector<std::pair<const char *, int>>{
           {"File/New", FL_CTRL + 'n'},
           {"File/Open...", FL_CTRL + 'o'},
           {"File/Save", FL_CTRL + 's'},
           {"File/Save as...", FL_CTRL + FL_SHIFT + 's'},
           {"File/Export glTF...", 0},
           {"File/Quit", FL_CTRL + 'q'},
           {"Edit/Undo", FL_CTRL + 'z'},
           {"Edit/Redo", FL_CTRL + FL_SHIFT + 'z'},
           {"Edit/Select all", FL_CTRL + 'a'},
           {"Edit/Delete selection", FL_Delete},
           {"Edit/Duplicate selection", FL_CTRL + 'd'},
           {"Edit/Transform selection...", 0},
           {"Edit/Recolor selection", 0},
           {"View/Fit drawing", 'f'},
           {"View/Reset view", 'r'},
           {"XR/Start XR painting", FL_F + 6},
           {"XR/Stop XR painting", 0},
           {"Help/Controls", 0},
           {"Help/About", 0}})
    menu.add(item.first, item.second, menuAction, this);
  tool.align(FL_ALIGN_TOP_LEFT);
  brush.align(FL_ALIGN_TOP_LEFT);
  tool.add("Paint|Erase|Select|Recolor|Move canvas");
  tool.value(0);
  for (auto &b : brushes())
    brush.add(b.name.c_str());
  brush.value(0);
  for (auto *w : {&size, &opacity}) {
    w->type(FL_HOR_NICE_SLIDER);
    w->align(FL_ALIGN_TOP_LEFT);
  }
  size.range(1, 150);
  size.step(1);
  size.value(15);
  opacity.range(.05, 1);
  opacity.step(.01);
  opacity.value(1);
  auto update = [](Fl_Widget *, void *p) {
    static_cast<Studio *>(p)->setControls();
  };
  for (auto *w :
       std::vector<Fl_Widget *>{&tool, &brush, &size, &opacity, &mirror})
    w->callback(update, this);
  tool.callback(
      [](Fl_Widget *, void *p) {
        auto &s = *static_cast<Studio *>(p);
        auto value = s.editor.tools();
        value.tool = Tool(s.tool.value());
        s.editor.configure(value);
        s.syncControls();
      },
      this);
  brush.callback(
      [](Fl_Widget *, void *p) {
        auto &s = *static_cast<Studio *>(p);
        auto value = s.editor.tools();
        value.brush = brushes().at(s.brush.value()).guid;
        value.tool = Tool::Paint;
        s.editor.configure(value);
        s.syncControls();
      },
      this);
  color.callback(
      [](Fl_Widget *, void *p) {
        auto &s = *static_cast<Studio *>(p);
        auto value = s.editor.tools();
        double r = value.color.r, g = value.color.g, b = value.color.b;
        if (fl_color_chooser("Brush color", r, g, b)) {
          value.color = {float(r), float(g), float(b), value.color.a};
          s.editor.configure(value);
          s.syncControls();
        }
      },
      this);
  layerList.format_char(0);
  layerList.align(FL_ALIGN_TOP_LEFT);
  layerList.callback(
      [](Fl_Widget *, void *p) {
        auto &s = *static_cast<Studio *>(p);
        int i = s.layerList.value() - 1;
        if (i >= 0 && i < int(s.layerIds.size())) {
          auto value = s.editor.tools();
          value.layer = s.layerIds[i];
          s.editor.configure(value);
          s.syncControls();
        }
      },
      this);
  addLayer.callback(
      [](Fl_Widget *, void *p) {
        auto &s = *static_cast<Studio *>(p);
        auto ids = s.editor.layers();
        auto value = s.editor.tools();
        value.layer = *std::max_element(ids.begin(), ids.end()) + 1;
        s.editor.configure(value);
        s.layerSignature.clear();
        s.syncControls();
      },
      this);
  visibleLayer.callback(
      [](Fl_Widget *, void *p) {
        auto &s = *static_cast<Studio *>(p);
        s.editor.layerVisible(s.editor.tools().layer, s.visibleLayer.value());
        s.layerSignature.clear();
      },
      this);
  xrButton.callback(
      [](Fl_Widget *, void *p) {
        auto &s = *static_cast<Studio *>(p);
        if (s.xr.valid())
          s.xrStop = true;
        else
          s.startXR();
      },
      this);
  undoButton.callback(
      [](Fl_Widget *, void *p) { static_cast<Studio *>(p)->command("Undo"); },
      this);
  redoButton.callback(
      [](Fl_Widget *, void *p) { static_cast<Studio *>(p)->command("Redo"); },
      this);
  callback([](Fl_Widget *, void *p) { static_cast<Studio *>(p)->close(); },
           this);
  menu.layoutChanged = [this] { layout(); };
  desktop = std::make_unique<DesktopMenu>(*this, menu);
  resizable(canvas);
  size_range(740, 760);
  status.align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  auto dir = statePath();
  std::filesystem::create_directories(dir);
  recoveryPath = dir / ("recovery-" + std::to_string(getpid()) + ".tilt");
  nextRecovery = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  syncControls();
  layout();
  Fl::add_timeout(.03, timer, this);
}
Studio::~Studio() {
  Fl::remove_timeout(timer, this);
  xrStop = true;
  if (xr.valid())
    xr.wait();
  if (io.valid())
    io.wait();
  if (recovery.valid())
    recovery.wait();
}
void Studio::layout() {
  int top = menu.h() + 8;
  menu.resize(0, 0, w(), menu.h());
  controls.resize(8, top, 210, h() - top - 42);
  canvas.resize(230, top, w() - 238, h() - top - 42);
  status.resize(8, h() - 34, w() - 16, 28);
  redraw();
}
void Studio::resize(int x, int y, int w, int h) {
  dx::GpuWindow::resize(x, y, w, h);
  layout();
}
void Studio::timer(void *p) {
  auto &s = *static_cast<Studio *>(p);
  s.poll();
  if (s.shown())
    Fl::repeat_timeout(.03, timer, p);
}
void Studio::setControls() {
  auto value = editor.tools();
  value.tool = Tool(tool.value());
  if (brush.value() >= 0)
    value.brush = brushes().at(brush.value()).guid;
  if (value.tool == Tool::Paint)
    value.size = size.value() / 1000.f;
  else
    value.eraser = size.value() / 1000.f;
  value.color.a = opacity.value();
  value.mirror = mirror.value();
  editor.configure(value);
  syncControls();
}
void Studio::syncControls() {
  auto value = editor.tools();
  tool.value(int(value.tool));
  for (size_t i = 0; i < brushes().size(); ++i)
    if (brushes()[i].guid == value.brush)
      brush.value(i);
  size.value((value.tool == Tool::Paint ? value.size : value.eraser) * 1000);
  size.label(value.tool == Tool::Paint ? "Brush size (mm)"
                                       : "Tool radius (mm)");
  opacity.value(value.color.a);
  mirror.value(value.mirror);
  color.color(fl_rgb_color(value.color.r * 255, value.color.g * 255,
                           value.color.b * 255));
  auto ids = editor.layers();
  std::string signature;
  for (auto id : ids)
    signature += std::to_string(id) + (editor.layerVisible(id) ? "v;" : "h;");
  if (signature != layerSignature) {
    layerIds = ids;
    layerList.clear();
    for (auto id : ids)
      layerList.add(("Layer " + std::to_string(id) +
                     (editor.layerVisible(id) ? "" : " [hidden]"))
                        .c_str());
    layerSignature = signature;
  }
  for (size_t i = 0; i < ids.size(); ++i)
    if (ids[i] == value.layer)
      layerList.value(i + 1);
  visibleLayer.value(editor.layerVisible(value.layer));
  redraw();
}
void Studio::poll() {
  if (xrSaveRequested.exchange(false))
    save();
  auto ready = [](auto &future) {
    return future.valid() && future.wait_for(std::chrono::seconds(0)) ==
                                 std::future_status::ready;
  };
  if (ready(xr)) {
    auto message = xr.get();
    xrButton.copy_label("Start XR painting");
    if (!message.empty())
      error = message;
    if (afterXR) {
      auto next = std::move(afterXR);
      afterXR = {};
      next();
    }
  }
  if (ready(io)) {
    auto result = io.get();
    if (!result.error.empty()) {
      error = result.error;
      afterIo = {};
    } else {
      if (result.load) {
        editor.load(std::move(result.document), result.path == recoveredFrom);
        currentPath = result.path == recoveredFrom ? "" : result.path;
        canvas.fit();
        syncControls();
      } else if (!result.exported) {
        editor.saved(result.version);
        currentPath = result.path;
        removeRecovery();
      }
      error.clear();
      if (afterIo) {
        auto next = std::move(afterIo);
        afterIo = {};
        next();
      }
    }
  }
  if (ready(recovery)) {
    auto message = recovery.get();
    if (!message.empty())
      error = message;
  }
  auto frame = editor.frame();
  dx::enable_menu(menu, "Edit/Undo", frame.undo);
  dx::enable_menu(menu, "Edit/Redo", frame.redo);
  dx::enable_menu(menu, "XR/Start XR painting", !xr.valid());
  dx::enable_menu(menu, "XR/Stop XR painting", xr.valid());
  for (auto path : {"Edit/Delete selection", "Edit/Duplicate selection",
                    "Edit/Transform selection...", "Edit/Recolor selection"})
    dx::enable_menu(menu, path, !frame.selected.empty());
  if (frame.undo)
    undoButton.activate();
  else
    undoButton.deactivate();
  if (frame.redo)
    redoButton.activate();
  else
    redoButton.deactivate();
  if (frame.revision != lastRevision ||
      frame.toolRevision != lastToolRevision) {
    lastToolRevision = frame.toolRevision;
    lastRevision = frame.revision;
    syncControls();
  }
  auto title = (currentPath.empty()
                    ? std::string("Untitled")
                    : std::filesystem::path(currentPath).filename().string()) +
               (frame.dirty ? " *" : "") + " — OpenBrush Native";
  if (!label() || title != label())
    copy_label(title.c_str());
  std::string message =
      error.empty()
          ? std::to_string(frame.document->strokes.size()) + " strokes · " +
                std::to_string(frame.selected.size()) + " selected · " +
                (xr.valid()
                     ? (xrStop ? "Stopping XR..." : "XR painting active")
                     : (io.valid() ? "File operation..."
                                   : "Left: tool · Right drag: orbit · "
                                     "Middle/Shift drag: pan · Wheel: zoom"))
          : error;
  status.copy_label(message.c_str());
  canvas.redraw();
  if (std::chrono::steady_clock::now() >= nextRecovery) {
    nextRecovery = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    if (frame.dirty)
      autosave();
  }
}
void Studio::quiet(std::function<void()> next) {
  if (io.valid()) {
    error = "Wait for the file operation to finish.";
    return;
  }
  if (xr.valid()) {
    xrStop = true;
    afterXR = std::move(next);
  } else
    next();
}
void Studio::maybeDiscard(std::function<void()> next) {
  editor.finish();
  if (!editor.frame().dirty) {
    next();
    return;
  }
  int answer = fl_choice("Save changes to the current drawing?", "Cancel",
                         "Save", "Discard");
  if (answer == 2) {
    removeRecovery();
    next();
  } else if (answer == 1 && save())
    afterIo = std::move(next);
}
void Studio::openDialog() {
  Fl_Native_File_Chooser chooser;
  chooser.title("Open artwork");
  chooser.type(Fl_Native_File_Chooser::BROWSE_FILE);
  chooser.filter("Open Brush\t*.tilt");
  if (chooser.show() == 0)
    open(chooser.filename());
}
void Studio::open(const std::string &path) {
  if (io.valid())
    return;
  io = std::async(std::launch::async, [path] {
    IoResult result;
    result.path = path;
    result.load = true;
    try {
      result.document = readTilt(path);
    } catch (const std::exception &e) {
      result.error = e.what();
    }
    return result;
  });
}
bool Studio::save(bool saveAs) {
  if (io.valid()) {
    error = "Wait for the file operation to finish.";
    return false;
  }
  editor.finish();
  std::string destination = currentPath;
  if (saveAs || destination.empty()) {
    Fl_Native_File_Chooser chooser;
    chooser.title("Save artwork");
    chooser.type(Fl_Native_File_Chooser::BROWSE_SAVE_FILE);
    chooser.options(Fl_Native_File_Chooser::SAVEAS_CONFIRM);
    chooser.filter("Open Brush\t*.tilt");
    chooser.preset_file(destination.empty() ? "Untitled.tilt"
                                            : destination.c_str());
    if (chooser.show() != 0)
      return false;
    destination = chooser.filename();
    if (std::filesystem::path(destination).extension() != ".tilt") {
      destination += ".tilt";
      if (std::filesystem::exists(destination) &&
          fl_choice("Replace existing artwork %s?", "Cancel", "Replace",
                    nullptr, destination.c_str()) != 1)
        return false;
    }
  }
  io = std::async(std::launch::async, [this, destination] {
    IoResult result;
    result.path = destination;
    try {
      auto snapshot = editor.snapshot();
      result.version = snapshot.revision;
      writeTilt(snapshot.document, destination);
    } catch (const std::exception &e) {
      result.error = e.what();
    }
    return result;
  });
  return true;
}
void Studio::exportDialog() {
  if (io.valid()) { error = "Wait for the file operation to finish."; return; }
  Fl_Native_File_Chooser chooser;
  chooser.title("Export glTF");
  chooser.type(Fl_Native_File_Chooser::BROWSE_SAVE_FILE);
  chooser.options(Fl_Native_File_Chooser::SAVEAS_CONFIRM);
  chooser.filter("glTF (embedded buffer)\t*.gltf\nBinary glTF\t*.glb");
  auto preset = currentPath.empty() ? std::filesystem::path("Untitled.gltf") : std::filesystem::path(currentPath).replace_extension(".gltf");
  chooser.preset_file(preset.c_str());
  if (chooser.show() != 0) return;
  std::string destination = chooser.filename();
  auto extension = std::filesystem::path(destination).extension().string();
  if (extension != ".gltf" && extension != ".glb") {
    destination += chooser.filter_value() == 1 ? ".glb" : ".gltf";
    if (std::filesystem::exists(destination) && fl_choice("Replace existing export %s?", "Cancel", "Replace", nullptr, destination.c_str()) != 1) return;
  }
  exportGltf(destination);
}
bool Studio::exportGltf(const std::string &path) {
  if (io.valid()) { error = "Wait for the file operation to finish."; return false; }
  editor.finish();
  error.clear();
  io = std::async(std::launch::async, [this, path] {
    IoResult result;
    result.path = path;
    result.exported = true;
    try { auto snapshot = editor.snapshot(); writeGltf(snapshot.document, path); }
    catch (const std::exception &e) { result.error = e.what(); }
    return result;
  });
  return true;
}
void Studio::startXR(unsigned frames) {
  if (xr.valid() || io.valid())
    return;
  editor.finish();
  xrStop = false;
  error.clear();
  xrButton.copy_label("Stop XR painting");
  xr = std::async(std::launch::async, [this, frames] {
    try {
      XrPainter painter(
          editor, [] {}, true, &xrStop, [this] { xrSaveRequested = true; });
      painter.run(frames);
      return std::string();
    } catch (const std::exception &e) {
      return std::string(e.what());
    }
  });
}
void Studio::autosave() {
  if (recovery.valid())
    return;
  auto path = recoveryPath;
  recovery = std::async(std::launch::async, [this, path] {
    try {
      auto snapshot = editor.snapshot(true);
      writeTilt(snapshot.document, path);
      return std::string();
    } catch (const std::exception &e) {
      return std::string("Recovery save failed: ") + e.what();
    }
  });
}
void Studio::removeRecovery() {
  if (recovery.valid()) {
    recovery.wait();
    recovery.get();
  }
  std::error_code ec;
  std::filesystem::remove(recoveryPath, ec);
  if (!recoveredFrom.empty())
    std::filesystem::remove(recoveredFrom, ec);
  recoveredFrom.clear();
}
void Studio::restore() {
  for (auto &entry : std::filesystem::directory_iterator(statePath())) {
    auto name = entry.path().filename().string();
    if (name.rfind("recovery-", 0) != 0 || entry.path().extension() != ".tilt")
      continue;
    try {
      auto pid = std::stoi(name.substr(9));
      if (pid > 0 && (kill(pid, 0) == 0 || errno == EPERM))
        continue;
    } catch (...) {
      continue;
    }
    int answer =
        fl_choice("A drawing from an interrupted session is available.",
                  "Keep for later", "Recover", "Discard");
    if (answer == 1) {
      recoveredFrom = entry.path();
      open(entry.path().string());
    } else if (answer == 2) {
      std::error_code ec;
      std::filesystem::remove(entry.path(), ec);
    }
    return;
  }
}
void Studio::close() {
  quiet([this] {
    maybeDiscard([this] {
      closing = true;
      removeRecovery();
      hide();
    });
  });
}
void Studio::command(const std::string &name) {
  try {
    if (name == "New")
      quiet([this] {
        maybeDiscard([this] {
          editor.load({});
          currentPath.clear();
          canvas.reset();
          syncControls();
        });
      });
    else if (name == "Open...")
      quiet([this] { maybeDiscard([this] { openDialog(); }); });
    else if (name == "Save")
      save();
    else if (name == "Save as...")
      save(true);
    else if (name == "Export glTF...")
      exportDialog();
    else if (name == "Quit")
      close();
    else if (name == "Undo")
      editor.undo();
    else if (name == "Redo")
      editor.redo();
    else if (name == "Select all")
      editor.selectAll();
    else if (name == "Delete selection")
      editor.deleteSelection();
    else if (name == "Duplicate selection")
      editor.duplicateSelection();
    else if (name == "Recolor selection")
      editor.recolorSelection(editor.tools().color);
    else if (name == "Transform selection...") {
      auto text = fl_input(
          "Translate X Y Z (meters), rotate Y (degrees), scale:", "0 0 0 0 1");
      if (text) {
        float x, y, z, angle, scale;
        if (sscanf(text, "%f %f %f %f %f", &x, &y, &z, &angle, &scale) != 5)
          throw std::runtime_error("Enter five numbers");
        float half = angle * 3.14159265f / 360;
        editor.transformSelection(
            {x, y, z}, {0, std::sin(half), 0, std::cos(half)}, scale);
      }
    } else if (name == "Fit drawing")
      canvas.fit();
    else if (name == "Reset view")
      canvas.reset();
    else if (name == "Start XR painting")
      startXR();
    else if (name == "Stop XR painting")
      xrStop = true;
    else if (name == "Controls")
      fl_message(
          "Desktop: left uses the tool; right drag orbits; middle/Shift drag "
          "pans; wheel zooms.\nXR: right trigger uses the tool; left trigger "
          "erases. Touch: X tools, Y eraser; A/B undo/redo.\n"
          "Left stick: brush/size. Right stick: color/opacity (between strokes).\n"
          "Left stick click: mirror. Right stick click: recenter tools.\n"
          "Firm trigger pull clicks palette buttons. Use the headset "
          "palette for brushes, colors, layers, editing and Save. Left X/menu "
          "toggles it; Hide leaves a Tools tab.\nSqueeze grips to move the "
          "canvas, or select Move and use triggers. Two grabs rotate/scale. "
          "Fit drawing brings artwork in front of you.\nF6 starts XR painting. "
          "Ctrl-S saves. Ctrl-Z / Ctrl-Shift-Z undo/redo.");
    else if (name == "About")
      fl_message("OpenBrush Native 0.1 — de-xr\nNative C++ / OpenXR / OpenGL / "
                 "FLTK. No external game engine.\nEarly implementation; brush "
                 "rendering differs from Unity Open Brush.\nMultiplayer and "
                 "cloud are outside scope.");
  } catch (const std::exception &e) {
    error = e.what();
  }
}
} // namespace ob
