// SPDX-License-Identifier: Apache-2.0
#include "reference.h"
#include "editor.h"
#include "graphics.h"
#include "render.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <jpeglib.h>
#include <png.h>
#include <unistd.h>
using namespace ob;
Bytes pngBytes() {
  const unsigned char rgba[] = {255, 0, 0,   255, 0,   255, 0, 0,
                                0,   0, 255, 255, 255, 255, 0, 255};
  png_image image{};
  image.version = PNG_IMAGE_VERSION;
  image.width = image.height = 2;
  image.format = PNG_FORMAT_RGBA;
  png_alloc_size_t size = 0;
  assert(
      png_image_write_to_memory(&image, nullptr, &size, 0, rgba, 0, nullptr));
  Bytes bytes(size);
  assert(png_image_write_to_memory(&image, bytes.data(), &size, 0, rgba, 0,
                                   nullptr));
  bytes.resize(size);
  return bytes;
}
int main(int argc, char **argv) {
  char pattern[] = "/tmp/openbrush-reference-test-XXXXXX";
  auto directory = mkdtemp(pattern);
  assert(directory);
  auto dir = std::filesystem::path(directory);
  auto pixels = decodeReference(pngBytes());
  assert(pixels->width == 2 && pixels->height == 2 && pixels->rgba[7] == 0);
  atomicWrite(dir / "reference.png", pngBytes());
  auto reference = readReference(dir / "reference.png");
  reference.layer = 4;
  reference.pose = {{0, 0, 0}, {}, 1};
  reference.opacity = 1;
  bool rejected = false;
  try {
    decodeReference(Bytes(20, 0));
  } catch (...) {
    rejected = true;
  }
  assert(rejected);
  rejected = false;
  try {
    decodeReference({255, 216, 0, 1, 2, 3, 4, 5, 6, 7});
  } catch (...) {
    rejected = true;
  }
  assert(rejected);
  // Exercise libjpeg decoding, including its in-memory error handler.
  jpeg_compress_struct jpeg{};
  jpeg_error_mgr error{};
  jpeg.err = jpeg_std_error(&error);
  jpeg_create_compress(&jpeg);
  unsigned char *encoded = nullptr;
  unsigned long size = 0;
  jpeg_mem_dest(&jpeg, &encoded, &size);
  jpeg.image_width = jpeg.image_height = 16;
  jpeg.input_components = 3;
  jpeg.in_color_space = JCS_RGB;
  jpeg_set_defaults(&jpeg);
  jpeg_start_compress(&jpeg, TRUE);
  unsigned char row[48];
  for (auto &v : row)
    v = 200;
  while (jpeg.next_scanline < jpeg.image_height) {
    JSAMPROW data = row;
    jpeg_write_scanlines(&jpeg, &data, 1);
  }
  jpeg_finish_compress(&jpeg);
  auto decoded = decodeReference(Bytes(encoded, encoded + size));
  assert(decoded->width == 16 && decoded->height == 16 &&
         decoded->rgba[3] == 255);
  std::free(encoded);
  jpeg_destroy_compress(&jpeg);
  Editor editor;
  editor.addReference(reference);
  assert(editor.frame().dirty && editor.copy().references.size() == 1);
  assert(editor.undo() && editor.copy().references.empty());
  assert(editor.redo());
  auto edited = reference;
  edited.opacity = .5f;
  edited.pose.position.x = .1f;
  editor.changeReference(0, edited);
  assert(editor.copy().references[0].opacity == .5f);
  assert(editor.undo());
  assert(editor.copy().references[0].pose.position.x == 0);
  assert(editor.layers() == std::vector<uint32_t>({0, 4}));
  editor.layerVisible(4, false);
  assert(editor.frame().document->references.empty());
  assert(editor.snapshot().document.references.size() == 1);
  auto doc = editor.copy();
  doc.attachments["unrelated.bin"] = {1, 2, 3};
  doc.metadata["UnknownField"] = 42;
  writeTilt(doc, dir / "image.tilt");
  std::filesystem::remove(dir / "reference.png");
  auto loaded = readTilt(dir / "image.tilt");
  assert(loaded.references.size() == 1 &&
         loaded.references[0].pixels->rgba == pixels->rgba &&
         loaded.references[0].layer == 4 &&
         loaded.references[0].name == "reference.png");
  assert(loaded.attachments.at("unrelated.bin") == Bytes({1, 2, 3}) &&
         loaded.metadata["UnknownField"] == 42);
  writeTilt(loaded, dir / "again.tilt");
  assert(readTilt(dir / "again.tilt").references.size() == 1);
  editor.removeReference(0);
  assert(editor.copy().references.empty());
  assert(editor.undo());
  loaded.references.clear();
  writeTilt(loaded, dir / "removed.tilt");
  assert(readTilt(dir / "removed.tilt").references.empty());
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
  Document draw;
  draw.references = {reference};
  auto color = [&](int x, int y) {
    std::array<unsigned char, 4> result;
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, result.data());
    return result;
  };
  renderer.draw(draw, nullptr, identity(), 128, 128, 0, {0, 0, 0, 0});
  auto topLeft = color(48, 80), bottomLeft = color(48, 48),
       topRight = color(80, 80);
  assert(topLeft[0] > 240 && topLeft[2] < 15);
  assert(bottomLeft[2] > 240 && bottomLeft[0] < 15);
  assert(topRight[3] < 15);
  draw.references[0].opacity = .5f;
  renderer.draw(draw, nullptr, identity(), 128, 128, 0, {0, 0, 0, 0});
  auto half = color(48, 80);
  assert(half[0] > 115 && half[0] < 140 && half[3] > 115 && half[3] < 140);
  Stroke line;
  line.brush = brushes()[4].guid;
  line.color = {1, 1, 1, 1};
  line.size = .1f;
  line.points = {{{-.4f, 0, 0}, {}, 1, 0}, {{.4f, 0, 0}, {}, 1, 1}};
  draw.strokes = {line};
  renderer.draw(draw, nullptr, identity(), 128, 128, 0, {0, 0, 0, 0});
  auto painted = color(64, 64);
  assert(painted[0] > 240 && painted[1] > 240 && painted[2] > 240);
  draw.references.clear();
  draw.strokes.clear();
  renderer.draw(draw, nullptr, identity(), 128, 128, 0, {0, 0, 0, 0});
  assert(color(48, 80)[3] == 0);
  glDeleteFramebuffers(1, &fbo);
  glDeleteTextures(1, &texture);
  if (argc > 1) {
    Document demo;
    reference.pose = {{0, 1.3f, -1.2f}, {}, .8f};
    reference.opacity = .7f;
    demo.references = {reference};
    writeTilt(demo, argv[1]);
  }
  std::filesystem::remove_all(dir);
  std::cout << "Reference images: PNG/JPEG, alpha, undo, layers, embedded "
               "recovery, removal and GPU orientation/underlay passed\n";
}
