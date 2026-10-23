// SPDX-License-Identifier: Apache-2.0
#include "reference-render.h"
#include <set>
namespace ob {
ReferenceRenderer::ReferenceRenderer() {
  auto compile = [](GLenum type, const char *text) {
    auto shader = glCreateShader(type);
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint ok;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
      glDeleteShader(shader);
      throw std::runtime_error("Reference image shader failed");
    }
    return shader;
  };
  auto vertex = compile(GL_VERTEX_SHADER, R"(#version 330 core
layout(location=0)in vec2 pos;layout(location=1)in vec2 uv;
uniform mat4 matrix;uniform float aspect;out vec2 coord;
void main(){coord=uv;gl_Position=matrix*vec4(pos.x,pos.y/aspect,0,1);})");
  auto fragment = compile(GL_FRAGMENT_SHADER, R"(#version 330 core
in vec2 coord;uniform sampler2D pixels;uniform float opacity;out vec4 color;
void main(){color=texture(pixels,coord);color.a*=opacity;})");
  program = glCreateProgram();
  glAttachShader(program, vertex);
  glAttachShader(program, fragment);
  glLinkProgram(program);
  glDeleteShader(vertex);
  glDeleteShader(fragment);
  GLint ok;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (!ok)
    throw std::runtime_error("Reference image program failed");
  const float vertices[] = {-.5f, -.5f, 0, 1, .5f, -.5f, 1, 1, -.5f, .5f, 0, 0,
                            -.5f, .5f,  0, 0, .5f, -.5f, 1, 1, .5f,  .5f, 1, 0};
  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &vbo);
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                        reinterpret_cast<void *>(2 * sizeof(float)));
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glBindVertexArray(0);
}
ReferenceRenderer::~ReferenceRenderer() {
  for (auto &[key, t] : textures)
    glDeleteTextures(1, &t.id);
  glDeleteBuffers(1, &vbo);
  glDeleteVertexArrays(1, &vao);
  glDeleteProgram(program);
}
void ReferenceRenderer::abandon() {
  program = vao = vbo = 0;
  textures.clear();
}
void ReferenceRenderer::draw(const std::vector<ReferenceImage> &images,
                             const Mat4 &vp) {
  std::set<const RasterImage *> used;
  for (const auto &image : images)
    used.insert(image.pixels.get());
  for (auto it = textures.begin(); it != textures.end();)
    if (!used.count(it->first)) {
      glDeleteTextures(1, &it->second.id);
      it = textures.erase(it);
    } else
      ++it;
  if (images.empty())
    return;
  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glDisable(GL_CULL_FACE);
  glActiveTexture(GL_TEXTURE0);
  glUseProgram(program);
  glBindVertexArray(vao);
  glUniform1i(glGetUniformLocation(program, "pixels"), 0);
  for (const auto &image : images) {
    if (!image.pixels)
      continue;
    auto &texture = textures[image.pixels.get()];
    if (!texture.id) {
      texture.pixels = image.pixels;
      glGenTextures(1, &texture.id);
      glBindTexture(GL_TEXTURE_2D, texture.id);
      glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.pixels->width,
                   image.pixels->height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                   image.pixels->rgba.data());
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else
      glBindTexture(GL_TEXTURE_2D, texture.id);
    auto matrix = multiply(vp, modelMatrix(image.pose));
    glUniformMatrix4fv(glGetUniformLocation(program, "matrix"), 1, GL_FALSE,
                       matrix.data());
    glUniform1f(glGetUniformLocation(program, "aspect"),
                float(image.pixels->width) / image.pixels->height);
    glUniform1f(glGetUniformLocation(program, "opacity"), image.opacity);
    glDrawArrays(GL_TRIANGLES, 0, 6);
  }
  glBindVertexArray(0);
  glUseProgram(0);
  glBindTexture(GL_TEXTURE_2D, 0);
  glDepthMask(GL_TRUE);
  glEnable(GL_DEPTH_TEST);
}
} // namespace ob
