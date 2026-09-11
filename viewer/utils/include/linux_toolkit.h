#if defined(__linux__) || defined(__FreeBSD__)
#ifndef VIVE_LINUX_TOOLKIT_H
#define VIVE_LINUX_TOOLKIT_H

#include "vive_utils.h"

#include <unistd.h>
#include <pwd.h>
#include <fcntl.h>
#include <linux/kd.h>
#include <sys/wait.h> // waitpid
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/types.h> // gettid
#include <sys/syscall.h>

#include <dirent.h>

#include <sstream>

namespace vive {

//
// create directory:
//  return 1 if folder is newly created, 0 if already exists, -1 if create fails,
//  or -2 if it exist as a file not a directory
int create_directory(char const* dir, mode_t mode=S_IRWXU|S_IRWXG|S_IROTH);

//
// check if file exists
bool file_exists(char const* file);

#if 0
//
// to grab error message, try appending "2>&1" to cmd, e.g. "sh -c dmesg | grep ttyUSB 2>&1"
template<typename fun>
int shell_execute(const char* cmd, fun&& f) {
  auto pipe = popen(cmd, "r");
  if (pipe) {
    char buffer[256];
    int lines = 0;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
      if (f(buffer)) {
        ++lines;
      } else {
        break;
      }
    }
    pclose(pipe);
    return lines;
  }
  return -1;
}
#endif

//
// shell command execution and get response from linux kernel
bool shell_execute(char const* cmdline, char const* working_directory=nullptr);

//
// 2 more shell_executes are by implement of class ShellExecute...
// Prefer using class ShellExecute when possible.
//
bool shell_execute(char const* cmdline,
                   std::function<void(char const* msg, int len, int error)> const& callback,
                   char const* working_directory=nullptr);

bool shell_execute(char const* cmdline,
                   std::function<void(char const* msg, int len, int error)> const& std_out,
                   std::function<bool(char const*& cmd, int& len)> const& std_in, // return false will terminate the child process
                   char const* working_directory=nullptr);

class ShellExecute {
  char read_buf_[1024];
  //std::thread thread_;
  std::atomic<int> pid_;

  // child process just forked. (but the desired process image may not been replaced yet)
  virtual void on_start_(int /*pid*/) {}

  // to handle output and error streams
  virtual void stdout_and_stderr_(char const* msg, int len, int err) {
    LOG(err ? LOG_ERROR:LOG_INFO, "[ShellExecute] %s(%d)", msg, len);
  }

  // to handle input stream, return false will kill the process
  virtual bool stdin_(char const*& cmd, int& length) {
    cmd = nullptr; length = 0;
    return true;
  }

  // if the child process is stuck in above 2 functions, this is the chance to go over it.
  virtual void on_interrupt_(int /*pid*/) {}

  // exit code only take 8-bit int, meaning return -1 will be 255. 128 will be returned if interrupt occurs.
  virtual void on_exit_(uint8_t /*ret*/) {}

public:
  ShellExecute(ShellExecute const&) = delete;
  ShellExecute& operator=(ShellExecute const&) = delete;

  ShellExecute():/*thread_(),*/pid_(0) {}
  virtual ~ShellExecute() { Kill(-1); }

  // is running
  int IsRunning() const { return (int) pid_; }

  // kick off
  bool Run(char const* command, bool redirect_input_stream=false, char const* working_directory=nullptr);
/*
  bool RunAsync(char const* command, bool redirect_input_stream=false, char const* working_directory=nullptr) {
    if (0==pid_) {
      pid_ = -1;
      thread_ = std::move(std::thread([this,command,redirect_input_stream,working_directory]() {
        if (!Run(command,redirect_input_stream,working_directory)) {
          pid_ = 0;
        }
      }));

      while (-1==pid_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      return true;
    }
    return false;
  }

  // revise Kill
  bool Kill(int timeout=-1);
*/

  // kill and wait for timeout (in milliseconds), return true if child process is not running
  // call multiple time if it fails.
  bool Kill(int timeout=-1);
};

//
// a helper to get process with desired name, via "ps aux | grep name"
int for_all_process(char const* name, std::function<bool(char const* msg, int len)> const& callback);

//
// get app path
char* get_self_exe_path(char* path, int max_path_len);

//
// get process name by id
char* get_process_name_by_pid(char* name, int max_name_len, int pid);

//
// wait a key stroke
inline int wait_a_key() {
  timeval tv = { 0, 100000 };
  fd_set fds;
  FD_ZERO(&fds);

  // loop end only when key hit
  for (FD_SET(0, &fds); 0==select(1, &fds, NULL, NULL, &tv); ) {
    FD_SET(0, &fds);
  }

  //
  // char read[4096];
  // if (!fgets(read, 4096, stdin))
  //   break;
  //

  unsigned char c = 0xff;
  if (read(0, &c, 1)>=0) {
    return c;
  }

  return -1;
}

inline bool is_root() {
  // real user ID of the calling process
  return 0==getuid();
}

inline char const* get_user_name() {
  // The return value may point to a static area, and may be overwritten by subsequent calls to
  // getpwent(3), getpwnam(), or getpwuid(). 
  // Do not pass the returned pointer to free()!
  struct passwd* pw = getpwuid(geteuid());
  return pw ? pw->pw_name:"";
}

//
// numeric thread id in C++
//  1) std::ostringstream foo;
//     foo << thread_.get_id();
//     size_t const tid1 = stoll(foo.str());
// or
//  2) std::hash<decltype(thread_.get_id())> hash;
//     size_t const tid2 = hash(thread_.get_id());
//
// note:
//   *) tid1 != tid2
//  **) main_thread_id = std::this_thread::get_id();
//

inline bool is_readable(int fd, int timeout_ms) {
  if (-1!=fd) {
    timeval timemout = { timeout_ms/1000, 1000*(timeout_ms%1000) };
    fd_set readfds;
    for (int sel=-1; sel<0; ) {
      FD_ZERO(&readfds);
      FD_SET(fd, &readfds);
      sel = select(fd+1, &readfds, NULL, NULL, &timemout);
      if (0<sel) {
        return FD_ISSET(fd, &readfds);
      } else if (-1==sel && errno==EINTR) {
        errno = 0;
      }
    }
  }
  return false;
}

inline int read(int fd, void* buf, int buf_len, int timeout_ms) {
  if (-1!=fd && buf && buf_len>0) {
    if (timeout_ms>=0) {
      timeval timemout = { timeout_ms/1000, 1000*(timeout_ms%1000) };
      fd_set readfds;
      for (int sel=-1; sel<0; ) {
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        sel = select(fd+1, &readfds, NULL, NULL, &timemout);
        if (0<sel) {
          if (FD_ISSET(fd, &readfds)) {
            return (int) ::read(fd, buf, buf_len);
          }
        } else if (-1==sel && errno==EINTR) {
          errno = 0;
        }
      }
    } else {
      return (int) ::read(fd, buf, buf_len);
    }
  }
  return 0;
}

inline bool is_zombie_status(pid_t pid) {
  std::ifstream statusFile("/proc/" + std::to_string(pid) + "/status");
  if (statusFile.is_open()) {
    for (std::string line; std::getline(statusFile, line); ) {
      if (line.rfind("State:", 0) == 0) { 
        size_t pos = line.find_first_of("ZSRDTIW", 6);
        if (std::string::npos!=pos) {
          //LOGD("State= %s", line.substr(pos).c_str());
          return 'Z'==line[pos];
        }
      }
    }
  }
  return false; // Process does not exist
}

class Fork {
  int pid_;  // fork child process id if > 0
  int cin_;  // [write to] child process's stdin
  int cout_; // [read from] child process's stdout
  int cerr_; // [read from] child process's stderr

public:
  Fork(Fork const&) = delete;
  Fork& operator=(Fork const&) = delete;
  Fork():pid_(0),cin_(-1),cout_(-1),cerr_(-1) {}
  ~Fork() { Wait(nullptr); }

  operator bool() {
    if (pid_>0) {
      if (0==kill(pid_, 0)) {
        if (!is_zombie_status(pid_)) {
          return true;
        }
        //LOGD("%pid:%d zombie.");
        Halt();
      } else {
        // EPERM : no permission
        // ESRCH : does not exist!
        //LOGD("error: %s(%d)",strerror(errno), errno);
        errno = 0;
      }
      pid_ = 0;
    }
    return false;
  }
  int pid() const { return pid_; } // no check, be cautious, see bool().

  // fork - spawn a process and run
  bool Run(std::function<int(void*)> const& process_main, void* process_main_params=nullptr);

  // [std::cin] write to child process's STDIN_FILENO
  int Write(void const* data, int len) const {
    return (cin_>0&&data&&len>0) ? (int) ::write(cin_, data, len):0;
  }

  // [std::cout] read from child process's STDOUT_FILENO
  int Read(void* buf, int buf_size, int timeout_ms=-1) const {
    return vive::read(cout_, buf, buf_size, timeout_ms);
  }

  // [std::cerr] read from child process's STDERR_FILENO
  int Error(void* buf, int buf_size, int timeout_ms=-1) const {
    return vive::read(cerr_, buf, buf_size, timeout_ms);
  }

  // wait process done
  bool Wait(int* status) {
    if (pid_>0) {
      // suspends execution of the calling process until process pid_ terminates
      bool const result = ::waitpid(pid_, status, 0)==pid_;
      if (cin_>0) close(cin_);
      if (cout_>0) close(cout_);
      if (cerr_>0) close(cerr_);
      pid_ = 0;
      cin_ = cout_ = cerr_ = -1;
      return result;
    }
    return false;
  }

  // Halt - really?
  int Halt() {
    int err = 0;
    if (pid_>0) {
      err = kill(pid_, SIGKILL); // kill -9

      if (cin_>0) close(cin_);
      if (cout_>0) close(cout_);
      if (cerr_>0) close(cerr_);

      // hum...
      pid_ = 0;
      cin_ = cout_ = cerr_ = -1;
    }
    return err;
  }
};

// unsigned long int, %lld

// this return unique id, but not the real number
//inline size_t gettid() { return std::hash<std::thread::id>{}(); }

inline std::string gettid_str(std::thread::id const& id = std::this_thread::get_id()) {
  std::stringstream ss;
  ss << std::this_thread::get_id();
  return ss.str();
}

} // namespace vive

#endif // #ifndef VIVE_LINUX_TOOLKIT_H
#endif // #if defined(__linux__) || defined(__FreeBSD__)
