// taken from class ExampleAppConsole of imgui_demo.cpp 

#ifndef IMGUI_CONSOLE_H
#define IMGUI_CONSOLE_H

#include "imgui.h"
#include "imgui_internal.h" // ImGuiWindow

#include "vive_utils.h"

namespace vive {

struct Command {
  static constexpr int cmd_buffer_size = 120;
  static constexpr int cmd_max_length = 80; // null character not included
  static constexpr int8_t resule_OK = 1;
  char cmd[cmd_buffer_size];
  uint32_t hash;
  uint8_t len; // cmdlen
  uint8_t source;
  uint8_t async;
  int8_t result;

  void Reset() {
    cmd[0] = '\0';
    hash = 0;
    len = source = async = 0;
    result = 0;
  }

  void Result(char const* msg, int8_t _result) {
    if (msg) {
      int pos = len + 1;
      while (pos<cmd_buffer_size) {
        if ('\0'!=(cmd[pos]=*msg++)) {
          ++pos;
        } else {
          break;
        }
      }

      if (pos>=cmd_buffer_size) {
        cmd[cmd_buffer_size-1] = '\0';
        cmd[cmd_buffer_size-2] = '.';
        cmd[cmd_buffer_size-3] = '.';
        cmd[cmd_buffer_size-4] = '.';
      }
    }
    result = _result;
  }
};

class ImGuiConsole {
private:
  enum { cmd_list_size = 1024 };
  Command cmd_list_[cmd_list_size]; // 128 KB!? put this in heap?

  char const* win_title_ {};
  int cmd_sent_{0}, history_id_{-1};
  bool scroll_to_bottom_{false};

  // execute command and set the result, i.e. cmd.Result("OK", 2);
  virtual void DispatchCommand_(Command& cmd) = 0;

  // auto complete the command like when tab is hit
  virtual bool AutoCompletion_(char* cmd_buf, int& cursor_pos, int& cmd_len, int cmd_capacity) = 0;

public:
  ImGuiConsole() {
    memset(cmd_list_, 0, sizeof(cmd_list_));
    win_title_ = nullptr;
    cmd_sent_ = 0;
    history_id_ = -1;
    scroll_to_bottom_ = false;
  }

  virtual ~ImGuiConsole() = default;

  void Clear() {
    cmd_list_[cmd_sent_=0].Reset();
    history_id_ = -1;
  }

  // e.g. AddLog("a log: %s", important_msg).Result("OK", 2);
  Command& AddLog(const char* fmt, ...) /*IM_FMTARGS(2)*/ {
    auto& cmd = cmd_list_[(cmd_sent_++)%cmd_list_size];

    va_list args;
    va_start(args, fmt);
    int const len = vsnprintf(cmd.cmd, Command::cmd_buffer_size-1, fmt, args);
    va_end(args);

    cmd.cmd[len] = 0;
    cmd.hash = 0xffffffff;
    cmd.len = (uint8_t) len;
    cmd.source = 255; // it's a log
    cmd.async = 0;
    cmd.result = 0;

    scroll_to_bottom_ = true;
    return cmd;
  }

  void Draw(const char* title, bool& is_open) {
    // set initial position and size
    if (win_title_!=title) {
      ImGuiViewport const* viewport = ImGui::GetMainViewport();
      if (viewport) {
        constexpr float border_x = 8.0f;
        constexpr float border_y = 8.0f;

        ImVec2 const& work_size = viewport->WorkSize;
        float init_height = inf(240.0f, work_size.y-border_y*2.0f);
        ImGui::SetNextWindowPos({border_x, work_size.y-init_height-border_y}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({work_size.x-border_x*2.0f, init_height}, ImGuiCond_FirstUseEver);

//        LOGV("[ImGuiConsole::Draw] %dx%d Set windows rect {%.1f, %.1f, %.1f, %.1f}",
//             (int)work_size.x, (int)work_size.y,
//             border_x, work_size.y-init_height-border_y, work_size.x-border_x*2.0f, init_height);
      }
      win_title_ = title;
    }

    ImGui::SetNextWindowBgAlpha(0.5f);
    if (!ImGui::Begin(title, &is_open)) {
      LOGE("[ImGuiConsole::Draw] ImGui::Begin(%s) failed!", title);
      ImGui::End();
      return;
    }

    // Reserve enough left-over height for 1 separator + 1 input text
    float const footer_height_to_reserve = ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
    if (ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footer_height_to_reserve), false, ImGuiWindowFlags_HorizontalScrollbar)) {
      // Display every line as a separate entry so we can change their color or add custom widgets.
      // If you only want raw text you can use ImGui::TextUnformatted(log.begin(), log.end());
      // NB- if you have thousands of entries this approach may be too inefficient and may require user-side clipping
      // to only process visible items. The clipper will automatically measure the height of your first item and then
      // "seek" to display only items in the visible area.
      // To use the clipper we can replace your standard loop:
      //      int item_start = sup(0, cmd_sent_-cmd_list_size+1);
      //      for (int i=item_start; i<cmd_sent_; ++i)
      //   With:
      //      ImGuiListClipper clipper;
      //      clipper.Begin(cmd_sent_);
      //      while (clipper.Step())
      //         for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
      // - That your items are evenly spaced (same height)
      // - That you have cheap random access to your elements (you can access them given their index,
      //   without processing all the ones before)
      // You cannot this code as-is if a filter is active because it breaks the 'cheap random-access' property.
      // We would need random-access on the post-filtered list.
      // A typical application wanting coarse clipping and filtering may want to pre-compute an array of indices
      // or offsets of items that passed the filtering test, recomputing this array when user changes the filter,
      // and appending newly elements as they are inserted. This is left as a task to the user until we can manage
      // to improve this example code!
      // If your items are of variable height:
      // - Split them into same height items would be simpler and facilitate random-seeking into your list.
      // - Consider using manual call to IsRectVisible() and skipping extraneous decoration from your items.
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1)); // Tighten spacing

      // return ImU32 color in format: 0xAABBGGRR
      auto color_ImU32 = [] (int error) {
        if (error>0) {
          return (error>1) ? 0xff00ff00:0xffffff00;
        } else if (-1==error) {
          return 0xff00ffff;
        } else if (-2==error) {
          return 0xff0000ff;
        }
        return 0xffffffff;
      };

      //char text_buffer[256];
      ImU32 curr_color = color_ImU32(0), color;
      ImGui::PushStyleColor(ImGuiCol_Text, curr_color);
      for (int pos=sup(0, cmd_sent_-cmd_list_size+1); pos<cmd_sent_; ++pos) {
        auto const& cmd = cmd_list_[pos%cmd_list_size];

        // set color
        color = color_ImU32(cmd.result);
        if (color!=curr_color) {
          ImGui::SetStyleColor(ImGuiCol_Text, curr_color=color);
        }

        //sprintf(text_buffer, );
        //ImGui::TextUnformatted(text_buffer);
        if (cmd.source<255) {
          ImGui::Text("%s > %s", cmd.cmd, cmd.cmd+cmd.len+1);
        } else {
          ImGui::Text("%s", cmd.cmd);
        }
      }

      ImGui::PopStyleColor();

      // Keep up at the bottom of the scroll region if we were already at the bottom at the beginning of the frame.
      // Using a scrollbar or mouse-wheel will take away from the bottom edge.
      if (scroll_to_bottom_) {
        //if (ImGui::GetScrollY()<ImGui::GetScrollMaxY()) {
          ImGui::SetScrollHereY(1.0f);
        //}
        scroll_to_bottom_ = false;
      }

      ImGui::PopStyleVar();
    }
    ImGui::EndChild();
    ImGui::Separator();

    // input text
    auto* window = ImGui::GetCurrentWindow(); // ImGui::FindWindowByName(title);
    bool reclaim_focus = (window && !window->WasActive && window->Active);
    ImGuiInputTextFlags input_text_flags = ImGuiInputTextFlags_EnterReturnsTrue |
                                           ImGuiInputTextFlags_CallbackCompletion |
                                           ImGuiInputTextFlags_CallbackHistory |
                                           ImGuiInputTextFlags_NoHorizontalScroll |
                                           ImGuiInputTextFlags_CallbackEdit |
                                           ImGuiInputTextFlags_EscapeClearsAll;
  //if (reclaim_focus) {
  //  AddDebugLog("reclaim focus: %s=%p current=%p currentRead=%p", title, window, ImGui::GetCurrentWindow(), ImGui::GetCurrentWindowRead());
  //  scroll_to_bottom_ = true;
  //}

    auto& cmd = cmd_list_[cmd_sent_%cmd_list_size];
    int text_cnt = 0;
    if (ImGui::InputText(" ", cmd.cmd, Command::cmd_max_length+1, input_text_flags, &TextEditCallbackStub, this)) {
      //assert(!is_junk(cmd.cmd[0]));

      // setup command
      cmd.cmd[Command::cmd_max_length] = '\0';
      cmd.hash = hash_cmd(cmd.cmd, &text_cnt);

      // remove junk characters
      if (cmd.cmd[text_cnt]) {
        char const* end = cmd.cmd + Command::cmd_max_length;
        char* put = cmd.cmd + text_cnt;
        char const* get = put + 1;
        for (*put=' '; put<end&&*get; ++get) {
          if (vive::is_junk(*get)) {
            if (' '!=*put) {
              *++put = ' ';
            }
          } else {
            *++put = *get;
          }
        }

        for (*++put='\0'; put>cmd.cmd&&' '==put[-1];) {
          *--put = '\0';
        }
        cmd.len = (uint8_t) (put-cmd.cmd);
      } else {
        cmd.len = (uint8_t) text_cnt;
      }

      cmd.cmd[cmd.len+1] = '.'; // response
      cmd.cmd[cmd.len+2] = '.'; // response
      cmd.cmd[cmd.len+3] = '.'; // response
      cmd.cmd[cmd.len+4] = '\0'; // response
      cmd.source = 0;
      cmd.async = 0;
      cmd.result = 0;

      // execute
      cmd_list_[(++cmd_sent_)%cmd_list_size].Reset(); // clear next command
      DispatchCommand_(cmd);

      history_id_ = -1; // refresh
      text_cnt = 0;
      scroll_to_bottom_ = reclaim_focus = true;
    } else {
      //assert(!vive::is_junk(cmd.cmd[0]));
      text_cnt = 0;
      while (text_cnt<=Command::cmd_max_length) {
        if ('\0'!=cmd.cmd[text_cnt]) {
          ++text_cnt;
        } else {
          break;
        }
      }
    }

    // ato-focus on window apparition
    ImGui::SetItemDefaultFocus();
    if (reclaim_focus) {
      ImGui::SetKeyboardFocusHere(-1); // auto focus previous widget
    }

    // text count
    ImGui::SameLine(); ImGui::Text("%2d/%d", text_cnt, Command::cmd_max_length);

    ImGui::End();
  }

  // In C++11 you'd be better off using lambdas for this sort of forwarding callbacks
  static int TextEditCallbackStub(ImGuiInputTextCallbackData* data) {
    ImGuiConsole* console = (ImGuiConsole*)data->UserData;
    return console->TextEditCallback(data);
  }

  int TextEditCallback(ImGuiInputTextCallbackData* data) {
    switch (data->EventFlag)
    {
    case ImGuiInputTextFlags_CallbackCompletion:
      data->BufDirty = AutoCompletion_(data->Buf, data->CursorPos, data->BufTextLen, data->BufSize);
      break;

    case ImGuiInputTextFlags_CallbackHistory:
      if (cmd_sent_>0) {
        int const prev_history_pos = history_id_;
        if (data->EventKey==ImGuiKey_UpArrow) {
          int const total_checks = inf(cmd_list_size-1, cmd_sent_);
          int prev_id = (0<history_id_) ? (history_id_-1):(cmd_sent_-1);
          history_id_ = -1;
          for (int i=0; i<total_checks&&prev_id>=0; ++i,--prev_id) {
            if (255!=cmd_list_[prev_id%cmd_list_size].source) {
              history_id_ = prev_id;
              break;
            }
          }
        } else if (data->EventKey==ImGuiKey_DownArrow) {
          if (-1!=history_id_) {
            for (int next_id=history_id_+1; next_id<cmd_sent_; ++next_id) {
              if (255!=cmd_list_[next_id%cmd_list_size].source) {
                history_id_ = next_id;
                break;
              }
            }
          }
        }

        if (prev_history_pos!=history_id_) {
          data->DeleteChars(0, data->BufTextLen);
          data->InsertChars(0, (history_id_>=0) ? cmd_list_[history_id_%cmd_list_size].cmd:"");
        }

      } break;

    case ImGuiInputTextFlags_CallbackEdit: {
      // format check...
      if (data->BufTextLen>0) {
        int discard = 0;
        for (int i=0; i<data->BufTextLen; ++i) {
          if (is_junk(data->Buf[i])) {
            ++discard;
          } else {
            break;
          }
        }

        if (discard>0) {
          if ((data->CursorPos-=discard)<0) {
            data->CursorPos = 0;
          }
          if ((data->BufTextLen-=discard)>0) {
            memmove(data->Buf, data->Buf+discard, data->BufTextLen);
          } else {
            data->BufTextLen = 0;
          }
          data->Buf[data->BufTextLen] = '\0';
          data->BufDirty = true;
          //LOGH(">> \"%s\" length:%d cursor:%d", data->Buf, data->BufTextLen, data->CursorPos);
        }
      }
    } break;

    default:
      break;
    }

    return 0;
  }

  static uint32_t hash_cmd(char const* cmd, int* len=nullptr) {
    if (cmd) {
      uint32_t hash = 0x811c9dc5;
      char const* s = cmd;
      for (; '\0'!=*s&&' '!=*s; ++s) {
        if ('a'<=*s && *s<='z') { // to uppercase
          hash = (hash^uint32_t(*s-32))*0x1000193;
        } else {
          hash = (hash^uint32_t(*s))*0x1000193;
        }
      }
      if (len) {
        *len = (int)(s-cmd);
      }
      return hash;
    }

    if (len) {
      *len = 0;
    }
    return 0;
  }

  static bool init_cmd(Command& cmd, char const* cmdline) {
    if (cmdline) {
      cmd.hash = hash_cmd(cmdline);
      for (cmd.len=0; cmd.len<=Command::cmd_max_length;) {
        if ('\0'!=(cmd.cmd[cmd.len]=*cmdline++)) {
          ++cmd.len;
        } else {
          break;
        }
      }

      if (cmd.len>Command::cmd_max_length) {
        return false;
      }
    } else {
      cmd.hash = 0;
      cmd.len = 0;
    }

    cmd.source = 0;
    cmd.async = 0;
    cmd.result = 0;
    return true;
  }
};

}

#endif
