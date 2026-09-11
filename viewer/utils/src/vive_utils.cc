#include "../include/vive_utils.h"

#include <sys/stat.h>

namespace vive {

/*
  console color code, not working for some console windows, e.g. visual studios
  e.g.
    cout << "\033[1;31mbold red text\033[0m\n";

  Here, \033 (\x27, ASCII 0x27) is the ESC character. It is followed by [, then zero or more numbers separated by ;,
  and finally the letter m. The numbers describe the colour and format to switch to from that point onwards.

  The codes for foreground and background colours are:

             foreground background
    black        30         40
    red          31         41
    green        32         42
    yellow       33         43
    blue         34         44
    magenta      35         45
    cyan         36         46
    white        37         47

  Additionally, you can use these:

    reset             0  (everything back to normal)
    bold/bright       1  (often a brighter shade of the same colour)
    underline         4
    inverse           7  (swap foreground and background colours)
    bold/bright off  21
    underline off    24
    inverse off      27

  P.S.
    To determine whether your terminal supports colour sequences, read the value of the TERM environment variable.
    It should specify the particular terminal type used (e.g. vt100, gnome-terminal, xterm, screen, ...)
    check the colors capability.
*/

class terminal {
  enum { 
    EXT_LOG_TYPES = 10,
    ALL_LOG_TYPES = LOG_HIGHLIGHT+1+EXT_LOG_TYPES
  };
  char string_buf_[ALL_LOG_TYPES*16];
  struct {
    char const* type;
    char const* code;
  } codes_[ALL_LOG_TYPES];
  std::mutex mutex_;
  FILE* log_file_;
  int tm_mday_;

  bool color_enable_;

public:
  terminal():mutex_(),log_file_(nullptr),tm_mday_(-1),color_enable_(false) {
    memset(string_buf_, 0, sizeof(string_buf_));
    memset(codes_, 0, sizeof(codes_));

    char* tm = getenv("TERM");
    if (tm) {
      // "xterm-256color"   Intel NUC 7 with Ubuntu
      // "xterm"            Git Bash on windows, PuTTy on Windows
      // "dumb"             Visual Studio Linux Console Window, color not support!
      if (0==memcmp("xterm", tm, 5)) {
        color_enable_ = true;
      } // "vt100" ?
        // "gnome-terminal" ?
        // "screen" ?
        // ...
        //
    }

    char* p = string_buf_;

    // base types
    codes_[LOG_DISABLE].type = p; *p++='?'; *p++='\0';
    codes_[LOG_ERROR].type = p;   *p++='E'; *p++='\0';
    codes_[LOG_WARNING].type = p; *p++='W'; *p++='\0';
    codes_[LOG_INFO].type = p;    *p++='I'; *p++='\0';
    codes_[LOG_DEBUG].type = p;   *p++='D'; *p++='\0';
    codes_[LOG_VERBOSE].type = p; *p++='V'; *p++='\0';
    codes_[LOG_HIGHLIGHT].type = p; *p++='H'; *p++='\0';

    // extended types
    char c = '0';
    for (int i=1; i<=EXT_LOG_TYPES; ++i) {
      codes_[LOG_HIGHLIGHT+i].type = p; *p++=c++; *p++='\0';
    }

    if (color_enable_) {
      // base types
      codes_[LOG_DISABLE].code = p; strcpy(p, "\033[1;31m"); /*red*/    p+=(strlen(p)+1);
      codes_[LOG_ERROR].code = p;   strcpy(p, "\033[1;31m"); /*red*/    p+=(strlen(p)+1);
      codes_[LOG_WARNING].code = p; strcpy(p, "\033[1;33m"); /*yellow*/ p+=(strlen(p)+1);
      codes_[LOG_INFO].code = p;    strcpy(p, "\033[1;37m"); /*white*/  p+=(strlen(p)+1);
      codes_[LOG_DEBUG].code = p;   strcpy(p, "\033[1;36m"); /*cyan*/   p+=(strlen(p)+1);
      codes_[LOG_VERBOSE].code = p; strcpy(p, "\033[1;32m"); /*green*/  p+=(strlen(p)+1);
      codes_[LOG_HIGHLIGHT].code = p; strcpy(p, "\033[1;35m"); /*magenta*/  p+=(strlen(p)+1);

      // extended types
      int const ci[] = { 31, 32, 33, 34, 35, 36, 37 };
      for (int i=1; i<=EXT_LOG_TYPES; ++i,++c) {
        codes_[LOG_HIGHLIGHT+i].code = p;
        p += sprintf(p, "\033[0;%2dm", ci[i%array_size(ci)]) + 1;
      }

      assert((size_t)(p-string_buf_)<sizeof(string_buf_));
    }
  }

#if 0
  ~terminal() {
    auto tt = std::time(nullptr);
    tm* now = std::localtime(&tt);
    if (now) {
      std::lock_guard<std::mutex> __lock(mutex_);
      if (color_enable_) {
        printf("\033[1;37m%4d/%02d/%02d %02d:%02d:%02d program ended\033[0m\n",
               now->tm_year+1900, now->tm_mon+1, now->tm_mday, now->tm_hour, now->tm_min, now->tm_sec);
      } else {
        printf("%4d/%02d/%02d %02d:%02d:%02d program ended\n",
               now->tm_year+1900, now->tm_mon+1, now->tm_mday, now->tm_hour, now->tm_min, now->tm_sec);
      }
    }
    if (log_file_) {
      fclose(log_file_);
      log_file_ = nullptr;
    }
  }
#else
  ~terminal() {
    if (log_file_) {
      fclose(log_file_);
      log_file_ = nullptr;
    }
  }
#endif

  void SetFileLog(char const* filename, bool clear) {
    std::lock_guard<std::mutex> __lock(mutex_);
    if (log_file_) {
      fclose(log_file_);
      log_file_ = nullptr;
    }
    if (filename) {
      log_file_ = fopen(filename, clear ? "w":"a");
    }
  }

  void operator()(int type, char const* log) {
    auto const& ctx = codes_[(0<=type&&type<ALL_LOG_TYPES) ? type:0];
    auto tt = std::time(nullptr);
    tm* now = std::localtime(&tt);

    std::lock_guard<std::mutex> __lock(mutex_);
    if (now) {
      if (tm_mday_!=now->tm_mday) {
        if (ctx.code) {
          printf("\033[1;37m%4d/%02d/%02d, Another beautiful day :-)\033[0m\n", now->tm_year+1900, now->tm_mon+1, now->tm_mday);
        } else {
          printf("%4d/%02d/%02d, Another beautiful day :-)\n", now->tm_year+1900, now->tm_mon+1, now->tm_mday);
        }

        if (log_file_) {
          fprintf(log_file_, "\n--- %4d/%02d/%02d ---\n", now->tm_year+1900, now->tm_mon+1, now->tm_mday);
        }
        tm_mday_ = now->tm_mday;
      }
      printf("%02d:%02d:%02d [%s] : ", now->tm_hour, now->tm_min, now->tm_sec, ctx.type);
      if (log_file_) {
        fprintf(log_file_, "%02d:%02d:%02d [%s] : ", now->tm_hour, now->tm_min, now->tm_sec, ctx.type);
      }
    } else {
      printf("--:--:-- [%s] : ", ctx.type);
    }

    if (ctx.code) {
      printf("%s%s\033[0m\n", ctx.code, log);
    } else {
      printf("%s\n", log);
    }
    if (log_file_) {
      fprintf(log_file_, "%s\n", log);
      fflush(log_file_);
    }
  }
} term;

// default log function
static std::function<void(int, char const*)> log_ = [](int type, char const* log) {
  term(type, log);
};

int sys_log_level = LOG_LEVEL;

void SetFileLog(char const* filename, bool clear) {
  term.SetFileLog(filename, clear);
}

std::function<void(int type, char const* msg)>
SetUserLog(std::function<void(int type, char const* msg)> const& log) {
  auto backup = log_; // copy
  log_ = log;
  return backup;
}

// LOG
void LOG(int type, char const* format, ...) {
  if (type>LOG_VERBOSE || type<=sys_log_level) {
    char buffer[2048]; // 2K buffer

    // parse the given string
    std::va_list va;
    va_start(va, format);
    int const len = vsnprintf(buffer, 2047, format, va);
    va_end(va);

    buffer[len] = '\0';

    log_(type, buffer);
  }
}

int FILE_LOG(FILE* file, char const* format, ...) {
#ifndef DISABLE_FILE_LOG
  if (file) {
    auto tt = std::time(nullptr);
    tm* now = std::localtime(&tt);
    int len = fprintf(file, "%4d/%02d/%02d %02d:%02d:%02d ", 
                      now->tm_year+1900, now->tm_mon+1, now->tm_mday, 
                      now->tm_hour, now->tm_min, now->tm_sec);
    va_list args;
    va_start(args, format);
    len += vfprintf(file, format, args);
    va_end(args);

    fflush(file);
    return len;
  }
#endif
  return 0;
}

int FILE_LOG(char const* filename, char const* format, ...) {
#ifndef DISABLE_FILE_LOG
  FILE* file = (filename && filename[0]) ? fopen(filename, "a"):nullptr;
  if (!file) {
    file = fopen("/usr/local/vivetracker/logs/vivetracker.log", "a");
  }

  if (file) {
    auto tt = std::time(nullptr);
    tm* now = std::localtime(&tt);
    //flockfile(file);
    int len = fprintf(file, "%4d/%02d/%02d %02d:%02d:%02d ", 
                      now->tm_year+1900, now->tm_mon+1, now->tm_mday, 
                      now->tm_hour, now->tm_min, now->tm_sec);
    va_list args;
    va_start(args, format);
    len += vfprintf(file, format, args);
    va_end(args);

    //funlockfile(file);
    fclose(file);
    return len;
  }

#endif
  return 0;
}

int FILE_WRITE(char const* filename, bool append, char const* format, ...) {
  FILE* file = (filename && filename[0]) ? fopen(filename, append ? "a":"w"):nullptr;
  if (file) {
    va_list args;
    va_start(args, format);
    int const len = vfprintf(file, format, args);
    va_end(args);

    //funlockfile(file);
    fclose(file);
    return len;
  }

  return 0;
}

bool get_current_timestamp(char ts[16]) {
  auto tt = std::time(nullptr);
  tm* now = std::localtime(&tt);
  if (now) {
    return (int) sprintf(ts, "%4d%02d%02dT%02d%02d%02d",
                         now->tm_year+1900, now->tm_mon+1, now->tm_mday,
                         now->tm_hour, now->tm_min, now->tm_sec);
  }
  return 0;
}

char* simple_json_parser(std::function<bool(char* key, char* value, int index, bool is_end)> const& callback, char* json, char* end) {
  if (nullptr!=json && json<end && (json=str_moveon(json, end))<end && '{'==*json) {
    char* key, *value;
    int count = 0;
    for (json=str_moveon(++json, end); (json<end)&&('\"'==*json); json=str_moveon(json, end)) {
      key = ++json;
      json = str_chr(json, end, '\"');
      if (json<end && '\"'==*json) {
        *json++ = '\0'; // null-terminate key
        json = str_moveon(json, end);
        if (json<end && ':'==*json) {
          value = json = str_moveon(json+1, end);
          if (json<end) {
            if ('\"'==*json) {
              json = str_chr(++value, end, '\"');
              if (json<end && '\"'==*json) {
                *json++ = '\0'; // null-terminate value
                json = str_moveon(json, end);
                if (json<end) {
                  bool const hit_end = ('}'==*json);
                  if (','==*json || hit_end) {
                    ++json;
                    if (!callback(key, value, count++, hit_end) || hit_end) {
                      return json;
                    }
                    continue;
                  }
                }
              }
            } else if ('{'==*json || '['==*json) {
              char const right = ('{'==*json) ? '}':']';
              char const left = *json++;
              int bracklet = 1;
              for (++json; json<end; ++json) {
                if (right==*json) {
                  if (0==--bracklet) {
                    break;
                  }
                } else if (left==*json) {
                  ++bracklet;
                }
              }

              if (++json<end) {
                assert(right==json[-1]);
                char* ve = json;
                json = str_moveon(json, end);
                bool const hit_end = ('}'==*json);
                if (','==*json || hit_end) {
                  ++json;
                  *ve = '\0';
                  if (!callback(key, value, count++, hit_end) || hit_end) {
                    return json;
                  }
                  continue;
                }
              }
            } else {
              //
              // value not quoted, could be null, true, false, and decimal numbers.
              bool illformat = false;
              if (isdigit(*json) || '-'==*json) {
                if ('0'!=*json || ((json+1)<end && ('.'==json[1] || ','==json[1] || is_junk(json[1])))) {
                  int dots = ('.'==*json) ? 1:0;
                  int exps = 0;
                  for (++json; json<end; ++json) {
                    if (!isdigit(*json)) {
                      if ('.'==*json && dots<1 && 0==exps) {
                        dots = 1;
                        continue;
                      } else if (('e'==*json || 'E'==*json) && ++exps<2) {
                        if ((json+1)<end && ('+'==json[1] || '-'==json[1])) {
                          ++json;
                        }
                        continue;
                      }
                      break;
                    }
                  }

                  if (dots>1 || exps>1) {
                    illformat = true;
                  }
                } else {
                  illformat = true;
                }
              } else if (0==memcmp(json, "null", 4)) {
                json += 4;
              } else if (0==memcmp(json, "true", 4)) {
                json += 4;
              } else if (0==memcmp(json, "false", 5)) {
                json += 5;
              } else {
                illformat = true;
              }

              if (!illformat && json<end) {
                char* ve = json;
                json = str_moveon(json, end);
                bool const hit_end = '}'==*json;
                if (','==*json || hit_end) {
                  ++json;
                  if (','==*ve || '}'==*ve || is_junk(*ve)) {
                    *ve = '\0';
                    if (!callback(key, value, count++, hit_end) || hit_end) {
                      return json;
                    }
                    continue;
                  }
                }
              }
            }
          }
        }
      }

      break;
    }
  }
  return nullptr;
}

}
