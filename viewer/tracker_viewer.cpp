#include "ultimate_tracker.h"

#include "include/tracker_model.h"
#include "include/linux_toolkit.h"
#include "include/imgui_console.h"
#include "include/image_io.h"

#include "BLApplication.h"
#include "BLCStdMemory.h"
#include "BLGraphics.h"
#include "BLPrimitives.h"
#include "BLMath.h"
#include "BLQuaternion.h"

#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <vector>

BL_CSTD_MEMMGR_IMPLMENTATION(64<<20, 1<<20);

using namespace mlabs::balai::graphics;
using namespace mlabs::balai::math;
using namespace mlabs::balai::framework;

using namespace vive;

namespace {

constexpr int default_fov = 60;

constexpr float focus_distance = 30.0f; // cm

constexpr float xz_grid_space = 10.0f; // cm
constexpr int xz_grid_x = 20;
constexpr int xz_grid_z = 20;
constexpr int xz_grid_z_adj = 0;

//
// the ultimate tracker obj model coordinate system:
//  X axis: left (left camera, the camera closer to the letter 'E')
//  Y axis: forward (the VIVE face direction)
//  Z axis: down (usb-c jack direction, i.e. opposite button direction)
//
// the ultimate tracker (after v0.0.3) uses isaac sim coordinate system:
//  X axis: forward
//  Y axis: left
//  Z axis: up
// => obj to tracker = +Y, +X, -Z = | 0  1  0 |
//                                  | 1  0  0 |
//                                  | 0  0 -1 |
//
// finally, balai app uses opencv coordinate system:
//  X axis: right
//  Y axis: down
//  Z axis: forward
// => tracker to opencv = -Y, -Z, +X = | 0 -1  0 |
//                                     | 0  0 -1 |
//                                     | 1  0  0 |

// transfrom from tracker obj model coordinates to tracker coordinates.
mlabs::balai::math::Matrix3 const obj_2_tracker(0.0f, 1.0f, 0.0f,
                                                1.0f, 0.0f, 0.0f,
                                                0.0f, 0.0f,-1.0f);

// transform from tracker coordinates to opencv coordinates.
mlabs::balai::math::Matrix3 const tracker_2_opencv(0.0f,-1.0f, 0.0f,
                                                   0.0f, 0.0f,-1.0f,
                                                   1.0f, 0.0f, 0.0f);

// tracker's front vector
mlabs::balai::math::Vector3 const tracker_front(1.0f, 0.0f, 0.0f);


char const* get_state_string(uint8_t s) {
  constexpr char const* state_string[] {
    "NA",
    "Pairing",
    "Paired",
    "Connecting",
    "Connected",
    "Initialized",
    "Syncing",
    "BuildMap",
    "Ready",
  };
  return (s<array_size(state_string)) ? state_string[s]:"Unknown";
};

}

class TrackerViewer : private mlabs::balai::framework::BaseApp,
                      private vive::ImGuiConsole,
                      private vive::ITrackerIPCClient {
  struct Tracker {
    std::string name, label; // S/N

    mlabs::balai::math::Matrix3 xform = mlabs::balai::math::Matrix3::Identity;
    mlabs::balai::math::Matrix3 odo_origin, inv_odo_origin; // odo

    float velocity[3]{}, angular_velocity[3]{};

    int64_t timestamp_us{0};
    uint32_t id{0};
    uint32_t flags{0}; 

    uint8_t state{0};
    uint8_t button{0};
    uint8_t clicks{0};
    uint8_t battery{0};

    bool active{false}, odo{false};

    bool Update(vive::TrackerData const& p) {
      // xform = [ R | t ]
      Quaternion(p.Rotation[3], p.Rotation[0], p.Rotation[1], p.Rotation[2]).BuildRotationMatrix(xform, false);
      xform.SetOrigin(100.0f*p.Location[0], 100.0f*p.Location[1], 100.0f*p.Location[2]);

      velocity[0] = 100.0f*p.Velocity[0];
      velocity[1] = 100.0f*p.Velocity[1];
      velocity[2] = 100.0f*p.Velocity[2];

      angular_velocity[0] = p.AngularVelocity[0];
      angular_velocity[1] = p.AngularVelocity[1];
      angular_velocity[2] = p.AngularVelocity[2];

      timestamp_us = p.Timestamp_us;
      id = p.Id;
      flags = p.Flags;

      state = p.State;
      button = p.Button;
      clicks = p.Clicks;
      battery = p.Battery;

      label = name;
      if (0!=(TrackerFlag_IsHostTracker&flags)) {
        label += "*";
      }

      return true;
    }
  };

  std::vector<Tracker> trackers_;

  // obj model
  vive::TrackerModel model_;

  // virtual 3D camera local transform matrix
  mlabs::balai::math::Matrix3 camera_ltm_;

  int view_fov_;
  uint8_t alt_pressed_{0}, ctrl_pressed_{0}, focus_mode_{0};

  bool show_main_ui_{true};
  bool console_open_{false};
  bool prompt_quit_{false};
  bool show_grids_{true};
  bool show_axes_{true};
  bool server_stop_{false};

  bool OnKeyEvent_(KeyEvent const& e) override {
    //LOGI("key:'%c' 0x%X action:%d\n", (char)e.ASCII(), (int)e.ASCII(), (int)e.Action);
    switch (e.Key)
    {
    case GLFW_KEY_ESCAPE:
      if (1==e.Action) {
        if (console_open_) {
          console_open_ = false;
        } else if (prompt_quit_) {
          // to quit...
          return false;
        } else {
          prompt_quit_ = true;
        }
      }
      return true;

    case GLFW_KEY_F1:
      if (1==e.Action) {
        show_main_ui_ = !show_main_ui_;
      }
      break;

    case GLFW_KEY_F2:
      if (1==e.Action) {
        console_open_ = !console_open_;
      }
      break;

    case GLFW_KEY_F4:
      if (1==e.Action) {
      }
      break;

    case GLFW_KEY_LEFT_SHIFT:
    case GLFW_KEY_RIGHT_SHIFT:
    case GLFW_KEY_LEFT_CONTROL:
    case GLFW_KEY_RIGHT_CONTROL:
    case GLFW_KEY_LEFT_ALT:
    case GLFW_KEY_RIGHT_ALT:
      break;

    default:
      break;
    }

    if (!ImGui::IsAnyItemActive()) {
      switch (e.Key)
      {
      case GLFW_KEY_1:
      case GLFW_KEY_2:
        if (1==e.Action) {
        }
        break;

      case GLFW_KEY_3:
        if (1==e.Action) {
        }
        break;

      case GLFW_KEY_W:
        if (e.Action) {
          camera_ltm_.Move(0.0f, 0.0f, (e.SHIFT+e.ALT+e.CTRL) ? 0.1f:1.0f, true);
        }
        break;

      case GLFW_KEY_A:
        if (e.Action) {
          camera_ltm_.Move((e.SHIFT+e.ALT+e.CTRL) ? -0.1f:-1.0f, 0.0f, 0.0f, true);
        }
        break;

      case GLFW_KEY_S:
        if (e.Action) {
          camera_ltm_.Move(0.0f, 0.0f, (e.SHIFT+e.ALT+e.CTRL) ? -0.1f:-1.0f, true);
        }
        break;

      case GLFW_KEY_D:
        if (e.Action) {
          camera_ltm_.Move((e.SHIFT+e.ALT+e.CTRL) ? 0.1f:1.0f, 0.0f, 0.0f, true);
        }
        break;

      case GLFW_KEY_Q:
        if (e.Action) { // 0:release 1:pressed 2:repeat
          camera_ltm_.Move(0.0f, (e.SHIFT+e.ALT+e.CTRL) ? -0.1f:-1.0f, 0.0f, true);
        }
        break;

      case GLFW_KEY_Z:
        if (e.Action) {
          camera_ltm_.Move(0.0f, (e.SHIFT+e.ALT+e.CTRL) ? 0.1f:1.0f, 0.0f, true);
        }
        break;

      case GLFW_KEY_LEFT_ALT:
      case GLFW_KEY_RIGHT_ALT:
        alt_pressed_ = e.Action;
        break;

      case GLFW_KEY_LEFT_CONTROL:
      case GLFW_KEY_RIGHT_CONTROL:
        ctrl_pressed_ = e.Action;
        break;

      default:
        break;
      }
    }

    return true; // BaseApp::OnKeyEvent_(e);
  }

  void MouseTouchpadScroll_(float /*xoffset*/, float yoffset) override {
    if (alt_pressed_) {
      yoffset *= 5.0f;
    } else if (ctrl_pressed_) {
      yoffset *= 0.1f;
    }
    camera_ltm_.Move(0.0f, 0.0f, yoffset, true);
  }
//void TouchBegan_(int id, int taps, int x, int y) override {}
  void TouchMoved_(int id, int taps, int /*x*/, int /*y*/, int dx, int dy) override {
    //LOGI("[touch move] id:%d taps:%d  x:%d(%+d) y:%d(+%d)\n", id, taps, x, dx, y, dy);
    if (ImGui::IsAnyItemActive()||ImGui::IsItemHovered()) return;

    //
    // let's go with Unreal Engine...
    if (1==taps) {
      if (0==id) { // LMB
        Vector3 const origin(camera_ltm_.Origin());
        float const rotate_per_pixel = (float)view_fov_*constants::float_deg_to_rad/(float)height_;
        if (alt_pressed_ || ctrl_pressed_) {
          if (abs(dx)>abs(dy)) {
            auto const front = camera_ltm_.ZAxis();

            camera_ltm_.SetOrigin(0.0f, 0.0f, 0.0f);
            camera_ltm_ = Matrix3::RotationMatrix(0.0f, 4.0f*rotate_per_pixel*(float)dx, 0.0f) * camera_ltm_; // yaw

            auto o = -origin.Norm()*camera_ltm_.ZAxis();
            if (alt_pressed_ && fabs(front.y)>1.e-3f) {
              float const forward = -origin.y/front.y;
              if (0.0f<forward && forward<10000.0f) {
                o = -forward*camera_ltm_.ZAxis();
                o.x += origin.x + forward*front.x;
                o.z += origin.z + forward*front.z;
              }
            }
            o.y = origin.y;
            camera_ltm_.SetOrigin(o);
          } else if (dy) {
            camera_ltm_.Move(0.0f, 0.0f, -200.0f*(float)dy/(float)height_);
          }
        } else {
          if (dx) {
            camera_ltm_ = Matrix3::RotationMatrix(0.0f, rotate_per_pixel*(float)dx, 0.0f) * camera_ltm_; // global rotate
            camera_ltm_.SetOrigin(origin);
          }
          if (dy) {
            camera_ltm_.Move(0.0f, 0.0f, -200.0f*(float)dy/(float)height_);
            camera_ltm_._24 = origin.y; // fix height
          }
        }
      } else if (1==id) { // RMB
        if (alt_pressed_) {
          camera_ltm_.Move(0.0f, 0.0f, 100.0f*(float)dy/(float)height_);
        } else {
          float const rotate_per_pixel = (float)view_fov_*constants::float_deg_to_rad/(float)height_;
          if (dx) {
            Vector3 const origin = camera_ltm_.Origin();
            camera_ltm_ = Matrix3::RotationMatrix(0.0f, rotate_per_pixel*(float)dx, 0.0f) * camera_ltm_; // global rotate
            camera_ltm_.SetOrigin(origin);
          }

          if (dy) {
            camera_ltm_ *= Matrix3::RotationMatrix(-rotate_per_pixel*(float)dy, 0.0f, 0.0f); // local rotate
          }
        }
      } else if (2==id) { // middle mouse move for panning. in UE, LMB+RMB = panning also.
        camera_ltm_.Move(100.0f*(float)dx/(float)width_, 100.0f*(float)dy/(float)width_, 0.0f);
      }
    }
  }
  //void TouchEnded_(int id, int taps, int x, int y) override {}

  bool Initialize_() override {
    model_.Initialize("./Vive_Ultimate_Tracker.obj", "./Vive_Ultimate_Tracker_BaseColor.png");

    camera_ltm_.SetLookAt(Vector3(-10.0f, -15.0f, -20.0f),
                          Vector3(0.0f, 0.0f, 0.0f),
                          Vector3(0.0f, -1.0f, 0.0f));
    //
    // init imgui
    IMGUI_CHECKVERSION();
    auto* ctx = ImGui::CreateContext();
    if (ctx) {
      auto& io = ImGui::GetIO();
      io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;

      ImGui::StyleColorsDark();

      if (!ImGui_ImplGlfw_InitForOpenGL(BaseApp::window_, true)) {
        LOGE("ImGui_ImplGlfw_InitForOpenGL() failed!");
      }

      if (!ImGui_ImplOpenGL3_Init("#version 330")) { // GLSL 3.3
        LOGE("ImGui_ImplOpenGL3_Init() failed!");
      }

      server_stop_ = false;
      return true;
    }
    return false;
  }

  bool FrameMove_(float elapsed_time) override  {
    ITrackerIPCClient::Poll(); // listen...
    return !server_stop_;
  }

  bool Render_() override {
    if (!RenderUI_()) {
      LOGD("ready to quit...");
      return false;
    }

    auto& renderer = Renderer::GetInstance();

    // world as identity
    renderer.SetWorldMatrix(Matrix3::Identity);

    // view as we are controlling
    renderer.SetViewFromLTM(camera_ltm_);

    // projection matrix
    renderer.SetPerspectiveProjection((float)view_fov_*constants::float_deg_to_rad, 1.0f, 10000.0f);

    renderer.PushState();
    renderer.SetCullDisable();
    renderer.Clear(Color::Gray);
    if (renderer.BeginScene()) {
      if (show_grids_ || show_axes_) {
        renderer.SetBlendMode(GFXBLEND_SRCALPHA, GFXBLEND_INVSRCALPHA);

        auto& prim = Primitives::GetInstance();
        prim.BeginDraw(NULL, GFXPT_LINELIST);

        float const xx = (float)(xz_grid_x/2)*xz_grid_space;
        float const zz = (float)(xz_grid_z/2)*xz_grid_space;

        if (show_grids_) {
          //float const z0 = xz_grid_z_adj*xz_grid_size;
          //float const z1 = (xz_grid_z+xz_grid_z_adj)*xz_grid_size;
          for (int x=0; x<=xz_grid_x; ++x) {
            int xi = x - xz_grid_x/2;
            float const xf = (float)xi*xz_grid_space;
            if (0==(xi%5)) {
              prim.SetColor({0,0,0,128});
            } else {
              prim.SetColor({0,0,0,64});
            }
            prim.AddVertex(xf, 0.0f, -zz);
            prim.AddVertex(xf, 0.0f, (!show_axes_||0!=xi) ? zz:0.0f);
          }

          for (int z=0; z<=xz_grid_z; ++z) {
            int const zi = z - xz_grid_z/2;
            float zf = (float)(zi+xz_grid_z_adj)*xz_grid_space;
            if (0==(z%5)) {
              prim.SetColor({0,0,0,128});
            } else {
              prim.SetColor({0,0,0,64});
            }
            prim.AddVertex(xx, 0.0f, zf);
            prim.AddVertex((!show_axes_||0!=zi) ? -xx:0.0f, 0.0f, zf);
          }
        }

        if (show_axes_) {
          // isaac sim +x axis (forward) is the opencv +z axis
          prim.SetColor({255, 0, 0, 128});
          prim.AddVertex(0.0f, 0.0f, 0.0f);
          prim.AddVertex(0.0f, 0.0f, zz);

          // isaac sim is a right-handed coordinate system, +y axis = z cross x which should point to left.
          // so is opencv -x axis
          prim.SetColor({0,255,0,128});
          prim.AddVertex(0.0f, 0.0f, 0.0f);
          prim.AddVertex( -xx, 0.0, 0.0f);

          // isaac sim +z axis (up) is the opencv -y axis
          prim.SetColor({0,0,255,128});
          prim.AddVertex(0.0f, 0.0f, 0.0f);
          prim.AddVertex(0.0f, -zz, 0.0f);
        }

        prim.EndDraw();

        renderer.SetBlendDisable();
      }

      {
        bool draw_axes = false;
        for (auto const& t : trackers_) {
          if (t.active) {
            if (TrackerState::Ready==t.state && 0==(TrackerFlag_RotationOnly&t.flags)) {
              model_.Render(tracker_2_opencv*t.xform*obj_2_tracker);
            } else {
              model_.Render(tracker_2_opencv*t.xform*obj_2_tracker, {0,0,0,64}, true);
            }

            if (t.odo) {
              draw_axes = true;
            }
          }
        }

        if (draw_axes) {
          Vector3 o0, o1, p0, p1;
          renderer.SetBlendMode(GFXBLEND_SRCALPHA, GFXBLEND_INVSRCALPHA);
          auto& prim = Primitives::GetInstance();
          prim.BeginDraw(NULL, GFXPT_LINELIST);
          for (auto const& t:trackers_) {
            if (t.active && t.odo) {
              auto const& cs0 = t.odo_origin;
              auto const& cs1 = t.xform;
              prim.SetColor({255,255,0,64});
              tracker_2_opencv.PointTransform(o0, cs0.Origin());
              tracker_2_opencv.PointTransform(o1, cs1.Origin());
              prim.AddVertex(o0);
              prim.AddVertex(o1);

              prim.SetColor(Color::Red);
              tracker_2_opencv.PointTransform(p0, cs0.Origin()+10.0f*cs0.XAxis());
              tracker_2_opencv.PointTransform(p1, cs1.Origin()+10.0f*cs1.XAxis());
              prim.AddVertex(o0);
              prim.AddVertex(p0);
              prim.AddVertex(o1);
              prim.AddVertex(p1);

              prim.SetColor(Color::Green);
              tracker_2_opencv.PointTransform(p0, cs0.Origin()+10.0f*cs0.YAxis());
              tracker_2_opencv.PointTransform(p1, cs1.Origin()+10.0f*cs1.YAxis());
              prim.AddVertex(o0);
              prim.AddVertex(p0);
              prim.AddVertex(o1);
              prim.AddVertex(p1);

              prim.SetColor(Color::Blue);
              tracker_2_opencv.PointTransform(p0, cs0.Origin()+10.0f*cs0.ZAxis());
              tracker_2_opencv.PointTransform(p1, cs1.Origin()+10.0f*cs1.ZAxis());
              prim.AddVertex(o0);
              prim.AddVertex(p0);
              prim.AddVertex(o1);
              prim.AddVertex(p1);
            }
          }

          prim.EndDraw();
          renderer.SetBlendDisable();
        }
      }

      renderer.EndScene();
    }

    renderer.PopState();

    // draw UI
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    return true;
  }

  void Cleanup_() override {
    model_.Destroy();
    trackers_.clear();

    // finish ImGui
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
  }

  // ImGuiConsole interface
  void DispatchCommand_(vive::Command& cmd) override {
    if (0==memcmp(cmd.cmd, "clear", 6)) {
      vive::ImGuiConsole::Clear();
    } else if (0==memcmp(cmd.cmd, "help", 5) || 0==memcmp(cmd.cmd, "list", 5) || 0==memcmp(cmd.cmd, "h", 2)) {
      cmd.Result("list all commands...", 1);
      vive::ImGuiConsole::AddLog(" Pairing <id-list>");
      vive::ImGuiConsole::AddLog(" Unpair <id-list>");
      vive::ImGuiConsole::AddLog(" Restart <id-list>");
      vive::ImGuiConsole::AddLog(" PowerOff <id-list>");
      vive::ImGuiConsole::AddLog(" Echo [id-list], default: all");
      vive::ImGuiConsole::AddLog(" Setup [id], default: any");
      //vive::ImGuiConsole::AddLog(" force_halt__ to close tracker server");
    } else {
      if (ITrackerIPCClient::SendCommand(cmd.cmd, cmd.len)) {
        cmd.Result("sent", 1);
      } else {
        cmd.Result("failed", -1);
      }
    }
  }

  bool AutoCompletion_(char* cmd_buf, int& cursor_pos, int& cmd_len, int /*cmd_capacity*/) override {
    cursor_pos = cmd_len = sprintf(cmd_buf, "%s", "help");
    return true;
  }

  // imgui
  bool RenderUI_() {
    //
    // Updata ImGui and Render
    // start the dear ImGui frame...
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    {
      ImGui::SetNextWindowPos({8, 6});
      ImGui::SetNextWindowBgAlpha(0.5f);
      int const connected_trackers = (int) trackers_.size();
      float const ui_height = inf(88.0f+(float)connected_trackers*142.0f, (float)(BaseApp::height_-6));
      ImGui::SetNextWindowSize({360.0f, ui_height});

      bool dummy_show_ui = true;
      ImGui::Begin("UI", &dummy_show_ui, ImGuiWindowFlags_NoDecoration); //|ImGuiWindowFlags_NoBackground);
      ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 0, 255));

      auto const& io = ImGui::GetIO();
      ImGui::Text("FPS:%.1f", io.Framerate);
      ImGui::SameLine();
      ImGui::SetCursorPosX(80);
      ImGui::Checkbox("Grids", &show_grids_);
      ImGui::SameLine();
      //ImGui::SetCursorPosX(120);
      ImGui::Checkbox("Axes", &show_axes_);
      ImGui::SameLine();
      ImGui::Checkbox("Console", &console_open_);

      ImGui::Separator();
      ImGui::SliderInt(" ", &view_fov_, 5, 150, "FOV:%d");
      ImGui::SameLine();
      if (ImGui::Button("Reset")) {
        view_fov_ = default_fov;
        camera_ltm_.SetOrigin(-focus_distance*camera_ltm_.ZAxis());
        focus_mode_ = 0;
      }

      if (connected_trackers>0) {
        ImGui::Separator();
        Matrix3 rel;
        Vector3 rot;

        int64_t const cur_timestamp = vive::GetTimestamp();
        char time_status[16];
        auto get_time_status_string = [&time_status](int64_t ms) {
          if (ms>1000) {
            memcpy(time_status, "Zzz...(>1s)", 12);
          } else if (ms>=0) {
            memcpy(time_status, "-----------", 12);
            ms /= 10;
            time_status[ms<10 ? ms:10] = '+';
          } else {
            memcpy(time_status, "???", 4);
          }
          return time_status;
        };

        bool setup_required = false;
        for (auto const& t:trackers_) {
          if (t.active) {
            //ImGui::SetNextItemOpen(true);
            //ImGui::Separator();

            ImGui::PushID(&t);
            if (ImGui::CollapsingHeader(t.label.c_str())) {
#if 0
              constexpr char const* pose_states[] = {
                "Uninitialized",
                "Loading",
                "Good",
                "Calibrating",
                "RotationOnly",
                "Initiating",
                "Syncing",
                "No_Map"
              };
              constexpr char const* pose_state = pose_states[TrackerFlag_PoseStateMask&t.flags];
#endif
              ImGui::Text("  Status: %s  battery: %d%%  %s %c%c", get_state_string(t.state), (int)t.battery,
                          get_time_status_string((cur_timestamp-t.timestamp_us)/1000),
                          0!=(TrackerFlag_PoseStable&t.flags) ? 'S':' ',
                          0!=(TrackerFlag_RotationOnly&t.flags) ? 'R':' ');

              if (0!=(TrackerFlag_RotationOnly&t.flags)) {
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(128, 128, 128, 255));
                ImGui::Text("Location: %.1f, %.1f, %.1f cm", t.xform._14, t.xform._24, t.xform._34);
                ImGui::PopStyleColor();
              } else {
                ImGui::Text("Location: %.1f, %.1f, %.1f cm", t.xform._14, t.xform._24, t.xform._34);
              }

              rot = t.xform.EulerAngles()*constants::float_rad_to_deg;
              ImGui::Text("Rotation: %.1f\u00B0 %.1f\u00B0 %.1f\u00B0", rot.x, rot.y, rot.z);
              ImGui::Text("Velocity: % .1f, % .1f, % .1f cm/s", t.velocity[0], t.velocity[1], t.velocity[2]);
              ImGui::Text(" AngVecl: % .1f, % .1f, % .1f rad/s", t.angular_velocity[0], t.angular_velocity[1], t.angular_velocity[2]);

              if (0!=(TrackerFlag_SetupRequired&t.flags)) {
                setup_required = true;
              }

              if (t.odo) {
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 255, 255, 255));
                //ImGui::Separator();
                //constexpr float ident = 24.0f;
                //ImGui::SetCursorPosX(ident); ImGui::SliderFloat("Scale X", &t.scale[0], 0.5f, 2.0f, "%.2f");
                //ImGui::SetCursorPosX(ident); ImGui::SliderFloat("Scale Y", &t.scale[1], 0.5f, 2.0f, "%.2f");
                //ImGui::SetCursorPosX(ident); ImGui::SliderFloat("Scale Z", &t.scale[2], 0.5f, 2.0f, "%.2f");

                rel = t.inv_odo_origin*t.xform; // check
                rot = rel.EulerAngles()*constants::float_rad_to_deg;
                auto move = rel.Origin();
                ImGui::Text("    Move: %+.1f, %+.1f, %+.1f | %.1f cm", move.x, move.y, move.z, move.Norm());
                ImGui::Text("  Rotate: %+.1f\u00B0 %+.1f\u00B0 %+.1f\u00B0", rot.x, rot.y, rot.z);

                ImGui::PopStyleColor();
              }
            }
            ImGui::PopID();
          }
        }

        if (setup_required) {
          ImGui::Separator();
          ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255));
          ImGui::Text("    (Setup Required!)");
          ImGui::PopStyleColor();
        }
      }

      ImGui::PopStyleColor();
      ImGui::End();
    }

    if (console_open_) {
      vive::ImGuiConsole::Draw("Console", console_open_);
    }

    bool confirm_quit = false;
    if (prompt_quit_) {
      ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
      ImGui::SetNextWindowSize({180, 64});
      ImGui::SetNextWindowBgAlpha(0.75f); // Transparent background
      ImGui::Begin("Quit", &prompt_quit_, ImGuiWindowFlags_NoDecoration |
                                          ImGuiWindowFlags_AlwaysAutoResize |
                                          ImGuiWindowFlags_NoSavedSettings |
                                          ImGuiWindowFlags_NoFocusOnAppearing |
                                          ImGuiWindowFlags_NoNav);

      float windowWidth = ImGui::GetWindowSize().x;
      float textWidth = ImGui::CalcTextSize("Quit?").x;
      ImGui::SetCursorPosX((windowWidth - textWidth) * 0.5f);
      ImGui::Text("Quit?");
      ImGui::Separator();

      textWidth = ImGui::CalcTextSize(" Yes12345No ").x;
      ImGui::SetCursorPosX((windowWidth - textWidth) * 0.5f);
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
      if (ImGui::Button(" Yes ")) {
        confirm_quit = true;
        prompt_quit_ = false;
      }
      ImGui::SameLine();
      if (ImGui::Button(" No ")) {
        prompt_quit_ = false;
      }
      ImGui::End();
    }

    ImGui::Render();

    return !confirm_quit;
  }

public:
  TrackerViewer():BaseApp(),
    trackers_(GetNumTrackers()),model_(),camera_ltm_(0.0f,0.0f,-50.0f),view_fov_(default_fov) {
    BaseApp::window_title_ = "VIVE Tracker Viewer";
    BaseApp::width_  = 1280;
    BaseApp::height_ = 720;
    //BaseApp::gl_major_version_ = 4;
    //BaseApp::gl_minor_version_ = 5;
  }

  void ParseCommandLine(int argc, char* argv[]) override {
    for (int i=0; i<argc; ++i) {
      auto const* arg = argv[i];
      if ('-'==*arg) {
        if ('-'==*++arg) {
          ++arg;
        }

        if (('f'==arg[0] && '\0'==arg[1]) || 0==memcmp(arg, "fullscreen", 11)) {
          BaseApp::fullscreen_ = true;
        } else if (0==memcmp(arg, "sdk", 4)) { // if call from ultimate tracker sdk
          BaseApp::window_title_ = "VIVE Tracker Viewer (sdk)";
        }
      }
    }
  }

  //
  // vive::ITrackerIPCClient implementation...
  // server start/stop
  void ServerStart() override {
    LOGI("tracker server start...");
  }
  void ServerStop() override {
    LOGI("tracker server stopped.");
    server_stop_ = true;
  }
  void CommandResponse(vive::TrackerCommandResponse const& r) override {
    char str[256];
    char* s = str + sprintf(str, "%s", get_state_string(r.TrackerStates[0]));
    for (uint32_t i=1; i<r.NumTrackers; ++i) {
      s += sprintf(s, ", %s", get_state_string(r.TrackerStates[i]));
    }
    LOGI("[TrackerViewer::CommandResponse] %s >> %s states[%d]: { %s }", r.Command, r.Result, r.NumTrackers, str );
  }
  void Add(uint32_t tracker_id, char const* name) override {
    if (tracker_id<trackers_.size()) {
      auto& tracker = trackers_[tracker_id];
      if (tracker.active) {
        LOGW("[TrackerViewer::Add] %s id:%d already added!?", tracker.name.c_str(), tracker_id);
      }

      tracker = {};
      tracker.id = tracker_id;
      tracker.timestamp_us = vive::GetTimestamp();
      tracker.name = (name && name[0]) ? name:"(no name)";
      tracker.label = tracker.name;
      tracker.active = true;
      tracker.odo = false;
      LOGV("[TrackerViewer::Add] new tracker: %s", tracker.name.c_str());
    } else {
      LOGD("[TrackerViewer::Add] invalid id:%d(total:%d) %s",
           tracker_id, (int)trackers_.size(), (name && name[0]) ? name:"(no name)");
    }
  }
  void Update(vive::TrackerData const& p) override {
    if (p.Id<trackers_.size()) {
      auto& tracker = trackers_[p.Id];
      assert(tracker.id==p.Id && tracker.active);
      tracker.Update(p);
    } else {
      LOGW("[TrackerViewer::Update] invalid id:%d(total:%d)", p.Id, (int)trackers_.size());
    }
  }
  void Click(uint32_t tracker_id, int /*button_id*/, int clicks) override {
    if (clicks>0) {
      if (tracker_id<trackers_.size()) {
        auto& t = trackers_[tracker_id];
        assert(t.id==tracker_id);

        Vector3 o = t.xform.Origin();
        bool finishODO = false;
        if (2==clicks) {
          // a double-click event...
        } else if (1==clicks) {
          t.odo = !t.odo;
          if (t.odo) {
            t.odo_origin = t.xform;
            t.inv_odo_origin = t.odo_origin.GetInverse();

            Vector3 from, to;
            tracker_2_opencv.PointTransform(to, o);
            tracker_2_opencv.PointTransform(from, o - focus_distance*t.xform.VectorTransform(tracker_front));
            camera_ltm_.SetLookAt(from, to, Vector3(0.0f, -1.0f, 0.0f));
          } else {
            finishODO = true;
          }
        } else {
          // releasing button
        }

        if (finishODO) {
          auto rel = t.inv_odo_origin*t.xform;
          auto rot = rel.EulerAngles()*constants::float_rad_to_deg;
          auto move = rel.Origin();
          LOGV("[%s] Rotate:%+.1f\u00B0 %+.1f\u00B0 %+.1f\u00B0 Move: %+.1f %+.1f %+.1f (%.1f cm)",
               t.name.c_str(), rot.x, rot.y, rot.z, move.x, move.y, move.z, move.Norm());
        }
      }
    }
  }
  void Remove(uint32_t tracker_id) override {
    if (tracker_id<trackers_.size()) {
      auto& tracker = trackers_[tracker_id];
      assert(tracker.id==tracker_id);
      LOGI("[TrackerViewer::Remove] %s", tracker.name.c_str());

      tracker.name.clear();
      tracker.label.clear();
      tracker.active = false;
      tracker.odo = false;
    }
  }

  //
  // macro needed for IMPLEMENT_APPLICATION_CLASS
  DECLARE_APPLICATION_CLASS;
};

//
// the macro to define the balai application class
IMPLEMENT_APPLICATION_CLASS(TrackerViewer);
