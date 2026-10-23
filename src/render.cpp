// SPDX-License-Identifier: Apache-2.0
#include "render.h"
#include "reference-render.h"
#include "scene.h"
#include <cstddef>
namespace ob {
Mat4 identity() { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }
Mat4 modelMatrix(Transform t) {
  auto x = rotate(t.orientation, {t.scale, 0, 0}),
       y = rotate(t.orientation, {0, t.scale, 0}),
       z = rotate(t.orientation, {0, 0, t.scale});
  return {x.x, x.y, x.z, 0, y.x,          y.y,          y.z,          0,
          z.x, z.y, z.z, 0, t.position.x, t.position.y, t.position.z, 1};
}
Mat4 multiply(const Mat4 &a, const Mat4 &b) {
  Mat4 c{};
  for (int col = 0; col < 4; ++col)
    for (int row = 0; row < 4; ++row)
      for (int i = 0; i < 4; ++i)
        c[col * 4 + row] += a[i * 4 + row] * b[col * 4 + i];
  return c;
}
Mat4 viewMatrix(Vec3 p, Quat q) {
  q = normalized(q);
  q = {-q.x, -q.y, -q.z, q.w};
  auto x = rotate(q, {1, 0, 0}), y = rotate(q, {0, 1, 0}),
       z = rotate(q, {0, 0, 1}), t = rotate(q, p) * -1;
  return {x.x, x.y, x.z, 0, y.x, y.y, y.z, 0,
          z.x, z.y, z.z, 0, t.x, t.y, t.z, 1};
}
Mat4 projection(float left, float right, float down, float up, float near,
                float far) {
  float l = std::tan(left), r = std::tan(right), b = std::tan(down),
        t = std::tan(up);
  return {2 / (r - l),
          0,
          0,
          0,
          0,
          2 / (t - b),
          0,
          0,
          (r + l) / (r - l),
          (t + b) / (t - b),
          -(far + near) / (far - near),
          -1,
          0,
          0,
          -2 * far * near / (far - near),
          0};
}
namespace {
GLuint compile(GLenum type, const char *source) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &source, nullptr);
  glCompileShader(s);
  GLint ok;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char error[4096];
    glGetShaderInfoLog(s, sizeof(error), nullptr, error);
    glDeleteShader(s);
    throw std::runtime_error(error);
  }
  return s;
}
} // namespace
Renderer::Renderer() {
  const char *vs = R"(#version 330 core
layout(location=0)in vec3 position;layout(location=1)in vec3 normal;layout(location=2)in vec4 color;layout(location=3)in float emission;
uniform mat4 matrix;out vec4 tint;out vec3 n;out float glow;
void main(){gl_Position=matrix*vec4(position,1);tint=color;n=normal;glow=emission;})";
  const char *fs = R"(#version 330 core
in vec4 tint;in vec3 n;in float glow;out vec4 pixel;
void main(){float light=.45+.55*abs(dot(normalize(n),normalize(vec3(.3,.8,.6))));pixel=vec4(tint.rgb*mix(light,1.,glow),tint.a);})";
  auto vert = compile(GL_VERTEX_SHADER, vs),
       frag = compile(GL_FRAGMENT_SHADER, fs);
  shader = glCreateProgram();
  glAttachShader(shader, vert);
  glAttachShader(shader, frag);
  glLinkProgram(shader);
  glDeleteShader(vert);
  glDeleteShader(frag);
  GLint ok;
  glGetProgramiv(shader, GL_LINK_STATUS, &ok);
  if (!ok)
    throw std::runtime_error("Brush shader link failed");
  matrix = glGetUniformLocation(shader, "matrix");

  for (auto *batch : {&scene, &live}) {
    glGenVertexArrays(1, &batch->vao);
    glGenBuffers(1, &batch->vbo);
    glGenBuffers(1, &batch->ebo);
  }
}
void Renderer::abandon() {
  shader = 0;
  scene = {};
  live = {};
  cachedRevision = 0;
  if (references)
    references->abandon();
}
Renderer::~Renderer() {
  for (auto *b : {&scene, &live}) {
    glDeleteBuffers(1, &b->vbo);
    glDeleteBuffers(1, &b->ebo);
    glDeleteVertexArrays(1, &b->vao);
  }
  glDeleteProgram(shader);
}
void Renderer::upload(Batch &b, const Mesh &mesh) {
  glBindVertexArray(b.vao);
  glBindBuffer(GL_ARRAY_BUFFER, b.vbo);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b.ebo);
  glBufferData(GL_ARRAY_BUFFER, mesh.vertices.size() * sizeof(Vertex),
               mesh.vertices.data(), GL_DYNAMIC_DRAW);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size() * sizeof(uint32_t),
               mesh.indices.data(), GL_DYNAMIC_DRAW);
  for (unsigned i = 0; i < 4; ++i)
    glEnableVertexAttribArray(i);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        reinterpret_cast<void *>(offsetof(Vertex, position)));
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        reinterpret_cast<void *>(offsetof(Vertex, normal)));
  glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        reinterpret_cast<void *>(offsetof(Vertex, color)));
  glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        reinterpret_cast<void *>(offsetof(Vertex, emission)));
  b.count = mesh.indices.size();
}
void Renderer::render(const Batch &b) {
  if (b.count) {
    glBindVertexArray(b.vao);
    glDrawElements(GL_TRIANGLES, b.count, GL_UNSIGNED_INT, nullptr);
  }
}
void Renderer::extras(const std::vector<Stroke> &strokes, const Mat4 &vp) {
  if (strokes.empty())
    return;
  Mesh all;
  for (const auto &s : strokes) {
    auto mesh = brushMesh(s);
    auto start = uint32_t(all.vertices.size());
    all.vertices.insert(all.vertices.end(), mesh.vertices.begin(),
                        mesh.vertices.end());
    for (auto index : mesh.indices)
      all.indices.push_back(start + index);
  }
  glUseProgram(shader);
  glUniformMatrix4fv(matrix, 1, GL_FALSE, vp.data());
  upload(live, all);
  render(live);
  glBindVertexArray(0);
  glUseProgram(0);
  if (glGetError() != GL_NO_ERROR)
    throw std::runtime_error("OpenGL overlay rendering failed");
}
void Renderer::draw(const Document &d, const std::vector<Stroke> *active,
                    const Mat4 &vp, int w, int h, uint64_t revision,
                    Color background) {
  glViewport(0, 0, w, h);
  glDisable(GL_SCISSOR_TEST);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                      GL_ONE_MINUS_SRC_ALPHA);
  glClearColor(background.r, background.g, background.b, background.a);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  if (!references)
    references = std::make_unique<ReferenceRenderer>();
  auto images = d.references;
  images.erase(std::remove_if(images.begin(), images.end(),
                              [&](const auto &image) {
                                return !sourceLayerVisible(d, image.layer);
                              }),
               images.end());
  references->draw(images, vp);
  glUseProgram(shader);
  glUniformMatrix4fv(matrix, 1, GL_FALSE, vp.data());
  auto combine = [&](const std::vector<Stroke> &strokes, bool local) {
    Mesh all;
    for (const auto &s : strokes) {
      if (local && !sourceLayerVisible(d, s.layer))
        continue;
      auto mesh = local ? documentMesh(d, s) : brushMesh(s);
      uint32_t start = all.vertices.size();
      all.vertices.insert(all.vertices.end(), mesh.vertices.begin(),
                          mesh.vertices.end());
      for (auto index : mesh.indices)
        all.indices.push_back(start + index);
    }
    return all;
  };
  if (!revision || revision != cachedRevision) {
    upload(scene, combine(d.strokes, true));
    cachedRevision = revision;
  }
  render(scene);
  if (active && !active->empty()) {
    upload(live, combine(*active, false));
    render(live);
  }
  glBindVertexArray(0);
  glUseProgram(0);
  glDisable(GL_DEPTH_TEST);
  if (glGetError() != GL_NO_ERROR)
    throw std::runtime_error("OpenGL brush rendering failed");
}
} // namespace ob
