
#ifndef BL_ALLOW_PLATFORM_APPLICATION_INCLUDE
#error "Do NOT include 'BLGLFWApp.h' directly, #include 'BLApplication.h' instead"
#endif

#ifndef BL_GLFW_APPLIATION_H
#define BL_GLFW_APPLIATION_H

//
// !!! NOTE ON GLFW3 library !!!
// Unfortunately, on pre-built prototype machine 10.1.122.60, we have 2 versions of GLFW installed
//
// #1 the old 3.2.1 version: before GLFW_LOCK_KEY_MODS was supported
//    /usr/include/GLFW/glfw3.h and /usr/lib/x86_64-linux-gnu/libglfw.so
//   
//   Linker | Input | Library Dependencies = balai;GL;GLEW;glfw;X11;dl;pthread
// 
// #2 the new version 3.3.0 was installed with GLFW_LOCK_KEY_MODS supported
//   /usr/local/include/GLFW/glfw3.h and /usr/local/lib/libglfw3.a
//
//   Linker | Input | Library Dependencies = balai;GL;GLEW;glfw3;X11;dl;pthread
// 
// hopefully, i'd like to use GLFW3.3
// 
// -andre 2023/03/16
//

#include "GL/glew.h" // a must for having glew.h before gl.h
#include "GLFW/glfw3.h"

namespace mlabs { namespace balai { namespace framework {

typedef class GLFWApp : protected Application {
  enum { MAX_TOTAL_TOUCHES = 4 };
  struct {
    double button_time;
    int action;
    int taps;
  } cursors_[MAX_TOTAL_TOUCHES];
  double cursor_x_, cursor_y_;

protected:
  GLFWwindow* window_;

  // window positions
  //uint16 left_;
  //uint16 top_;

  // don't call me, override me...

  // return false (shift+ESC) to stop...
  struct KeyEvent {
    int Scancode;
    uint16_t Key;     // GLFW_KEY_XXXXX, all definitions please refer glfw3.h
    uint8_t Action:2; // 0:release 1:pressed 2:repeat

    // modifiers
    uint8_t SHIFT:1;
    uint8_t CTRL:1;
    uint8_t ALT:1;
    uint8_t SUPER:1; // GLFW_MOD_SUPER?
    uint8_t CAPS_LOCK:1;
    uint8_t Num_Lock:1;

    // valid range: 0~127
    uint8_t ASCII() const;
  };
  virtual bool OnKeyEvent_(KeyEvent const& e) {
    char const* keyName = glfwGetKeyName(e.Key, e.Scancode);
    auto const ascii = e.ASCII();
    BL_LOG("[Key Event] %s(%d) ascii:\'%c\'(%d) scancode:%d %s", keyName ? keyName:"???", (int)e.Key,
           (char)ascii, (int)ascii, e.Scancode, (e.Action>1) ? "REPEAT":(e.Action ? "PRESS":"RELEASE"));
    if (e.SHIFT) {
      BL_LOG(" shift");
    }
    if (e.CTRL) {
      BL_LOG(" ctrl");
    }
    if (e.ALT) {
      BL_LOG(" alt");
    }
    if (e.SUPER) {
      BL_LOG(" super");
    }
    if (e.CAPS_LOCK) {
      BL_LOG(" caps_lock");
    }
    if (e.Num_Lock) {
      BL_LOG(" num_lock");
    }
    BL_LOG("\n");

    return GLFW_KEY_ESCAPE!=e.Key || !e.Action;
  }

private:
  // touches callbacks:
  //    [id] 0:left mouse button 1:right mouse button 2:middle mouse button
  //  [taps] tap count
  //
  virtual void TouchBegan_(int /*id*/, int /*taps*/, int /*x*/, int /*y*/) {}
  virtual void TouchMoved_(int /*id*/, int /*taps*/, int /*x*/, int /*y*/, int /*dx*/, int /*dy*/) {}
  virtual void TouchEnded_(int /*id*/, int /*taps*/, int /*x*/, int /*y*/) {}
  virtual void TouchCancelled_(int id, int taps, int x, int y) {
    TouchEnded_(id, taps, x, y);
  }

  // mouse wheel or scrolling area of a touchpad.
  virtual void MouseTouchpadScroll_(float /*xoffset*/, float /*yoffset*/) {}

  virtual void OnSurfaceResized_(int /*width*/, int /*height*/) {}

  virtual bool Initialize_() = 0;
  virtual bool FrameMove_(float updateTime) = 0;
  virtual bool Render_() = 0;
  virtual void Cleanup_() = 0;

  // glfw callbacks...
  static void glfw_key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
  static void glfw_mousebutton_callback(GLFWwindow* /*window*/, int btn, int action, int mods);
  static void glfw_mousecursor_callback(GLFWwindow* /*window*/, double xpos, double ypos);
  static void glfw_mousescroll_callback(GLFWwindow* /*window*/, double xpos, double ypos);
  static void glfw_window_focus_callback(GLFWwindow* window, int focused);

protected:
  char const* window_title_;

  //
  // icon file is a binary file start with 4 bytes width and 4 bytes height(little endian)
  // follow by RGBA pixel bytes. so size would be 4 + 4 + width*height*4 bytes.
  // leave it null, if running platforms have no such things.
  char const* icon_;

  // implement class may modify width and height
  int width_;
  int height_;

  uint8_t colorBits_;
  uint8_t depthBits_;
  uint8_t stencilBits_;
  uint8_t vsync_on_;
  uint8_t fullscreen_;
  uint8_t gl_major_version_;
  uint8_t gl_minor_version_;
  uint8_t active_;

  // grant access to derived classes only
  GLFWApp(GLFWApp const&) = delete;
  GLFWApp& operator=(GLFWApp const&) = delete;

  GLFWApp();
  ~GLFWApp(); // non-public, non-virtual

public:
  // create then run
  bool Create(void* hInstance) override;
  void Destroy() override;
  int Run() override;

} BaseApp;

}}}

#endif // BL_GLFW_APPLIATION_H
