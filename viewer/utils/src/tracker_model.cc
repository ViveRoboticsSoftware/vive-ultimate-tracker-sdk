#include "../include/tracker_model.h"
#include "../include/image_io.h"

#include "BLGraphics.h"
#include "BLPrimitives.h"

#include <vector>
#include <fstream>

using namespace mlabs::balai::graphics;

namespace vive {

bool TrackerModel::Effect::Init(char const* vp, char const* fp) {
  Reset();
  if (vp && fp) {
    GLuint program = CreateGLProgram(vp, fp);
    if (program) {
      auto s = new GLProgram(0);
      if (s && s->Init(program)) {
        shader = s;
        diffusemap = s->FindSampler("diffusemap");
        color = s->FindConstant("color");
        return true;
      }
      glDeleteProgram(program);
      BL_SAFE_RELEASE(s);
    }
  }
  return false;
}

// mesh from file

void change_extension_inplace(char* filename, const char* new_ext) {
  char* dot = nullptr;
  char* slash = nullptr;

  // Find last dot and slash
  for (char* p = filename; *p; ++p) {
    if (*p == '.') {
      dot = p;
    } else if (*p == '/' || *p == '\\') {
      slash = p;
    }
  }

  // Only process dot if it's after last slash
  if (dot && (!slash || dot > slash)) {
    // Replace extension
    if (new_ext[0] == '.') {
      strcpy(dot, new_ext);
    } else {
      strcpy(dot + 1, new_ext);
    }
  } else {
    // Append extension
    strcat(filename, new_ext[0] == '.' ? new_ext : ".");
    if (new_ext[0] != '.') {
      strcat(filename, new_ext);
    }
  }
}

struct Model {
  struct XYZ { float x, y, z; };
  struct Texcoord {float u, v; };
  struct Vertex {
    XYZ p, n;
    Texcoord t;
  };

  std::vector<Vertex> vertices;
  std::vector<int> triangle_list;
  std::vector<int> line_list;
  std::vector<uint8_t> tex_data;
  int tex_w, tex_h, tex_c;

  bool read_from_cache_file(char const* filename) {
    vertices.clear();
    triangle_list.clear();
    line_list.clear();
    tex_data.clear();
    tex_w = tex_h = tex_c = 0;
    FILE* cache = fopen(filename, "rb");
    if (cache) {
      char magic[32]{}, sect[8]{};
      fread(magic, 1, 24, cache);

      int num_verts = 0;
      fread(&sect, 1, 4, cache);
      fread(&num_verts, 1, 4, cache);
      vertices.resize(num_verts);
      fread(vertices.data(), 1, num_verts*sizeof(Vertex), cache);

      int num_tris = 0;
      fread(&sect, 1, 4, cache);
      fread(&num_tris, 1, 4, cache);
      triangle_list.resize(num_tris);
      fread(triangle_list.data(), 1, num_tris*sizeof(int), cache);

      int num_lines = 0;
      fread(&sect, 1, 4, cache);
      fread(&num_lines, 1, 4, cache);
      line_list.resize(num_lines);
      fread(line_list.data(), 1, num_lines*sizeof(int), cache);

      fread(&sect, 1, 4, cache);
      fread(&tex_w, 1, 4, cache);
      fread(&tex_h, 1, 4, cache);
      fread(&tex_c, 1, 4, cache);
      int const tex_size = tex_w*tex_h*tex_c;
      tex_data.resize(tex_size);
      fread(tex_data.data(), 1, tex_size, cache);
#if 0
      LOGW("read_from_cache_file %s", filename);
      LOGW(" vertices: %d", (int) vertices.size());
      LOGW("triangles: %d/3", (int) triangle_list.size());
      LOGW("    lines: %d/2", (int) line_list.size());
      LOGW("  texture: %dx%dx%d %dB", tex_w, tex_h, tex_c, (int)tex_data.size());
#endif
      return true;
    }

    return false;
  }

  bool write_cache_file(char const* filename) {
    FILE* cache = fopen(filename, "wb");
    if (cache) {
      fwrite("vive ultimate tracker@TM", 1, 24, cache);

      fwrite("vert", 1, 4, cache);
      int const num_verts = (int) vertices.size();
      fwrite(&num_verts, 1, 4, cache);
      fwrite(vertices.data(), 1, num_verts*sizeof(Vertex), cache);

      fwrite("tris", 1, 4, cache);
      int const num_tris = (int) triangle_list.size();
      fwrite(&num_tris, 1, 4, cache);
      fwrite(triangle_list.data(), 1, num_tris*sizeof(int), cache);

      fwrite("lins", 1, 4, cache);
      int const num_lines = (int) line_list.size();
      fwrite(&num_lines, 1, 4, cache);
      fwrite(line_list.data(), 1, num_lines*sizeof(int), cache);

      fwrite("text", 1, 4, cache);
      fwrite(&tex_w, 1, 4, cache);
      fwrite(&tex_h, 1, 4, cache);
      fwrite(&tex_c, 1, 4, cache);
      fwrite(tex_data.data(), 1, tex_data.size(), cache);

      fclose(cache);
      return true;
    }
    return false;
  }

  bool read_from_file(char const* obj, char const* base_texture_name) {
    vertices.clear();
    triangle_list.clear();
    line_list.clear();
    tex_data.clear();
    tex_w = tex_h = tex_c = 0;

    struct FVertex { int v, t, n; };
    struct Triangle { FVertex a, b, c; };
    std::vector<XYZ> position;
    std::vector<XYZ> normals;
    std::vector<Texcoord> texcoords;
    std::vector<FVertex> face(8);
    std::vector<Triangle> triangles;

    constexpr float scale = 100.0f;
    float x, y, z;
    int read_section = 0;
    int num_lines = 0;

    std::string mtl;
    std::ifstream file(obj);
    for (std::string line; std::getline(file, line); ++num_lines) {
      if (line.empty()) {
        continue;
      }

      char const* c_str = line.c_str();
      if ('#'==*c_str) {
        //LOGI(c_str);
        continue;
      } else if ('v'==*c_str) {
        if (' '==c_str[1]) {
          if (3==std::sscanf(c_str+2, "%f %f %f", &x, &y, &z)) {
            position.push_back({scale*x,scale*y,scale*z});
            read_section = 1;
          }
        } else if ('t'==c_str[1]) {
          if (2==std::sscanf(c_str+2, " %f %f", &x, &y)) {
            texcoords.push_back({x, 1.0f-y});
            read_section = 2;
          }
        } else if ('n'==c_str[1]) { // normal
          if (3==std::sscanf(c_str+2, " %f %f %f", &x, &y, &z)) {
            normals.push_back({x,y,z});
            read_section = 3;
          }
        }
      } else if ('f'==*c_str && ' '==c_str[1]) {
        read_section = 4;
        // f v1/vt1 v2/vt2 v3/vt3 ...
        // f v1/vt1/vn1 v2/vt2/vn2 v3/vt3/vn3 ...
        // f v1//vn1 v2//vn2 v3//vn3 ...
        face.clear();
        int fv[3] = { 0, 0, 0 };
        int fn = 0;
        char const* s = c_str + 2;
        for (char const* e=s+1;fn<3;++e) {
          if ('/'==*e) {
            if (s<e) {
              fv[fn] = atoi(s);
            }
            ++fn;
            s = ++e;
          } else if (' '==*e) {
            if (s<e) {
              fv[fn] = atoi(s);
            }
            face.push_back({fv[0]-1, fv[1]-1, fv[2]-1});
            fn = 0;
            s = ++e;
          } else if ('\0'==*e) {
            if (s<e) {
              fv[fn] = atoi(s);
            }
            face.push_back({fv[0]-1, fv[1]-1, fv[2]-1});
            break;
          }
        }

        int const face_verts = (int)face.size();
        if (face_verts>=3) {
          auto const& v0 = face[0];
          for (int i=2; i<face_verts; ++i) {
            triangles.push_back({v0, face[i-1], face[i]});
          }
        }
      } else if (0==read_section && position.empty()) {
        if (0==memcmp(c_str, "mtllib ", 7)) {
          mtl = c_str + 7;
        }
      }
    }
    file.close();

    if (!mtl.empty()) {
      // extract
      char const* c = strrchr(obj, '/');
      if (!c) {
        c = strrchr(obj, '\\');
      }
      char path[256];
      char* s = path;
      if (c) {
        memcpy(s, obj, c - obj + 1);
        s += c - obj + 1;
      } else {
        memcpy(s, "./", 2);
        s += 2;
      }
      sprintf(s, "%s", mtl.c_str());
      file.open(path);

      num_lines = 0;
      for (std::string line; std::getline(file, line); ++num_lines) {
        if (char const* c_str = line.c_str()) {
          // "map_Ka ": ambient map
          // "map_Kd ": diffuse map
          // "map_Ks ": specular map
          if (0==memcmp("map_Kd ", c_str, 7)) {
            sprintf(s, "%s", c_str + 7);
            mtl = path;
            base_texture_name = mtl.c_str();

            LOGE("map_kd texture: %s", mtl.c_str());
            break;
          }
        }
      }
    }

    int const total_vtxs = (int) position.size();
    int const total_nrms = (int) normals.size();
    int const total_tc0s = (int) texcoords.size();

    vertices.reserve(triangles.size()*2);
    triangle_list.reserve(triangles.size()*3);
    auto find_index = [this](Vertex const& v) {
      int const n = (int) vertices.size();
      for (int i=0; i<n; ++i) {
        auto const& b = vertices[i];
        if (b.p.x==v.p.x && b.p.y==v.p.y && b.p.z==v.p.z) {
          if (b.t.u==v.t.u && b.t.v==v.t.v) {
            return i;
          }
        }
      }
      vertices.push_back(v);
      return n;
    };

    struct Line { int a, b; };
    std::vector<Line> Lines;
    Vertex v;

    int ia, ib, ic;
    auto add_line = [&Lines](int a, int b) {
      if (a>b) {
        std::swap(a, b);
      }
      for (auto const& line : Lines) {
        if (line.a==a && line.b==b) {
          return;
        }
      }
      Lines.push_back({a, b});
    };

    for (auto const& tri : triangles) {
      auto const& a = tri.a;
      auto const& b = tri.b;
      auto const& c = tri.c;

      if (a.v<total_vtxs && a.n<total_nrms && a.t<total_tc0s &&
          b.v<total_vtxs && b.n<total_nrms && b.t<total_tc0s &&
          c.v<total_vtxs && c.n<total_nrms && c.t<total_tc0s) {
        v.p = position[a.v];
        if (total_nrms>0) {
          v.n = normals[a.n];
        }
        v.t = texcoords[a.t];
        triangle_list.push_back(ia=find_index(v));

        v.p = position[b.v];
        if (total_nrms>0) {
          v.n = normals[b.n];
        }
        v.t = texcoords[b.t];
        triangle_list.push_back(ib=find_index(v));

        v.p = position[c.v];
        if (total_nrms>0) {
          v.n = normals[c.n];
        }
        v.t = texcoords[c.t];
        triangle_list.push_back(ic=find_index(v));

        add_line(ia, ib);
        add_line(ib, ic);
        add_line(ic, ia);
      }
    }
    line_list.reserve(Lines.size());
    for (auto const& line : Lines) {
      line_list.push_back(line.a);
      line_list.push_back(line.b);
    }

    if (void* pixels = vive::read_image(base_texture_name, tex_w, tex_h, tex_c)) {
      int const tex_size = tex_w*tex_h*tex_c;
      tex_data.resize(tex_size);
      memcpy(tex_data.data(), pixels, tex_size);
      free(pixels);
    }
#if 0
    LOGE("read_from_file: %s", obj);
    LOGE(" vertices: %d", (int) vertices.size());
    LOGE("triangles: %d/3", (int) triangle_list.size());
    LOGE("    lines: %d/2", (int) line_list.size());
    LOGE("  texture: %dx%dx%d %dB", tex_w, tex_h, tex_c, (int)tex_data.size());
#endif
    return true;
  }

  bool read(char const* obj, char const* base_texture_name) {
    if (obj) {
      char cached_file_name[260];
      strncpy(cached_file_name, obj, 256);
      change_extension_inplace(cached_file_name, "model");
      //LOGE("obj file: %s -> %s", obj, cached_file_name);

      if (read_from_cache_file(cached_file_name)) {
        return true;
      } else if (read_from_file(obj, base_texture_name)) {
        write_cache_file(cached_file_name);
        return true;
      }
    }

    return false;
  }
};

bool TrackerModel::Initialize(char const* obj/* obj file only! */, char const* base_texture_name) {
  Model model;
  if (model.read(obj, base_texture_name)) {
    diffuse_ = Texture2D::New(0);
    if (3==model.tex_c) {
      diffuse_->UpdateImage(model.tex_w, model.tex_h, mlabs::balai::graphics::FORMAT_RGB8, model.tex_data.data());
    } else if (4==model.tex_c) {
      diffuse_->UpdateImage(model.tex_w, model.tex_h, mlabs::balai::graphics::FORMAT_RGBA8, model.tex_data.data()); // check!
    } else if (1==model.tex_c) {
      diffuse_->UpdateImage(model.tex_w, model.tex_h, mlabs::balai::graphics::FORMAT_I8, model.tex_data.data());
    }
  } else {
    return false;
  }

  char const* vp = R"(
    #version 330
    layout(location=0) in vec3 pos;
    layout(location=1) in vec3 normal;
    layout(location=2) in vec2 texcoord;
    uniform mat4 matWorldViewProj;
    out vec2 texcoord_;
    void main() {
      gl_Position = vec4(pos, 1.0)*matWorldViewProj;
      texcoord_ = texcoord;
    })";

  char const* fp1 = R"(
    #version 330
    uniform sampler2D diffusemap;
    uniform vec4 color;
    in vec2 texcoord_;
    layout(location=0) out vec4 c0;
    void main() {
      c0 = color*(texture(diffusemap, texcoord_)+vec4(0.05,0.05,0.05,1.0));
    })";

  char const* fp2 = R"(
    #version 330
    layout(location=0) out vec4 c0;
    uniform vec4 color;
    void main() {
      c0 = color;
    })";

  if (!effects_[0].Init(vp, fp1) || !effects_[1].Init(vp, fp2)) {
    effects_[0].Reset();
    return false;
  }

  assert(0==vaos_[0]);
  glGenVertexArrays(sizeof(vaos_)/sizeof(vaos_[0]), vaos_);
  if (0==vaos_[0]) {
    return false;
  }

  GLuint bos[3] = { 0, 0, 0 };
  glGenBuffers(sizeof(bos)/sizeof(bos[0]), bos);
  if (0 == bos[0]) {
    glDeleteVertexArrays(sizeof(vaos_)/sizeof(vaos_[0]), vaos_);
    memset(vaos_, 0, sizeof(vaos_));
    return false;
  }

  glBindVertexArray(vaos_[0]);
  glBindBuffer(GL_ARRAY_BUFFER, bos[0]);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bos[1]);

  // vertex buffer
  num_vertices_ = (int)model.vertices.size();
  glBufferData(GL_ARRAY_BUFFER, num_vertices_*32, model.vertices.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (GLvoid const*)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, (GLvoid const*)12);
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, (GLvoid const*)24);
  glDisableVertexAttribArray(3);

  // triangle index buffer
  int const num_tri_indices = (int) model.triangle_list.size();
  int const* tri_ib = model.triangle_list.data();
  num_triangles_ = num_tri_indices/3;
  if (num_vertices_<65536) {
    uint16_t* const ib_data = (uint16_t*) tri_ib;
    uint16_t* dst = ib_data;
    for (int i=0; i<num_tri_indices; ++i) {
      *dst++ = (uint16_t) *tri_ib++;
    }
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, num_tri_indices*2, ib_data, GL_STATIC_DRAW);
  } else {
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, num_tri_indices*4, tri_ib, GL_STATIC_DRAW);
  }

  // line list vao
  glBindVertexArray(vaos_[1]);
  glBindBuffer(GL_ARRAY_BUFFER, bos[0]);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bos[2]);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (GLvoid const*)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, (GLvoid const*)12);
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, (GLvoid const*)24);
  glDisableVertexAttribArray(3);

  // line list index buffer
  int const num_line_indices = (int) model.line_list.size();
  int const* line_ib = model.line_list.data();
  num_lines_ = num_line_indices/2;
  if (num_vertices_<65536) {
    uint16_t* const ib_data = (uint16_t*) line_ib; // IB buffer must be big enough
    uint16_t* dst = ib_data;
    for (int i=0; i<num_line_indices; ++i) {
      *dst++ = (uint16_t) *line_ib++;
    }
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, num_lines_*2*2, ib_data, GL_STATIC_DRAW);
  } else {
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, num_lines_*4*2, line_ib, GL_STATIC_DRAW);
  }

  glBindVertexArray(0);

  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  glDeleteBuffers(sizeof(bos)/sizeof(bos[0]), bos);

  return true;
}

void TrackerModel::Destroy() {
  BL_SAFE_RELEASE(diffuse_);
  //BL_SAFE_RELEASE(specular_);

  for (int i=0; i<(int)(sizeof(effects_)/sizeof(effects_[0])); ++i) {
    effects_[i].Reset();
  }

  if (vaos_[0]) {
    glDeleteVertexArrays(sizeof(vaos_)/sizeof(vaos_[0]), vaos_);
    memset(vaos_, 0, sizeof(vaos_));
  }
}

bool TrackerModel::Render(mlabs::balai::math::Matrix3 const& world,
                          mlabs::balai::graphics::Color const& color,
                          bool wireframe) {
  Renderer& renderer = Renderer::GetInstance();
  renderer.PushState();
  renderer.PushWorldMatrix(world);

  if (color.a<255) {
    renderer.SetBlendMode(GFXBLEND_SRCALPHA, GFXBLEND_INVSRCALPHA);
  }

  auto const& fx = effects_[wireframe];
  if (fx.shader) {
    glBindVertexArray(vaos_[wireframe]);

    renderer.SetEffect(fx.shader);

    float const c[4] = { color.r/255.0f, color.g/255.0f, color.b/255.0f, color.a/255.0f }; 
    fx.shader->BindConstant(fx.color, c);
    if (!wireframe) {
      fx.shader->BindSampler(fx.diffusemap, diffuse_);
    }
    renderer.CommitChanges();

    GLenum const ib_type = (num_vertices_<65536) ? GL_UNSIGNED_SHORT:GL_UNSIGNED_INT;
    if (!wireframe) {
      glDrawElements(GL_TRIANGLES, num_triangles_*3, ib_type, NULL);
    } else {
      glDrawElements(GL_LINES, num_lines_*2, ib_type, NULL);
    }

    glBindVertexArray(0);
  }
#if 0
  glBindVertexArray(vaos_[0]);

  float depthscale[3] = {
    (depth_far_-depth_near_)*1.00394f, depth_near_, depth_far_ // depth scale, depth near, depth far extend
  };
  float const depth_solid_colors[12] = {
    color_r_, color_g_, color_b_, 1.0f, // near color
    0.3f, 0.3f, 0.3f, 1.0f, // far color
    1.0f, 0.0f, 0.0f, 1.0f, // invalid depth color
  };

  LOD const& lod1 = lods_[lod<num_lods_ ? lod:(num_lods_-1)];
  if (colormap) {
    auto const& fx = effects_[0];
    renderer.SetEffect(fx.shader);
    fx.shader->BindConstant(fx.instances, tile_params_.instances);
    fx.shader->BindConstant(fx.mapscale, tile_params_.mapscale);
    fx.shader->BindConstant(fx.depthscale, depthscale);
    fx.shader->BindSampler(fx.colormap, rgb_);
    fx.shader->BindSampler(fx.depthmap, depth_);
    renderer.CommitChanges();

    LOD const& lod2 = wireframe ? lods_[0]:lod1;
    glDrawElementsInstanced(GL_TRIANGLE_STRIP, lod2.count, GL_UNSIGNED_SHORT,
                            (void const*) size_t(lod2.offset), tile_params_.count);
  } else {
    auto const& fx = effects_[1];
    renderer.SetBlendMode(GFXBLEND_SRCALPHA, GFXBLEND_INVSRCALPHA);
    renderer.SetEffect(fx.shader);
    fx.shader->BindConstant(fx.instances, tile_params_.instances);
    fx.shader->BindConstant(fx.mapscale, tile_params_.mapscale);
    fx.shader->BindConstant(fx.depthscale, depthscale);
    fx.shader->BindSampler(fx.depthmap, depth_);
    fx.shader->BindConstant(fx.colors, depth_solid_colors);
    renderer.CommitChanges();
    glDrawElementsInstanced(GL_TRIANGLE_STRIP, lod1.count, GL_UNSIGNED_SHORT,
                            (void const*) size_t(lod1.offset), tile_params_.count);
  }

  if (wireframe) {
    renderer.SetZTest(GFXCMP_LESSEQUAL);
    renderer.SetBlendMode(GFXBLEND_SRCALPHA, GFXBLEND_INVSRCALPHA);

    depthscale[1] -= 0.001f; // depth near -1mm

    glBindVertexArray(vaos_[1]);
    auto const& fx = effects_[1];
    renderer.SetEffect(fx.shader);
    fx.shader->BindConstant(fx.instances, tile_params_.instances);
    fx.shader->BindConstant(fx.mapscale, tile_params_.mapscale);
    fx.shader->BindConstant(fx.depthscale, depthscale);
    fx.shader->BindSampler(fx.depthmap, depth_);
    fx.shader->BindConstant(fx.colors, depth_solid_colors);
    renderer.CommitChanges();
    glDrawElementsInstanced(GL_LINES, lod1.count2, GL_UNSIGNED_SHORT,
                            (void const*) size_t(lod1.offset2), tile_params_.count);
  }
#endif

  renderer.PopWorldMatrix();
  renderer.PopState();
  return true;
}

} // namespace vive
