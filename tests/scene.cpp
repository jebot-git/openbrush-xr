// SPDX-License-Identifier: Apache-2.0
#include "scene.h"
#include "editor.h"
#include "gltf.h"
#include "graphics.h"
#include "mesh.h"
#include "render.h"
#include <cassert>
#include <iostream>
using namespace ob;
void close(Vec3 a, Vec3 b) { assert(length(a - b) < 1e-4f); }
int main(int argc, char **argv) {
  assert(argc == 2);
  std::filesystem::path dir = argv[1];
  std::filesystem::create_directories(dir);
  assert(knownBrush("2241CD32-8BA2-48A5-9EE7-2CAEF7E9ED62"));
  assert(brushFor("2241CD32-8BA2-48A5-9EE7-2CAEF7E9ED62").emission == 1);
  Document d;
  float q = std::sqrt(.5f);
  d.metadata["SceneTransformInRoomSpace"] =
      Json::array({Json::array({10, 0, 20}), Json::array({0, q, 0, q}), 3});
  d.metadata["Layers"] = Json::array();
  d.metadata["Layers"].push_back(
      {{"Name", "Paint α"},
       {"Visible", true},
       {"Transform",
        Json::array({Json::array({0, 10, 0}), Json::array({0, 0, 0, 1}), 2})},
       {"Unknown", 42}});
  d.metadata["Layers"].push_back(
      {{"Name", "Hidden"},
       {"Visible", false},
       {"Transform",
        Json::array({Json::array({0, 0, 0}), Json::array({0, 0, 0, 1}), 0})}});
  Stroke s;
  s.brush = brushes()[0].guid;
  s.size = .01f;
  s.points = {{{.1f, 0, 0}, {}, 1, 0}, {{.2f, 0, 0}, {}, .5f, 1}};
  d.strokes = {s};
  auto hidden = s;
  hidden.layer = 1;
  d.strokes.push_back(hidden);
  auto pose = strokeTransform(d, 0);
  close(transform(pose, {.1f, 0, 0}), {1, 3, -1.4f});
  assert(std::abs(pose.scale - 6) < 1e-5f);
  assert(sourceLayerName(d, 0) == "Paint α" && !sourceLayerVisible(d, 1));
  assert(std::abs(strokeTransform(d, 1).scale - 3) < 1e-5f);
  auto mesh = documentMesh(d, s), original = brushMesh(s);
  assert(mesh.vertices.size() == original.vertices.size());
  for (size_t i = 0; i < mesh.vertices.size(); ++i) {
    close(mesh.vertices[i].position,
          transform(pose, original.vertices[i].position));
    close(mesh.vertices[i].normal,
          rotate(pose.orientation, original.vertices[i].normal));
  }
  writeTilt(d, dir / "source.tilt");
  auto loaded = readTilt(dir / "source.tilt");
  assert(loaded.metadata["Layers"] == d.metadata["Layers"]);
  close(loaded.strokes[0].points[0].position, {.1f, 0, 0});
  Editor editor;
  editor.load(loaded);
  assert(!editor.frame().dirty && !editor.layerVisible(1));
  assert(editor.layerName(0) == "Paint α" &&
         editor.frame().document->strokes.size() == 1);
  auto point = transform(pose, s.points[0].position);
  assert(editor.select(point, .001f));
  assert(editor.selectRay(point + Vec3{0, 0, 1}, {0, 0, -1}, .001f));
  editor.duplicateSelection({.2f, 0, 0});
  auto duplicate = editor.copy().strokes.back();
  close(transform(pose, duplicate.points[0].position), point + Vec3{.2f, 0, 0});
  assert(editor.undo());
  editor.selectAll();
  editor.transformSelection({0, .3f, 0}, {}, 1);
  close(transform(pose, editor.copy().strokes[0].points[0].position),
        point + Vec3{0, .3f, 0});
  assert(editor.undo());
  assert(editor.erase(point, .001f));
  assert(editor.copy().strokes.size() == 1);
  assert(editor.undo());
  auto settings = editor.tools();
  settings.mirror = true;
  settings.size = .015f;
  editor.configure(settings);
  Vec3 paint{.4f, 3, -1.5f};
  editor.sample(2, {paint, {}, 1, 10});
  auto active = editor.frame().active;
  assert(active.size() == 2);
  close(active[0].points[0].position, paint);
  close(active[1].points[0].position, {-paint.x, paint.y, paint.z});
  editor.finish(2);
  auto changed = editor.copy();
  assert(changed.strokes.size() == 4);
  close(transform(pose, changed.strokes[2].points[0].position), paint);
  assert(std::abs(changed.strokes[2].size * pose.scale - settings.size) <
         1e-6f);
  close(transform(pose, changed.strokes[3].points[0].position),
        {-paint.x, paint.y, paint.z});
  writeTilt(changed, dir / "edited.tilt");
  writeGltf(changed, dir / "edited.glb");
  auto reread = readTilt(dir / "edited.tilt");
  close(transform(strokeTransform(reread, 0),
                  reread.strokes[2].points[0].position),
        paint);
  editor.layerVisible(1, true);
  assert(editor.frame().dirty);
  writeTilt(editor.copy(), dir / "visible.tilt");
  auto visible = readTilt(dir / "visible.tilt");
  assert(sourceLayerVisible(visible, 1) &&
         visible.metadata["Layers"][0]["Unknown"] == 42);
  // Raw local poses and original transforms survive untouched round trips.
  writeTilt(loaded, dir / "roundtrip.tilt");
  bool invalid = false;
  loaded.metadata["Layers"][0]["Transform"] = {1, 2};
  try {
    validateScene(loaded);
  } catch (...) {
    invalid = true;
  }
  assert(invalid);
  NativeGraphics graphics;
  Renderer renderer;
  GLuint fbo, texture;
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 128, 128, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, nullptr);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         texture, 0);
  assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
  Document visual;
  visual.metadata["SceneTransformInRoomSpace"] =
      Json::array({Json::array({4, 0, 0}), Json::array({0, 0, 0, 1}), 1});
  visual.metadata["Layers"] = Json::array(
      {{{"Visible", true},
        {"Transform",
         Json::array({Json::array({0, 0, 0}), Json::array({0, 0, q, q}), 1})}},
       {{"Visible", false}}});
  Stroke line;
  line.brush = brushes()[4].guid;
  line.color = {1, 1, 1, 1};
  line.size = .08f;
  line.points = {{{-.3f, 0, 0}, {}, 1, 0}, {{.3f, 0, 0}, {}, 1, 1}};
  visual.strokes = {line};
  auto invisible = line;
  invisible.layer = 1;
  for (auto &p : invisible.points)
    p.position.x -= .8f;
  visual.strokes.push_back(invisible);
  renderer.draw(visual, nullptr, identity(), 128, 128, 0, {0, 0, 0, 1});
  auto pixel = [&](int x, int y) {
    std::array<unsigned char, 4> value;
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, value.data());
    return value;
  };
  assert(pixel(89, 64)[0] > 240);
  assert(pixel(64, 64)[0] == 0);
  assert(pixel(38, 64)[0] == 0);
  glDeleteFramebuffers(1, &fbo);
  glDeleteTextures(1, &texture);
  std::cout << "Scene/layers: source coordinates retained, composed "
               "transforms, legacy scale, names/visibility, world-space tools "
               "and edited exports passed\n";
}
