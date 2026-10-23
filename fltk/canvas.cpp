// SPDX-License-Identifier: Apache-2.0
#include "studio.h"
#include <fstream>
namespace ob {
Quat Canvas::orientation() const {
  Quat y{0, std::sin(yaw / 2), 0, std::cos(yaw / 2)},
      p{std::sin(pitch / 2), 0, 0, std::cos(pitch / 2)};
  return {y.w * p.x, y.y * p.w, -y.y * p.x, y.w * p.w};
}
Canvas::Canvas(int x, int y, int w, int h, Editor &value)
    : Fl_Gl_Window(x, y, w, h), editor(value) {
  mode(FL_RGB | FL_ALPHA | FL_DOUBLE | FL_DEPTH);
  end();
}
Canvas::~Canvas() {
  if (renderer) {
    if (context()) {
      make_current();
    } else
      renderer->abandon();
    renderer.reset();
  }
}
Transform Canvas::referencePlacement() const {
  return compose(inverse(editor.canvas()),
                 {target, orientation(), distance * .7f});
}
Point Canvas::point(int x, int y) const {
  float aspect = float(w()) / std::max(1, h());
  float nx = float(x) / std::max(1, w()), ny = float(y) / std::max(1, h());
  auto q = orientation();
  auto position =
      target + rotate(q, {(2 * nx - 1) * std::tan(.55f) * distance * aspect,
                          (1 - 2 * ny) * std::tan(.55f) * distance, 0});
  return {position, q, 1,
          uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                       .count())};
}
void Canvas::sample(int x, int y) {
  auto p = point(x, y);
  auto settings = editor.tools();
  if (settings.tool == Tool::Select || settings.tool == Tool::Recolor) {
    auto camera = target + rotate(orientation(), {0, 0, distance});
    bool selected = editor.selectRayRoom(
        camera, p.position - camera, settings.eraser, Fl::event_state(FL_CTRL));
    if (selected && settings.tool == Tool::Recolor)
      editor.recolorSelection(settings.color);
  } else
    editor.sampleRoom(1, p);
  redraw();
}
void Canvas::draw() {
  if (lastContext != context()) {
    if (renderer)
      renderer->abandon();
    renderer.reset();
    lastContext = context();
  }
  if (!renderer)
    renderer = std::make_unique<Renderer>();
  auto frame = editor.frame();
  auto q = orientation();
  auto camera = target + rotate(q, {0, 0, distance});
  float aspect = float(pixel_w()) / std::max(1, pixel_h());
  float horizontal = std::atan(std::tan(.55f) * aspect);
  unsigned char r, g, b;
  Fl::get_color(FL_BACKGROUND2_COLOR, r, g, b);
  renderer->draw(
      *frame.document, &frame.active,
      multiply(multiply(projection(-horizontal, horizontal, -.55f, .55f),
                        viewMatrix(camera, q)),
               modelMatrix(frame.navigation)),
      pixel_w(), pixel_h(), frame.revision,
      {r / 255.f, g / 255.f, b / 255.f, 1});
}
int Canvas::handle(int event) {
  int x = Fl::event_x(), y = Fl::event_y();
  if (event == FL_PUSH) {
    take_focus();
    dragButton = Fl::event_button();
    lastX = x;
    lastY = y;
    if (dragButton == 1 && !Fl::event_state(FL_SHIFT)) {
      painting = true;
      sample(x, y);
    }
    return 1;
  }
  if (event == FL_DRAG) {
    if (painting)
      sample(x, y);
    else if (dragButton == 3) {
      yaw += (x - lastX) * .008f;
      pitch = std::clamp(pitch + (y - lastY) * .008f, -1.5f, 1.5f);
    } else if (dragButton == 2 || dragButton == 1) {
      auto q = orientation();
      float scale = 2 * std::tan(.55f) * distance / std::max(1, h());
      target =
          target + rotate(q, {-(x - lastX) * scale, (y - lastY) * scale, 0});
    }
    lastX = x;
    lastY = y;
    redraw();
    return 1;
  }
  if (event == FL_RELEASE) {
    if (painting)
      editor.finish(1);
    painting = false;
    dragButton = 0;
    redraw();
    return 1;
  }
  if (event == FL_MOUSEWHEEL) {
    distance =
        std::clamp(distance * std::exp(Fl::event_dy() * .12f), .05f, 100.f);
    redraw();
    return 1;
  }
  if (event == FL_FOCUS || event == FL_UNFOCUS)
    return 1;
  return Fl_Gl_Window::handle(event);
}
void Canvas::reset() {
  editor.navigate({});
  target = {0, 1.3f, -1.2f};
  distance = 1.4f;
  yaw = pitch = 0;
  redraw();
}
void Canvas::fit() {
  auto frame = editor.frame();
  Vec3 low{1e30f, 1e30f, 1e30f}, high{-1e30f, -1e30f, -1e30f};
  bool any = false;
  for (const auto &s : frame.document->strokes)
    for (auto &point : s.points) {
      auto p = point;
      p.position = transform(frame.navigation, p.position);
      any = true;
      low = {std::min(low.x, p.position.x), std::min(low.y, p.position.y),
             std::min(low.z, p.position.z)};
      high = {std::max(high.x, p.position.x), std::max(high.y, p.position.y),
              std::max(high.z, p.position.z)};
    }
  for (const auto &image : frame.document->references) {
    float aspect = float(image.pixels->width) / image.pixels->height;
    for (float x : {-.5f, .5f})
      for (float y : {-.5f, .5f}) {
        auto p = transform(frame.navigation,
                           transform(image.pose, {x, y / aspect, 0}));
        any = true;
        low = {std::min(low.x, p.x), std::min(low.y, p.y),
               std::min(low.z, p.z)};
        high = {std::max(high.x, p.x), std::max(high.y, p.y),
                std::max(high.z, p.z)};
      }
  }
  if (!any) {
    reset();
    return;
  }
  target = (low + high) * .5f;
  distance = std::max(.1f, length(high - low) * 1.2f);
  redraw();
}
bool Canvas::capture(const std::filesystem::path &path) {
  make_current();
  draw();
  std::vector<unsigned char> pixels(pixel_w() * pixel_h() * 3);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, pixel_w(), pixel_h(), GL_RGB, GL_UNSIGNED_BYTE,
               pixels.data());
  std::ofstream out(path, std::ios::binary);
  out << "P6\n" << pixel_w() << ' ' << pixel_h() << "\n255\n";
  for (int y = pixel_h() - 1; y >= 0; --y)
    out.write(reinterpret_cast<const char *>(pixels.data() + y * pixel_w() * 3),
              pixel_w() * 3);
  return bool(out);
}
} // namespace ob
