#include "../include/linux_toolkit.h"

//
// calling python from C++ https://docs.python.org/3/c-api/intro.html?highlight=py_initialize
// sudo apt-get install python3-dev
// try shell command : locate Python.h to find where the package is.
// on mine, it's '/usr/include/python3.6m/Python.h', note the upper case P.
//#include "Python.h"

namespace vive {

int create_directory(const char* dir, mode_t mode) {
  struct stat st;
  if (stat(dir, &st)!=0) {
    // directory does not exist. EEXIST for race condition */
    return (0==mkdir(dir, mode)) ? 1:-1;
  } else if (!S_ISDIR(st.st_mode)) {
    errno = ENOTDIR;
    return -2;
  }
  return 0;
}

bool file_exists(char const* filename) {
  struct stat st;
  return (0==stat(filename, &st) && S_ISREG(st.st_mode));
}

inline bool execvp_params(char* argv[], int& argc,
                          char cmd_buf[], int& cmd_size,
                          char const* cmdline) {
  int const max_argc = argc;
  int const max_cmd_size = cmd_size;
  argc = cmd_size = 0;

  char* arg = cmd_buf;

  for (char const* p=cmdline; *p; ++p) {
    if (' '!=*p) {
      if ('|'!=*p) {
        if (cmd_size<max_cmd_size) {
          cmd_buf[cmd_size] = *p;
        }
        ++cmd_size;
        if ('/'==*p && 0==argc) {
          arg = cmd_buf + cmd_size;
        }
      } else {
        // this probably the pipeline concatenate commands, we launch with bash command
        argv[0] = nullptr;
        argc = 1;
        cmd_size = 0;
        return true;
      }
    } else if (cmd_size>0) {
      if (cmd_size<max_cmd_size) {
        cmd_buf[cmd_size] = '\0';
        if (*arg) {
#if 1
          if (argc<max_argc) {
            argv[argc] = arg;
          }
#else
          if (argc>0) {
            if (argc<max_argc) {
              argv[argc] = arg;
            }
          } else {
            argv[argc] = arg;
            if (0==memcmp(arg, "python", 6)) {
              // this probably the pipeline concatenate commands, we launch with bash command
              argv[0] = nullptr;
              argc = 1;
              cmd_size = 0;
              return true;
            }
          }
#endif
          ++argc;
          ++cmd_size;
        }
      } else {
        ++argc;
        ++cmd_size;
      }
      arg = cmd_buf + cmd_size;
    }
  }

  if (argc<max_argc && cmd_size<max_cmd_size) {
    cmd_buf[cmd_size++] = '\0';

    if (arg && *arg) {
      argv[argc++] = arg;
    }

    if (argc<max_argc) {
      argv[argc++] = nullptr;
      return true;
    }
  }

  argc+=2; ++cmd_size;

  return false;
}

inline void shell_command_exec(char const* cmdline, char const* cwd) {
  //
  // The exec() functions only return if an error has occurred.
  // The return value is -1, and errno is set to indicate the error.
#if 1
  char* argv_[32];
  char cmd_[256];

  char** argv = argv_;
  char* cmd = cmd_;

  int argc = (int) (sizeof(argv_)/sizeof(argv_[0]));
  int cmd_size = (int) (sizeof(cmd_)/sizeof(cmd_[0]));

  if (!execvp_params(argv, argc, cmd, cmd_size, cmdline)) {
    argv = (char**) malloc(argc*sizeof(char*) + cmd_size);
    cmd = (char*) (argv + argc);
    if (!execvp_params(argv, argc, cmd, cmd_size, cmdline)) {
      free(argv);
      exit(1);
    }
  }

  // chdir("~/projects"); failed!
  char path[PATH_MAX];
  if (cwd) {
    if (cwd[0]=='~' && cwd[1]=='/') {
      char const* home = getenv("HOME");
      if (home) {
        sprintf(path+128, "%s%s", home, cwd+1);
        cwd = path+128;
        //
        // don't have to delete home, as Standard 7.20.4.5 says:
        // The getenv function returns a pointer to a string associated with
        // the matched list member. The string pointed to shall not be modified
        // by the program, but may be overwritten by a subsequent call to the
        // getenv function.
      }
    }
    int unused __attribute__((unused));
    unused = chdir(cwd);
  }

  if (argv[0]) {
    if (memcmp(argv[0], "python", 6)) {
      execvp(cmd, argv); // not python
    } else {
      // sudo apt-get install python3-dev
      // https://docs.python.org/3/c-api/intro.html?highlight=py_initialize
      //
#if 0
      //
      // not working...
      //
      char* envp[] = { NULL, NULL };
      if (cwd) {
        sprintf(path, "PYTHONPATH=\"%s\":PYTHONPATH", cwd);
        envp[0] = path;

        // not working...
        setenv("PYTHONPATH", cwd, 1);
        setenv("PATH", cwd, 1);
      }
      execvp(cmd, argv);

      //execvpe(cmd, argv, envp);
#else
      //printf("cmdline=%s\n", cmdline);
      execl("/bin/sh", "sh", "-c", cmdline, NULL);
#endif
    }
  } else {
    execl("/bin/sh", "sh", "-c", cmdline, NULL);
  }

  if (argv!=argv_) {
    free(argv);
  }
#else
  // a bad version... launch via shell... this is equivalent to run a one-line shell script,
  // such will generate 2 child processes. the folk process becomes the shell process, not the
  // actual running process we hope.
  //
  execl("/bin/sh", "sh", "-c", cmdline, NULL);

/*
  char *binaryPath = "/bin/bash";
  char *arg1 = "-c";
  char *arg2 = "echo "Visit $HOSTNAME:$PORT from your browser."";
  char *const env[] = {"HOSTNAME=www.linuxhint.com", "PORT=8080", NULL};
  execle(binaryPath, binaryPath, arg1, arg2, NULL, env);
*/
#endif
}

static int popenRWE(int rwepipe[3], char const* cmdline, char const* cwd, bool redirect_input_stream=false) {
  int pipes[6] = { -1, -1, -1, -1, -1, -1 };

  int const total_pipes = redirect_input_stream ? 6:4;
  for (int i=0; i<total_pipes; i+=2) {
    if (pipe(pipes+i)<0) {
      for (int j=0; j<i; ++j) {
        close(pipes[j]);
      }
      return -1;
    }
  }

  int const pid = ::fork();
  if (pid>0) {
    // parent process
    rwepipe[0] = pipes[0]; // parent process read from stdout
    close(pipes[1]);
    rwepipe[1] = pipes[2]; // parent process read from stderr
    close(pipes[3]);
    if (pipes[4]>=0) {
      close(pipes[4]);
    }
    rwepipe[2] = pipes[5]; // parent process may write to stdin
  } else if (pid == 0) {
    // child process
    dup2(pipes[1], STDOUT_FILENO);  // child process write to stdout
    dup2(pipes[3], STDERR_FILENO);  // child process write to stderr
    if (pipes[4]>=0) {
      dup2(pipes[4], STDIN_FILENO); // child process read from stdin
    }
    for (int i=0; i<total_pipes; ++i) {
      close(pipes[i]);
    }

    //
    // set locale?
    //setlocale(LC_ALL, "English");

    //
    // shell command execution
    // The exec() functions only return if an error has occurred.
    // The return value is -1, and errno is set to indicate the error.
    //
    shell_command_exec(cmdline, cwd);

    // not reach here!
    ssize_t unused __attribute__((unused));
    unused = write(STDERR_FILENO, "shell_command_exec() failed!\n", 29);
    exit(-1);
  } else {
    for (int i=0; i<total_pipes; ++i) {
      close(pipes[i]);
    }
  }
  return pid;
}

bool Fork::Run(std::function<int(void*)> const& proc_main, void* user) {
  if (pid_>0) {
    return false;
  }

  int pipes[3][2];
  for (int i=0; i<3; ++i) {
    if (pipe(pipes[i])<0) {
      for (int j=0; j<i; ++j) {
        close(pipes[j][0]);
        close(pipes[j][1]);
      }
      return false;
    }
  }

  pid_t pid = fork();
  if (pid>0) {
    close(pipes[0][0]);
    close(pipes[1][1]);
    close(pipes[2][1]);

    pid_ = (int) pid;
    cin_ = pipes[0][1];  // parent process may write to stdin
    cout_ = pipes[1][0]; // parent process read from stdout
    cerr_ = pipes[2][0]; // parent process read from stderr
    return true;
  } else if (0==pid) {
    dup2(pipes[0][0], STDIN_FILENO);  // child process read from stdin
    dup2(pipes[1][1], STDOUT_FILENO); // child process write to stdout
    dup2(pipes[2][1], STDERR_FILENO); // child process write to stderr
    for (int i=0; i<3; ++i) {
      close(pipes[i][0]);
      close(pipes[i][1]);
    }

    //
    // set locale?
    //setlocale(LC_ALL, "English");

    //
    // shell command execution
    // The exec() functions only return if an error has occurred.
    // The return value is -1, and errno is set to indicate the error.
    //
    exit(proc_main(user));
  } else {
    for (int i=0; i<3; ++i) {
      close(pipes[i][0]);
      close(pipes[i][1]);
    }
  }
  return false;
}

bool ShellExecute::Run(char const* command, bool redirect_input_stream, char const* working_directory) {
  if (pid_>0) {
    return false;
  }

  int pipes[3];
  int wid = popenRWE(pipes, command, working_directory, redirect_input_stream);
  if (wid <= 0) {
    return false;
  }

  pid_ = wid;
  on_start_(wid);

  constexpr int buf_size = sizeof(read_buf_) - 1;
  char* const buf_end = read_buf_ + buf_size; *buf_end = '\0';

  constexpr int frametime_ms = 100;
  timeval timeout = { frametime_ms/1000, frametime_ms*1000 };
  auto const target_frametime = std::chrono::milliseconds(frametime_ms);
  auto time_last_frame = std::chrono::system_clock::now();

  fd_set readfds, writefds_;
  fd_set* const writefds = redirect_input_stream ? (&writefds_):NULL;
  int const nfds = (writefds ? pipes[2]:pipes[1]) + 1;
  char const* cmd = nullptr;
  int cmd_len = 0;
  int status = 0;
  uint8_t ret = 128;

  for (bool resume=true; resume&&(pid_>0); ) {
    //
    // waitpid(): on success, returns the process ID of the child whose state has changed;
    //  if WNOHANG was specified and one or more child(ren) specified by pid exist, but have not yet changed state,
    //  then 0 is returned. On error, -1 is returned.
    //
    wid = waitpid((int)pid_, &status, WNOHANG);
    if (wid>0) {
      assert(wid==(int)pid_);
      if (WIFEXITED(status)) {
        ret = (uint8_t) WEXITSTATUS(status);
        //LOGI("[ShellExecute::Run] PID:%d exit with %d(LSB 8-bit)\n", (int) pid_, (int)ret);
      } else if (WIFSIGNALED(status)) {
        // the number of the signal that caused the child process to terminate. 
#ifdef WCOREDUMP
        if (WCOREDUMP(status)) {
          LOGE("[ShellExecute::Run] PID:%d CORE DUMP child:%d", (int) pid_, WTERMSIG(status));
        }
#endif
      } else {
        LOGW("[ShellExecute::Run] PID:%d status now unknown:%d", (int) pid_, status);
      }

      pid_ = 0;
      break;
    } else if (wid<0) {
      //LOGE("[ShellExecute::Run] PID:%d waitpid(WNOHANG) failed(%d)", (int) pid_, wid);
      break;
    }

    FD_ZERO(&readfds);
    FD_SET(pipes[0], &readfds);
    FD_SET(pipes[1], &readfds);

    if (writefds) {
      FD_ZERO(writefds);
      FD_SET(pipes[2], writefds);
    }

    if (0<select(nfds, &readfds, writefds, NULL, &timeout)) {
      int stream_state = 0;
      for (int err=0; err<2; ++err) {
        if (0!=FD_ISSET(pipes[err], &readfds)) {
          stream_state |= (1<<err);

          // try to read as much as possible, but take a break when
          // read data are all processed.
          for (char* read_ptr=read_buf_;; ) {
            int const max_read_size = (int) (buf_end - read_ptr);
            int const read_len = (int) ::read(pipes[err], read_ptr, max_read_size);

            char* const data_end = read_ptr + read_len;
            assert(data_end<=buf_end);

            char* data_begin = read_buf_;
            for (char* s=data_begin; s<data_end; ++s) {
              if (*s=='\r' || *s=='\n' || *s=='\0') {
                *s = '\0';
                if (data_begin<s) {
                  stdout_and_stderr_(data_begin, (int)(s-data_begin), err);
                }

                data_begin = s + 1;
                while (data_begin<data_end && (*data_begin=='\r'||*data_begin=='\n'||*data_begin=='\0')) {
                  ++data_begin;
                }
                s = data_begin;
              }
            }

            int const residue = (int)(data_end-data_begin);
            if (residue>0) {
              if (is_readable(pipes[err], 1000)) {
                memmove(read_buf_, data_begin, residue);
                read_ptr = read_buf_ + residue; // continue the read...
              } else {
                *data_end = '\0';
                stdout_and_stderr_(data_begin, residue, err);
                break;
              }
            } else {
              break;
            }
          }
        }
      }

      // input command 
      if (writefds && 0!=FD_ISSET(pipes[2], writefds)) {
        resume = stdin_(cmd, cmd_len);
        if (cmd && cmd_len>0) {
          ssize_t unused __attribute__((unused));
          unused = write(pipes[2], cmd, cmd_len);
          if ('\n'!=cmd[cmd_len-1]) {
            unused = write(pipes[2], "\n", 1); // a '\n' to complete a command
          }
        } else if (resume && 0==stream_state) {
          std::this_thread::sleep_until(time_last_frame+target_frametime);
          time_last_frame = std::chrono::system_clock::now();
        }
      }
    }
  }

  if (pid_>0) {
    int const log_lv = (0==wid) ? LOG_INFO:LOG_ERROR;
    sprintf(read_buf_, "[ShellExecute::Run] force close pid:%d%s...", (int) pid_, 0==wid ? "(user's request)":"(waitpid failed)");

    // notify it's going to kill -9
    on_interrupt_((int) pid_);

    // kill -9
    status = kill((int) pid_, SIGKILL);

    // suspends execution of the calling process until a child process pid has changed state
    wid = waitpid((int) pid_, &status, 0);

    if (wid==(int)pid_) {
      if (WIFSIGNALED(status)) {
        LOG(log_lv, "%s KILLED", read_buf_);
      } else if (WIFEXITED(status)) {
        ret = (uint8_t) WEXITSTATUS(status);
        LOG(log_lv, "%s EXIT with %d", read_buf_, (int)ret);
      } else {
        LOG(log_lv, "%s UNKNOWN!", read_buf_);
      }
    } else {
      LOGE("%s FAILED!?", read_buf_);
    }

    pid_ = 0;
  }

  for (int i=0; i<3; ++i) {
    if (pipes[i]>=0) {
      close(pipes[i]);
      pipes[i] = -1;
    }
  }

  on_exit_(ret);

  return true;
}

bool ShellExecute::Kill(int timeout) {
  if ((int)pid_>0) {
    // notify it's going to kill -9
    on_interrupt_((int) pid_);

    // kill -9, send SIGKILL signal to the child process
    if (0==kill((int) pid_, SIGKILL)) {
      if (timeout>0) {
        for (; 0!=pid_&&timeout>0; timeout-=50) {
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
      } else if (timeout<0) { // wait forever
        constexpr int FOREVER = 1000000; // 1000 seconds
        for (timeout=50; 0!=pid_&&timeout<FOREVER; timeout+=50) {
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
          if (0==(timeout%10000)) {
            LOGW("[ShellExecute::Kill] %d seconds", timeout/1000);
          }
        }
      }
    } else {
      LOGE("[ShellExecute::Kill] failed(%i)", errno);

      if (EINVAL==errno) {
        // The value of the sig argument is an invalid or unsupported signal number.
      } else if (EPERM==errno) {
        // The process does not have permission to send the signal to any receiving process.
      } else if (ESRCH==errno) {
        // No process or process group can be found corresponding to that specified by pid.
        pid_ = 0;
      }

      errno = 0;
    }
  }

  return 0==pid_;
}

bool shell_execute(char const* cmdline, std::function<void(char const*, int, int)> const& std_out_err, char const* cwd) {
  if (cmdline) {
    if (std_out_err) {
      class shell_exe : public ShellExecute {
        std::function<void(char const*, int, int)> const& func_;
        void stdout_and_stderr_(char const* msg, int len, int err) override {
          func_(msg, len, err);
        }

      public:
        shell_exe(std::function<void(char const*, int, int)> const& f):func_(f) {}
        ~shell_exe()=default;
      } exec(std_out_err);
      return exec.Run(cmdline, false, cwd);
    } else {
      return shell_execute(cmdline, cwd);
    }
  }
  return false;
}

bool shell_execute(char const* cmdline,
                   std::function<void(char const* msg, int len, int error)> const& std_out_err,
                   std::function<bool(char const*& cmd, int& len)> const& std_in, char const* cwd) {
  if (cmdline) {
    if (std_out_err && std_in) {
      class shell_exe2 : public ShellExecute {
        std::function<void(char const*, int, int)> const& func_out_;
        std::function<bool(char const*& cmd, int& len)> const& func_in_;
        void stdout_and_stderr_(char const* msg, int len, int error) override {
          func_out_(msg, len, error);
        }
        bool stdin_(char const*& cmd, int& len) override {
          return func_in_(cmd, len);
        }

      public:
        shell_exe2(std::function<void(char const*, int, int)> const& f1,
                   std::function<bool(char const*& cmd, int& len)> const& f2):
          func_out_(f1),func_in_(f2) {}
        ~shell_exe2()=default;
      } exec(std_out_err, std_in);
      return exec.Run(cmdline, true, cwd);
    } else {
      return shell_execute(cmdline, std_out_err, cwd);
    }
  }

  return false;
}

/*
// this will redirect all child signals
static void sig_handler(int sig) {
  if (SIGCHLD==sig) {
    int retval;
    wait(&retval);
    printf("CATCH SIGNAL PID=%d\n", getpid());
  }
}
*/

bool shell_execute(char const* cmdline, char const* cwd) {
  if (cmdline) {
    auto const pid = ::fork();
    if (0==pid) {
      shell_command_exec(cmdline, cwd);
      exit(1);
    } else if (pid>0) {
      int status = 0;
      if (waitpid(pid, &status, 0)>=0) {
        status = WEXITSTATUS(status);
        return 0==status;
      }
    }
  }

  return false;
}

int for_all_process(char const* name, std::function<bool(char const* msg, int len)> const& callback) {
  if (name) {
    class ps_aux_grep : public ShellExecute {
      std::function<bool(char const* msg, int len)> const& callback_;
      int find_processes_;
      void stdout_and_stderr_(char const* msg, int len, int error) override {
        if (0==error && 0==strstr(msg, "grep") && callback_(msg, len)) {
          ++find_processes_;
        }
      }

    public:
      ps_aux_grep(std::function<bool(char const* msg, int len)> const& f):callback_(f) {}
      ~ps_aux_grep()=default;
      int Start(char const* name) {
        if (name) {
          find_processes_ = 0;
          char cmd[256];
          sprintf(cmd, "ps aux | grep %s", name);
          if (Run(cmd)) {
            return find_processes_;
          }
        }
        return 0;
      }
    };
    return ps_aux_grep(callback).Start(name);
  }
  return 0;
}

char* get_self_exe_path(char* path, int max_path_len) {
  int const count = (int) readlink("/proc/self/exe", path, max_path_len);
  if (0<=count) {
    path[count] = '\0';
    char* app_name = path;
    for (int i=0; i<count; ++i) {
      if (path[i]=='/') {
        app_name = path + i;
      } else if (path[i]=='\\') { // only happens on Windows?
        app_name = path + i;
        *app_name = '/';
      }
    }

    if (app_name>path) {
      ++app_name;
    }

    return app_name;
  }
  return nullptr;
}

char* get_process_name_by_pid(char* path, int max_name_len, int pid) {
  if (path && max_name_len>32 && pid>0) {
    sprintf(path, "/proc/%d/cmdline", pid);
    FILE* file = fopen(path, "r");
    int count = 0;
    if (file) {
      count = (int) fread(path, 1, max_name_len-1, file);
      fclose(file);
    }

    char* proc_name = path;
    if (0<=count) {
      path[count] = '\0';
      for (int i=0; i<count; ++i) {
        if (path[i]=='/') {
          proc_name = path + i;
        } else if (path[i]=='\\') { // only happens on Windows?
          proc_name = path + i;
          *proc_name = '/';
        }
      }

      if (proc_name>path) {
        ++proc_name;
      }
    } else {
      memcpy(proc_name, "unknown", 8);
    }
    return proc_name;
  }
  return nullptr;
}

}
