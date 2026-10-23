// SPDX-License-Identifier: Apache-2.0
#include "palette.h"
#include "scene.h"
#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <fstream>
#include <iomanip>
#include <sstream>
namespace ob {
namespace {
std::filesystem::path stylePath() {
  auto x = getenv("XDG_CONFIG_HOME"), home = getenv("HOME");
  return std::filesystem::path(x      ? x
                               : home ? std::string(home) + "/.config"
                                      : "/tmp") /
         "dome/appearance.json";
}
Color hex(std::string text, Color fallback) {
  unsigned value = 0;
  if (text.size() == 7 && text[0] == '#' &&
      sscanf(text.c_str() + 1, "%x", &value) == 1)
    return {((value >> 16) & 255) / 255.f, ((value >> 8) & 255) / 255.f,
            (value & 255) / 255.f, 1};
  return fallback;
}
const std::array<Color, 8> colorPresets = {{{1, 1, 1, 1},
                                            {1, .12f, .15f, 1},
                                            {1, .5f, .08f, 1},
                                            {1, .9f, .1f, 1},
                                            {.15f, .9f, .3f, 1},
                                            {.1f, .7f, 1, 1},
                                            {.55f, .3f, 1, 1},
                                            {1, .2f, .7f, 1}}};
const char *colorNames[] = {"White", "Red",  "Orange", "Yellow",
                            "Green", "Blue", "Purple", "Pink"};
bool same(Color a, Color b) {
  return std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b) < .03f;
}
} // namespace
const std::array<Color, 8> &presetColors() { return colorPresets; }
size_t closestPreset(Color color) {
  size_t best = 0;
  float score = 1e30f;
  for (size_t i = 0; i < colorPresets.size(); ++i) {
    auto c = colorPresets[i];
    float d = (color.r - c.r) * (color.r - c.r) +
              (color.g - c.g) * (color.g - c.g) +
              (color.b - c.b) * (color.b - c.b);
    if (d < score) {
      best = i;
      score = d;
    }
  }
  return best;
}
PaletteTheme paletteTheme() {
  PaletteTheme theme;
  Json config = Json::object();
  try {
    std::ifstream f(stylePath());
    if (f)
      f >> config;
  } catch (const std::exception &) {
  }
  if (!config.is_object())
    config = Json::object();
  for (auto name : {"font", "accent"})
    if (config.contains(name) && !config[name].is_string())
      config.erase(name);
  for (auto name : {"opacity", "font_size", "font_scale"})
    if (config.contains(name) &&
        (!config[name].is_number_integer() || config[name].get<int64_t>() < 0 ||
         config[name].get<int64_t>() > 4096))
      config.erase(name);
  if (config.contains("light") && !config["light"].is_boolean())
    config.erase("light");
  bool light = config.value("light", false);
  if (light) {
    theme.background = {.9f, .91f, .92f, .95f};
    theme.foreground = {.08f, .1f, .12f, 1};
    theme.button = {.8f, .82f, .85f, 1};
  }
  theme.accent =
      hex(config.value("accent", std::string("#6babff")), theme.accent);
  theme.background.a =
      std::clamp(config.value("opacity", 230) / 255.f, .45f, 1.f);
  int requested = std::clamp(config.value("font_size", 16), 8, 32) *
                  std::clamp(config.value("font_scale", 1), 1, 2);
  auto family = config.value("font", std::string("monospace"));
  auto slash = family.find(" / ");
  std::string faceStyle;
  if (slash != std::string::npos) {
    faceStyle = family.substr(slash + 3);
    family = family.substr(0, slash);
  }
  auto fontConfig = FcInitLoadConfigAndFonts();
  if (!fontConfig)
    throw std::runtime_error("Cannot load palette font configuration");
  auto pattern = FcPatternCreate();
  FcPatternAddString(pattern, FC_FAMILY,
                     reinterpret_cast<const FcChar8 *>(family.c_str()));
  if (!faceStyle.empty())
    FcPatternAddString(pattern, FC_STYLE,
                       reinterpret_cast<const FcChar8 *>(faceStyle.c_str()));
  FcPatternAddBool(pattern, FC_SCALABLE, FcFalse);
  FcPatternAddInteger(pattern, FC_SPACING, FC_MONO);
  FcPatternAddDouble(pattern, FC_PIXEL_SIZE, requested);
  FcConfigSubstitute(fontConfig, pattern, FcMatchPattern);
  FcDefaultSubstitute(pattern);
  FcResult result;
  auto match = FcFontMatch(fontConfig, pattern, &result);
  FcPatternDestroy(pattern);
  if (match) {
    FcChar8 *file = nullptr;
    if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch)
      theme.fontFile = reinterpret_cast<char *>(file);
    FcPatternDestroy(match);
  }
  FcConfigDestroy(fontConfig);
  FT_Library library{};
  if (FT_Init_FreeType(&library))
    throw std::runtime_error("Cannot initialize palette font renderer");
  FT_Face face{};
  if (theme.fontFile.empty() ||
      FT_New_Face(library, theme.fontFile.c_str(), 0, &face)) {
    FT_Done_FreeType(library);
    throw std::runtime_error("Cannot load palette font");
  }
  int magnify = 1;
  if (face->num_fixed_sizes) {
    int best = 0;
    for (int i = 1; i < face->num_fixed_sizes; ++i)
      if (std::abs(face->available_sizes[i].height - requested) <
          std::abs(face->available_sizes[best].height - requested))
        best = i;
    FT_Select_Size(face, best);
    magnify = std::clamp(
        int(std::lround(float(requested) / face->available_sizes[best].height)),
        1, 4);
  } else
    FT_Set_Pixel_Sizes(face, 0, requested);
  theme.pixels = std::max(8, int(face->size->metrics.height >> 6) * magnify);
  theme.ascent = int(face->size->metrics.ascender >> 6) * magnify;
  for (uint32_t c = 32; c < 127; ++c) {
    if (FT_Load_Char(face, c, FT_LOAD_RENDER | FT_LOAD_TARGET_MONO))
      continue;
    auto &b = face->glyph->bitmap;
    PaletteGlyph g;
    g.width = b.width * magnify;
    g.height = b.rows * magnify;
    g.left = face->glyph->bitmap_left * magnify;
    g.top = face->glyph->bitmap_top * magnify;
    g.advance = std::max(1, int(face->glyph->advance.x >> 6) * magnify);
    g.alpha.resize(g.width * g.height);
    for (unsigned y = 0; y < b.rows; ++y) {
      auto row =
          b.buffer + (b.pitch >= 0 ? y : b.rows - 1 - y) * std::abs(b.pitch);
      for (unsigned x = 0; x < b.width; ++x) {
        uint8_t a = b.pixel_mode == FT_PIXEL_MODE_MONO
                        ? ((row[x / 8] & (128 >> (x % 8))) ? 255 : 0)
                        : row[x];
        for (int dy = 0; dy < magnify; ++dy)
          for (int dx = 0; dx < magnify; ++dx)
            g.alpha[(y * magnify + dy) * g.width + x * magnify + dx] = a;
      }
    }
    theme.glyphs[c] = std::move(g);
  }
  FT_Done_Face(face);
  FT_Done_FreeType(library);
  return theme;
}
Palette::Palette() : theme(paletteTheme()) {}
void Palette::place(Transform head, const std::optional<Transform> &left) {
  viewer = head;
  pose.orientation = head.orientation;
  pose.scale = 1;
  pose.position =
      head.position + rotate(head.orientation, {-.18f, -.02f, -.65f});
  if (left && length(left->position - head.position) > .2f &&
      length(left->position - head.position) < 1.2f) {
    auto local =
        rotate(conjugate(head.orientation), left->position - head.position);
    local.x = std::clamp(local.x, -.22f, .15f);
    local.y = std::clamp(local.y + .18f, -.12f, .05f);
    local.z = std::clamp(local.z - .12f, -.9f, -.7f);
    pose.position = head.position + rotate(head.orientation, local);
  }
  anchored = true;
}
void Palette::toggle(Transform head, const std::optional<Transform> &left) {
  shown = !shown;
  if (shown)
    place(head, left);
  cancel();
}
const std::vector<PaletteButton> &Palette::layout(const EditorFrame &frame) {
  buttons.clear();
  int unit = std::max(1, (theme.pixels + 15) / 16);
  pixels.width = 640 * unit;
  pixels.height = 710 * unit;
  auto add = [&](int x, int y, int w, int h, std::string label, std::string cmd,
                 bool active = false, bool enabled = true) {
    buttons.push_back(
        {x, y, w, h, std::move(label), std::move(cmd), active, enabled});
  };
  add(480, 8, 148, 42, shown ? "Hide" : "Tools", shown ? "hide" : "expand");
  if (!shown) {
    for (auto &b : buttons) {
      b.x *= unit;
      b.y *= unit;
      b.w *= unit;
      b.h *= unit;
    }
    return buttons;
  }
  add(312, 8, 148, 42, "Fit drawing", "fit", false,
      !frame.document->strokes.empty() || !frame.document->references.empty());
  const char *tools[] = {"Paint", "Erase", "Select", "Recolor", "Move"};
  for (int i = 0; i < 5; ++i)
    add(12 + i * 124, 66, 116, 42, tools[i], "tool:" + std::to_string(i),
        int(frame.settings.tool) == i);
  for (size_t i = 0; i < brushes().size(); ++i)
    add(12 + (i % 4) * 156, 142 + (i / 4) * 52, 148, 44, brushes()[i].name,
        "brush:" + std::to_string(i),
        sameBrush(frame.settings.brush,brushes()[i].guid));
  for (int i = 0; i < 8; ++i) {
    add(12 + i * 78, 270, 70, 42, colorNames[i], "color:" + std::to_string(i),
        same(frame.settings.color, colorPresets[i]));
    buttons.back().swatch = true;
    buttons.back().color = colorPresets[i];
  }
  std::ostringstream size;
  size << std::fixed << std::setprecision(1)
       << (frame.settings.tool == Tool::Paint ? frame.settings.size
                                              : frame.settings.eraser) *
              1000
       << " mm";
  add(12, 350, 196, 42, "Size -", "size:-");
  add(222, 350, 196, 42, size.str(), "size:reset");
  add(432, 350, 196, 42, "Size +", "size:+");
  add(12, 406, 196, 42, "Opacity -", "alpha:-");
  add(222, 406, 196, 42,
      std::to_string(int(frame.settings.color.a * 100)) + "%", "alpha:reset");
  add(432, 406, 196, 42, "Opacity +", "alpha:+");
  add(12, 466, 148, 42, "Mirror", "mirror", frame.settings.mirror);
  add(168, 466, 148, 42, "Layer -", "layer:-");
  add(324, 466, 148, 42,
      (frame.currentLayerVisible ? "Layer " : "Hidden ") +
          std::to_string(frame.settings.layer),
      "layer:toggle", frame.currentLayerVisible);
  add(480, 466, 148, 42, "Layer +", "layer:+");
  add(12, 522, 148, 42, "Undo", "undo", false, frame.undo);
  add(168, 522, 148, 42, "Redo", "redo", false, frame.redo);
  add(324, 522, 148, 42, "Select all", "select");
  add(480, 522, 148, 42, "Duplicate", "duplicate", false,
      !frame.selected.empty());
  add(12, 578, 148, 42, "Delete", "delete", false, !frame.selected.empty());
  add(168, 578, 148, 42, "Recolor", "recolor", false, !frame.selected.empty());
  add(324, 578, 148, 42, "Save", "save");
  add(480, 578, 148, 42, "Reset view", "reset");
  for (auto &b : buttons) {
    b.x *= unit;
    b.y *= unit;
    b.w *= unit;
    b.h *= unit;
  }
  return buttons;
}
std::optional<PaletteHit> Palette::hit(Vec3 origin, Vec3 direction) const {
  if (!visible())
    return {};
  auto inv = inverse(pose);
  auto o = ob::transform(inv, origin),
       d = rotate(inv.orientation, normalized(direction, {0, 0, -1}));
  if (d.z >= -.00001f)
    return {};
  float distance = -o.z / d.z;
  if (distance < 0 || distance > 4)
    return {};
  auto p = o + d * distance;
  if (std::abs(p.x) > width / 2 || std::abs(p.y) > height / 2)
    return {};
  int x = (p.x / width + .5f) * pixels.width,
      y = (.5f - p.y / height) * pixels.height, index = -1;
  for (size_t i = 0; i < buttons.size(); ++i) {
    auto &b = buttons[i];
    if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) {
      index = i;
      break;
    }
  }
  if (!shown && index < 0)
    return {};
  return PaletteHit{distance, origin + normalized(direction) * distance, index};
}
bool Palette::activate(const std::string &cmd, Editor &editor,
                       const std::function<void()> &save) {
  if (cmd == "hide") {
    shown = false;
    hover = -1;
    signature.clear();
    return true;
  }
  if (cmd == "expand") {
    shown = true;
    signature.clear();
    return true;
  }
  if (cmd == "fit") {
    auto content = editor.frame();
    Vec3 low{1e30f, 1e30f, 1e30f}, high{-1e30f, -1e30f, -1e30f};
    bool any = false;
    for (auto &s : content.document->strokes)
      for (auto &raw : s.points) {
        auto p = raw;
        p.position = ob::transform(strokeTransform(*content.document, s.layer),
                                   p.position);
        any = true;
        low = {std::min(low.x, p.position.x), std::min(low.y, p.position.y),
               std::min(low.z, p.position.z)};
        high = {std::max(high.x, p.position.x), std::max(high.y, p.position.y),
                std::max(high.z, p.position.z)};
      }
    for (const auto &image : content.document->references) {
      float aspect = float(image.pixels->width) / image.pixels->height;
      for (float x : {-.5f, .5f})
        for (float y : {-.5f, .5f}) {
          auto p = ob::transform(image.pose, {x, y / aspect, 0});
          any = true;
          low = {std::min(low.x, p.x), std::min(low.y, p.y),
                 std::min(low.z, p.z)};
          high = {std::max(high.x, p.x), std::max(high.y, p.y),
                  std::max(high.z, p.z)};
        }
    }
    if (!any)
      return false;
    float scale =
        std::clamp(.6f / std::max(.01f, length(high - low)), .05f, 20.f);
    auto center = (low + high) * .5f;
    auto position = viewer.position +
                    rotate(viewer.orientation, {0, -.05f, -.9f}) -
                    rotate(viewer.orientation, center * scale);
    editor.navigate({position, viewer.orientation, scale});
    return true;
  }
  auto value = editor.tools();
  if (cmd.rfind("tool:", 0) == 0) {
    int index = std::stoi(cmd.substr(5));
    if (index < 0 || index > 4)
      return false;
    value.tool = Tool(index);
  } else if (cmd.rfind("brush:", 0) == 0) {
    value.brush = brushes().at(std::stoul(cmd.substr(6))).guid;
    value.tool = Tool::Paint;
  } else if (cmd.rfind("color:", 0) == 0) {
    int index = std::stoi(cmd.substr(6));
    if (index < 0 || index >= 8)
      return false;
    float a = value.color.a;
    value.color = colorPresets[index];
    value.color.a = a;
  } else if (cmd.rfind("size:", 0) == 0) {
    auto &size = value.tool == Tool::Paint ? value.size : value.eraser;
    size =
        cmd == "size:reset"
            ? (value.tool == Tool::Paint ? .015f : .03f)
            : std::clamp(size * (cmd == "size:+" ? 1.25f : .8f), .001f, .15f);
  } else if (cmd.rfind("alpha:", 0) == 0)
    value.color.a =
        cmd == "alpha:reset"
            ? 1
            : std::clamp(value.color.a + (cmd == "alpha:+" ? .1f : -.1f), .05f,
                         1.f);
  else if (cmd == "mirror")
    value.mirror = !value.mirror;
  else if (cmd == "layer:+")
    value.layer = std::min(value.layer + 1, uint32_t(999));
  else if (cmd == "layer:-")
    value.layer = value.layer ? value.layer - 1 : 0;
  else if (cmd == "layer:toggle") {
    editor.layerVisible(value.layer, !editor.layerVisible(value.layer));
    return true;
  } else if (cmd == "undo") {
    editor.undo();
    return true;
  } else if (cmd == "redo") {
    editor.redo();
    return true;
  } else if (cmd == "select") {
    editor.selectAll();
    return true;
  } else if (cmd == "duplicate") {
    editor.duplicateSelection();
    return true;
  } else if (cmd == "delete") {
    editor.deleteSelection();
    return true;
  } else if (cmd == "recolor") {
    editor.recolorSelection(value.color);
    return true;
  } else if (cmd == "reset") {
    editor.navigate({});
    return true;
  } else if (cmd == "save") {
    editor.finish();
    if (save)
      save();
    return true;
  } else
    return false;
  editor.configure(value);
  return true;
}
bool Palette::point(Vec3 origin, Vec3 direction, bool down, bool valid,
                    Editor &editor, const std::function<void()> &save,
                    float pressure) {
  layout(editor.frame());
  auto target = valid ? hit(origin, direction) : std::nullopt;
  hover = target ? target->button : -1;
  if (down && target && !latched) {
    latched = true;
    armed = !lastDown;
    clicked = false;
    armedButton = hover;
  }
  if (down && latched && armed && !clicked && pressure >= .65f &&
      hover == armedButton && hover >= 0 && buttons[hover].enabled) {
    clicked = true;
    if (activate(buttons[hover].command, editor, save))
      ++clicks;
  }
  bool consumed = latched || bool(target);
  if (!down) {
    latched = false;
    armed = false;
    clicked = false;
  }
  lastDown = down;
  return consumed;
}
void Palette::raster(const EditorFrame &frame) {
  int unit = pixels.width / 640;
  pixels.rgba.resize(pixels.width * pixels.height * 4);
  auto put = [&](int x, int y, Color c) {
    if (x < 0 || y < 0 || x >= pixels.width || y >= pixels.height)
      return;
    size_t i = (y * pixels.width + x) * 4;
    pixels.rgba[i] = std::clamp(c.r, 0.f, 1.f) * 255;
    pixels.rgba[i + 1] = std::clamp(c.g, 0.f, 1.f) * 255;
    pixels.rgba[i + 2] = std::clamp(c.b, 0.f, 1.f) * 255;
    pixels.rgba[i + 3] = std::clamp(c.a, 0.f, 1.f) * 255;
  };
  auto rect = [&](int x, int y, int w, int h, Color c) {
    for (int yy = y; yy < y + h; ++yy)
      for (int xx = x; xx < x + w; ++xx)
        put(xx, yy, c);
  };
  auto text = [&](int x, int y, int maxWidth, const std::string &s, Color c) {
    int cursor = x;
    for (unsigned char ch : s) {
      auto it = theme.glyphs.find(ch);
      if (it == theme.glyphs.end())
        continue;
      auto &g = it->second;
      if (cursor + g.advance > x + maxWidth)
        break;
      for (int yy = 0; yy < g.height; ++yy)
        for (int xx = 0; xx < g.width; ++xx)
          if (g.alpha[yy * g.width + xx] > 127)
            put(cursor + g.left + xx, y + theme.ascent - g.top + yy, c);
      cursor += g.advance;
    }
  };
  rect(0, 0, pixels.width, pixels.height,
       shown ? theme.background : Color{0, 0, 0, 0});
  if (shown) {
    text(14 * unit, 8 * unit, 612 * unit, "OpenBrush Native", theme.foreground);
    text(14 * unit, 39 * unit, 612 * unit, "Tools", theme.foreground);
    text(14 * unit, 115 * unit, 612 * unit, "Brushes", theme.foreground);
    text(14 * unit, 240 * unit, 612 * unit, "Colors", theme.foreground);
    text(14 * unit, 320 * unit, 612 * unit,
         frame.settings.tool == Tool::Paint ? "Brush size" : "Tool radius",
         theme.foreground);
  }
  for (size_t i = 0; i < buttons.size(); ++i) {
    auto &b = buttons[i];
    auto bg = b.swatch ? b.color : b.active ? theme.accent : theme.button;
    rect(b.x, b.y, b.w, b.h, bg);
    auto border =
        int(i) == hover || b.active ? theme.foreground : theme.background;
    rect(b.x, b.y, b.w, 2 * unit, border);
    rect(b.x, b.y + b.h - 2 * unit, b.w, 2 * unit, border);
    rect(b.x, b.y, 2 * unit, b.h, border);
    rect(b.x + b.w - 2 * unit, b.y, 2 * unit, b.h, border);
    Color fg = b.swatch ? Color{0, 0, 0, 1} : theme.foreground;
    if (!b.enabled)
      fg = {(fg.r + bg.r) * .5f, (fg.g + bg.g) * .5f, (fg.b + bg.b) * .5f, 1};
    text(b.x + 8 * unit, b.y + (b.h - theme.pixels) / 2, b.w - 16 * unit,
         b.label, fg);
  }
  if (shown) {
    text(14 * unit, 641 * unit, 612 * unit,
         "Left X/menu: palette. Move: use triggers.", theme.foreground);
    text(14 * unit, 673 * unit, 612 * unit,
         "One grab: move. Two grabs: rotate/scale.", theme.foreground);
  }
  ++pixels.revision;
}
const PaletteImage &Palette::image(const EditorFrame &frame) {
  if (reload.valid() &&
      reload.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
    try {
      theme = reload.get();
      signature.clear();
    } catch (const std::exception &) {
    }
  }
  auto now = std::chrono::steady_clock::now();
  if (now >= nextStyle) {
    nextStyle = now + std::chrono::seconds(1);
    std::error_code error;
    auto modified = std::filesystem::last_write_time(stylePath(), error);
    if (!error && modified != styleTime && !reload.valid()) {
      styleTime = modified;
      reload = std::async(std::launch::async, paletteTheme);
    }
  }
  layout(frame);
  std::string key = std::to_string(hover);
  for (auto &b : buttons)
    key +=
        b.label + b.command + (b.active ? "1" : "0") + (b.enabled ? "1" : "0");
  if (key != signature || pixels.rgba.empty()) {
    signature = std::move(key);
    raster(frame);
  }
  return pixels;
}
} // namespace ob
