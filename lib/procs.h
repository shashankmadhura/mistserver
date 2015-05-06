/// \file procs.h
/// Contains generic function headers for managing processes.

#pragma once
#include <unistd.h>
#include <string>
#include <set>
#include <vector>

#ifdef _WIN32
  #include <windows.h> 
  #define pid_t HANDLE
#endif

/// Contains utility code, not directly related to streaming media
namespace Util {

  /// Deals with spawning, monitoring and stopping child processes
  class Procs {
    private:
      static std::set<pid_t> plist; ///< Holds active processes
      #ifndef _WIN32
      static bool handler_set; ///< If true, the sigchld handler has been setup.
      static void childsig_handler(int signum);
      static void setHandler();
      #endif
      static void exit_handler();
      static void runCmd(std::string & cmd);
    public:
      static std::string getOutputOf(char * const * argv);
      static pid_t runArgs(char * const * argv);
      static void Stop(pid_t name);
      static void Murder(pid_t name);
      static void StopAll();
      static int Count();
      static bool isActive(pid_t name);
      static bool isRunning(pid_t pid);
  };

}
