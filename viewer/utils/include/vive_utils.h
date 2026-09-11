// VIVE Robotics, HTC. All Rights Reserved.
#ifndef VIVE_UTILITIES_H
#define VIVE_UTILITIES_H

#include <math.h>
#include <chrono>
#include <fstream>
#include <string> // getline
#include <thread>
#include <functional>
#include <atomic>
#include <mutex>
#include <cstdarg>
#include <assert.h>
#include <memory.h>
#include <stdint.h>
#include <limits.h>
#include <stdlib.h> // malloc/free
#include <string>

#if __cplusplus >= 201703L
#include <string_view> // C++17
#endif

/*

// TO-DO this only works in visual studios
#define STRINGIZE_HELPER(x) #x
#define STRINGIZE(x) STRINGIZE_HELPER(x)
#define __MESSAGE(text) __pragma(message(__FILE__ "(" STRINGIZE(__LINE__) ")" text))
#define COMPILE_WARNING(text) __MESSAGE( " : Warning: " #text )
#define COMPILE_ERROR(text) __MESSAGE( " : Error: " #text )
#define COMPILE_MESSAGE(text) __MESSAGE( ": " #text )
#define TODO(text) COMPILE_WARNING( TODO: text )

*/

//
// to check C++ standard
// In C++0x the macro __cplusplus will be set to a value that differs from (is greater than) the current 199711L.
// Although this isn't as helpful as one would like. gcc (apparently for nearly 10 years) had this value set to 1,
// ruling out one major compiler, until it was fixed when gcc 4.7.0 came out.
// These are the C++ standards and what value you should be able to expect in __cplusplus:
//   C++ pre-C++98: __cplusplus is 1.
//   C++98: __cplusplus is 199711L.
//   C++98 + TR1: This reads as C++98 and there is no way to check that I know of.
//   C++11: __cplusplus is 201103L.
//   C++14: __cplusplus is 201402L.
//   C++17: __cplusplus is 201703L.
//
#if __cplusplus>=201103L
template<typename T, std::size_t N>
constexpr std::size_t array_size(T (&)[N]) noexcept {
  return N;
}
#else
#define array_size(ra) (sizeof(ra)/sizeof(ra[0]))
#endif

template<typename T>
T clamp(T a, T inf, T sup) {
  return (a<inf) ? inf:((a<sup) ? a:sup);
}

//
// warping x in range [a, b). T=float or double
// i.e. a <= range_mod(x, a, b) < b with precondition: a<b
// example: to map x into [0,360) range,
//  x = range_mod(x, 0.0f, 360.0f);
template<typename T>
T range_mod(T x, T a, T b) {
  if (a<b) {
    if (x<a) {
      x = b - fmod(a-x, b-a);
      return (x<b) ? x:a;
    } else {
      return a + fmod(x-a, b-a);
    }
  }
  return a; // NO!!!
}

template<typename T>
T inf(T a, T b) { return (a<b) ? a:b; }

template<typename T>
T sup(T a, T b) { return (a>b) ? a:b; }

// C++11 Variadic Template Arguments
template<typename T, typename... Args>
T inf(T first, Args&&... args) {
  T&& rest = inf(args...);
  return (first<rest) ? first:rest;
}

template<typename T, typename... Args>
T sup(T first, Args&&... args) {
  T&& rest = sup(args...);
  return (first>rest) ? first:rest;
}

//
// TODO:
//   1) macro and type
//   2) platform and architecture
//   3) debug message and log
//
#define VIVE_MAKE_4CC(a, b, c, d) ((((uint32_t)a)<<24) | (((uint32_t)b)<<16) | (((uint32_t)c)<<8) | ((uint32_t)d))

inline uint32_t VIVE_MAKE_UINT32(uint8_t const* c) {
  return (((uint32_t)c[0])<<24) | (((uint32_t)c[1])<<16) | (((uint32_t)c[2])<<8) | (c[3]);
}
inline uint16_t VIVE_MAKE_UINT16(uint8_t const* c) {
  return (uint16_t) ((((uint16_t)c[0])<<8) | ((uint16_t)c[1]));
}
inline uint8_t* VIVE_WRITE_UINT16(uint8_t* s, uint16_t v) {
  *s++ = (uint8_t) ((v&0xff00)>>8);
  *s++ = (uint8_t)  (v&0x00ff);
  return s;
}
inline uint8_t* VIVE_WRITE_UINT32(uint8_t* s, uint32_t v) {
  *s++ = (uint8_t) ((v&0xff000000)>>24);
  *s++ = (uint8_t) ((v&0x00ff0000)>>16);
  *s++ = (uint8_t) ((v&0x0000ff00)>>8);
  *s++ = (uint8_t)  (v&0x000000ff);
  return s;
}

namespace vive {

inline float rad_to_deg(float d) { return d*57.295778f; }
inline float deg_to_rad(float d) { return d*0.017453f; }
inline double rad_to_deg(double d) { return d*57.295779513082321; }
inline double deg_to_rad(double d) { return d*0.017453292519943; }

inline uint32_t hash_data_uint32(uint8_t const* data, int len) {
  if (data && len>0) {
    constexpr uint32_t prime = 0x1000193;
    uint32_t hash = 0x811c9dc5;
    for (int i=0; i<len; ++i) {
      hash = (hash^(*data++))*prime;
    }
    return hash;
  }
  return 0;
}

inline uint32_t hash_str_uint32(char const* str, int* len2=nullptr) {
  uint32_t hash = 0;
  int len = 0;
  if (str && *str) {
    constexpr uint32_t prime = 0x1000193;
    hash = 0x811c9dc5;
    for (uint8_t const* data = (uint8_t*) str; *data; ++data,++len) {
      hash = (hash^(*data))*prime;
    }
  }
  if (len2) { *len2 = len; }
  return hash;
}

inline uint32_t hash_str_uint32(char const* str, char lim, int* len2=nullptr) {
  uint32_t hash = 0;
  int len = 0;
  if (str && *str) {
    uint8_t const stop = (uint8_t) lim;
    constexpr uint32_t prime = 0x1000193;
    hash = 0x811c9dc5;
    for (uint8_t const* data = (uint8_t*) str; *data&&*data!=stop; ++data,++len) {
      hash = (hash^(*data))*prime;
    }
  }
  if (len2) { *len2 = len; }
  return hash;
}

// trim from left
inline std::string& ltrim(std::string& s, const char* t=" \t\n\r\f\v") {
  s.erase(0, s.find_first_not_of(t));
  return s;
}

// trim from right
inline std::string& rtrim(std::string& s, const char* t=" \t\n\r\f\v") {
  s.erase(s.find_last_not_of(t) + 1);
  return s;
}

#if __cplusplus >= 201703L
template<typename Fun>
void tokenize(std::string_view sv, Fun const& proc, const char* delimiter=" \t\n\r\f\v") {
  size_t s{0};
  for (size_t e{0}; std::string::npos!=(e=sv.find_first_of(delimiter, s)); s=e+1) {
    proc(std::string{sv.substr(s, e-s)});
  }
  proc(std::string{sv.substr(s)});
}
#else
template<typename Fun>
void tokenize(std::string const& line, Fun const& proc, const char* delimiter=" \t\n\r\f\v") {
  size_t s{0};
  for (size_t e{0}; std::string::npos!=(e=line.find_first_of(delimiter, s)); s=e+1) {
    if (e>s) {
      proc(line.substr(s, e-s));
    }
  }
  if (s<line.length()) {
    proc(s>0 ? line.substr(s):line);
  }
}

/*
  //
  // "hello world! {is this a real life?}"
  //   1) hello
  //   2) world!
  //   3) {is this a real life?}
  //
  size_t s{0};
  for (size_t e{0}; std::string::npos!=(e=line.find_first_of(" {", s)); s=e+1) {
    if (e>s) {
      proc(line.substr(s, e-s));
    }

    if ('{'==line[e]) {
      e = line.find("}", (s=e)+1);
      if (std::string::npos!=e) {
        proc(line.substr(s, (++e)-s));
      } else {
        break;
      }
    }
  }

  if (s<line.length()) {
    proc(s>0 ? line.substr(s):line);
  }
*/
#endif

//
// simple-escape-sequence(ISO/IEC 14882 2.13.2):
//  '\'':  // single quote
//  '\"':  // double quote(same as '"')
//  '\?':  // question mark(same as '?')
//  '\\':  // backslash
//  '\a':  // alert(bell)
//  '\b':  // backspace
//  '\f':  // form feed
//  '\n':  // new-line(line feed)
//  '\r':  // carriage return
//  '\t':  // horizontal tab
//  '\v':  // vertical tab
inline bool is_junk(char ch) { return ' '==ch || '\r'==ch || '\n'==ch; }
inline char* str_moveon(char* p, char* end) {
  while (p<end && is_junk(*p)) {
    ++p;
  }
  return p;
}
inline char* str_chr(char* p, char* end, char ch) {
  while (p<end && ch!=*p) {
    ++p;
  }
  return p;
}

//
// read value from text
struct NumericSerializer {
  union {
    float f32;
    int   i32;
  };
  enum {
    TYPE_NA,
    TYPE_FLOAT,
    TYPE_INT,
  } type{TYPE_NA}; // 0: unknown, 1:float 2:int... error if < 0

  NumericSerializer& operator=(float vf) {
    type = TYPE_FLOAT;
    f32 = vf;
    return *this;
  }
  NumericSerializer& operator=(int vi) {
    type = TYPE_INT;
    i32 = vi;
    return *this;
  }
  operator bool() const { return TYPE_NA!=type; }
  bool IsInt() const { return TYPE_INT==type; }
  bool IsFloat() const { return TYPE_FLOAT==type; }
  operator float() const {
    return (TYPE_FLOAT==type) ? f32:(TYPE_INT==type ? (float)i32:0.0f);
  }
  operator int() const {
    return (TYPE_INT==type) ? i32:(TYPE_FLOAT==type ? (int)f32:0);
  }
  bool read(char const*& begin, char const* end) {
    while (begin<end && is_junk(*begin)) {
      ++begin;
    }

    if (isdigit(*begin) || '.'==*begin) {
      int dots = ('.'==*begin) ? 1:0;
      char const* init = begin++;
      while (*begin && begin<end) {
        if ('0'<=*begin && *begin<='9') {
          ++begin;
        } else if ('.'==*begin) {
          if (++dots>1) {
            break;
          }
          ++begin;
        } else {
          break;
        }
      }

      if (dots>0) {
        f32 = (float) atof(init);
        type = TYPE_FLOAT;
      } else {
        i32 = atoi(init);
        type = TYPE_INT;
      }

      if (','==*begin || ' '==*begin) {
        ++begin;
      }

      return true;
    }

    type = TYPE_NA;
    return false;
  }
};

//
// a simple log system
#define LOG_DISABLE 0
#define LOG_ERROR 1
#define LOG_WARNING 2
#define LOG_INFO 3
#define LOG_DEBUG 4
#define LOG_VERBOSE 5
#define LOG_HIGHLIGHT 6

//
// to dynamic change system log level, you should restore to LOG_LEVEL after changed.
extern int sys_log_level;

// log options - set in make file, for visual studio, Preprocessor Definitions: LOG_LEVEL=0, 1, 2, 3, 4 or 5(default)
#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_VERBOSE
#endif

int FILE_LOG(FILE* file, char const* format, ...);
int FILE_LOG(char const* filename, char const* format, ...);
int FILE_WRITE(char const* filename, bool append, char const* format, ...);

#if LOG_LEVEL>=LOG_ERROR
void SetFileLog(char const* filename, bool clear=false);
std::function<void(int type, char const* msg)> SetUserLog(std::function<void(int type, char const* msg)> const& log);
void LOG(int, char const* format, ...);
#define LOGE(...)  LOG(LOG_ERROR, __VA_ARGS__)
#define LOGH(...)  LOG(LOG_HIGHLIGHT, __VA_ARGS__)
#define LOGEXT(ext, ...)  LOG(LOG_VERBOSE+1+ext, __VA_ARGS__)
 #if LOG_LEVEL>=LOG_WARNING
  #define LOGW(...)  LOG(LOG_WARNING, __VA_ARGS__)
   #if LOG_LEVEL>=LOG_INFO
    #define LOGI(...)  LOG(LOG_INFO, __VA_ARGS__)
    #if LOG_LEVEL>=LOG_DEBUG
     #define LOGD(...)  LOG(LOG_DEBUG, __VA_ARGS__)
      #if LOG_LEVEL>=LOG_VERBOSE
       #define LOGV(...)  LOG(LOG_VERBOSE, __VA_ARGS__)
      #else
       #define LOGV(...)
      #endif
     #else
      #define LOGV(...)
      #define LOGD(...)
     #endif
   #else
     #define LOGV(...)
     #define LOGD(...)
     #define LOGI(...)
   #endif
 #else
  #define LOGV(...)
  #define LOGD(...)
  #define LOGI(...)
  #define LOGW(...)
 #endif

#else
#define LOGV(...)
#define LOGD(...)
#define LOGI(...)
#define LOGW(...)
#define LOGE(...)
#define LOGEXT(...)
#define LOG(...)
#define SetFileLog(filename, clear)
#define SetUserLog(log)
#endif

//
// simple json parser -- I'll call you back... json will be modify, save a copy if you don't want me to.
// return the pointer point to the end of of json
char* simple_json_parser(std::function<bool(char* key, char* value, int index, bool is_end)> const& callback, char* json_start, char* json_end);
inline char* simple_json_parser(std::function<bool(char* key, char* value, int index, bool is_end)> const& callback, char* json, int len) {
  return simple_json_parser(callback, json, json+len);
}

#if 0
inline char* simple_json_remove_escape(char* json) {
  char* src, *dst;
  for (src=dst=json; *src!='\0'; ++src) {
    *dst = *src;
    if (*dst!='\\') {
      ++dst;
    }
  }
  *dst = '\0';
  return json;
}
#endif

// prefer alias declaration to typedefs
using time_point = std::chrono::time_point<std::chrono::system_clock>;
inline time_point get_current_time() {
//inline auto get_current_time() -> decltype(std::chrono::system_clock::now()) {
  return std::chrono::system_clock::now();
}

inline int64_t get_timestamp_microseconds() {
  return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
inline int64_t get_timestamp_microseconds(time_point const& t) {
  return std::chrono::duration_cast<std::chrono::microseconds>(t.time_since_epoch()).count();
}

inline int64_t get_delta_time_seconds(time_point const& a, time_point const& b) {
  return std::chrono::duration_cast<std::chrono::seconds>(b-a).count();
}
inline int64_t get_delta_time_milliseconds(time_point const& a, time_point const& b) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(b-a).count();
}
inline int64_t get_delta_time_microseconds(time_point const& a, time_point const& b) {
  return std::chrono::duration_cast<std::chrono::microseconds>(b-a).count();
}

inline int64_t get_elapsed_time_seconds(decltype(get_current_time()) const& start_time) {
  return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now()-start_time).count();
}
inline int64_t get_elapsed_time_milliseconds(decltype(get_current_time()) const& start_time) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()-start_time).count();
}
inline int64_t get_elapsed_time_microseconds(decltype(get_current_time()) const& start_time) {
  return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now()-start_time).count();
}
inline void sleep_for_seconds(int seconds) {
  std::this_thread::sleep_for(std::chrono::seconds(seconds));
}
inline void sleep_for_milliseconds(int milliseconds) {
  std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}
inline void sleep_for_microseconds(int64_t microsecond) {
  std::this_thread::sleep_for(std::chrono::microseconds(microsecond));
}

bool get_current_timestamp(char ts[16]);

//
// handy memory buffer
class MemoryBuffer final {
  enum {
    DEFAULT_BUFFER_SIZE = 64 - sizeof(uint8_t*) - 4*sizeof(uint32_t)
  };
  uint8_t  default_buffer_[DEFAULT_BUFFER_SIZE];
  uint8_t* buffer_;
  int      capacity_;
  int      size_;

public:
  explicit MemoryBuffer(int reserve_size=0):
    buffer_(NULL),capacity_(DEFAULT_BUFFER_SIZE),size_(0) {
    buffer_ = default_buffer_;
    Malloc(reserve_size);
  }
  MemoryBuffer(MemoryBuffer const&) = delete;
  MemoryBuffer& operator=(MemoryBuffer const&) = delete;
  ~MemoryBuffer() {
    if (buffer_!=default_buffer_) {
      free(buffer_);
      buffer_ = NULL;
    } else {
      assert(capacity_==DEFAULT_BUFFER_SIZE);
    }
    capacity_ = 0;
  }

  uint8_t* Malloc(int size) {
    if (capacity_<size) {
      int const new_capacity = (size+1023) & ~1023;
      uint8_t* new_buffer = (uint8_t*) malloc(new_capacity);
      if (new_buffer) {
        if (size_>0) {
          memcpy(new_buffer, buffer_, size_);
        }

        if (buffer_!=default_buffer_) {
          free(buffer_);
        }
        buffer_ = new_buffer;
        capacity_ = new_capacity;
      } else {
        return NULL;
      }
    }
    return buffer_;
  }

  void Free() {
    if (buffer_!=default_buffer_) {
      free(buffer_);
      buffer_=default_buffer_;
    }
    capacity_ = DEFAULT_BUFFER_SIZE;
  }

  operator uint8_t*() { return buffer_; } // dangerous
  operator uint8_t const*() const { return buffer_; }

  uint8_t const* ptr() const { return buffer_; }
  uint8_t* ptr() { return buffer_; }
  int capaticy() const { return capacity_; }
  int size() const { return size_; }
  bool set_size(int s) {
    if (s<=capacity_) {
      size_ = s;
      return true;
    }
    return false;
  }
};

//
// read a text file line by line. F could be
//   [](std::string const& line, int line_id) or
//   std::function<bool(std::string const& msg, int line_id)>
template<typename F>
int read_text_file(char const* filename, F&& fun) {
  if (filename) {
    std::ifstream file(filename);
    if (!file.fail()) {
      int read_lines = 0;
      std::string line;
      while (std::getline(file, line)) {
      /*
        // to get sub-string separated by ','
        int words = 0;
        std::string word;
        for (std::stringstream ss(line); std::getline(ss, word, ','); ) {
          ++words;
        }
      */
        if (!fun(line, read_lines++)) {
          break;
        }
      }
      return read_lines;
    }
  }
  return -1;
}

inline void* read_from_file(size_t& size, char const* filename) {
  char* data = nullptr;
  size = 0;
  if (filename) {
    FILE* file = fopen(filename, "rb");
    if (file) {
      fseek(file, 0, SEEK_END);
      size = ftell(file);
      data = (char*) malloc(size+1);
      if (data) {
        rewind(file);
        if (size==fread(data, 1, size, file)) {
          data[size] = '\0';
        } else {
          free(data);
          data = nullptr;
          size = 0;
        }
      }
      fclose(file);
    }
  }
  return data;
}

}
#endif
