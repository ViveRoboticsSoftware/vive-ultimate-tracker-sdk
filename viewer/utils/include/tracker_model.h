// VIVE Robotics, HTC. All Rights Reserved.
#ifndef VIVE_TRACKER_MODEL_H
#define VIVE_TRACKER_MODEL_H

#include "BLOpenGL.h"
#include "BLGLShader.h"
#include "BLTexture.h"
#include "BLMatrix3.h"
#include "BLColor.h"

#include <functional>
#include <mutex>

namespace vive {

class TrackerModel final {
  // render effects
  struct Effect {
    mlabs::balai::graphics::ShaderEffect* shader;
    mlabs::balai::graphics::shader::Constant const* color;
    mlabs::balai::graphics::shader::Sampler const* diffusemap;

    bool Init(char const* vp, char const* fp);
    void Reset() {
      BL_SAFE_RELEASE(shader);
      color = nullptr;
      diffusemap = nullptr;
    }
  } effects_[2]{};

  // vertex array objects
  GLuint vaos_[2] {0, 0};

  // RGB image and depth in RGB camera space
  mlabs::balai::graphics::Texture2D* diffuse_ { nullptr };
  //mlabs::balai::graphics::Texture2D* specular_;

  int num_vertices_{0};
  int num_triangles_{0};
  int num_lines_{0}; // wireframe

public:
  TrackerModel() = default;
  TrackerModel(TrackerModel const&) = delete;
  TrackerModel& operator=(TrackerModel const&) = delete;
  ~TrackerModel() = default;

  // load model
  bool Initialize(char const* model/* obj file only! */, char const* base_texture_name=nullptr);

  // return false isn't an error... meaning no frames available yet...
  bool Render(mlabs::balai::math::Matrix3 const& world,
              mlabs::balai::graphics::Color const& color=mlabs::balai::graphics::Color::White,
              bool wireframe=false);

  // clear everything
  void Destroy();
};

}
#endif
