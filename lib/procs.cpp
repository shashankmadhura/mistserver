/// \file procs.cpp
/// Contains generic functions for managing processes.

#include <string.h>
#include <sys/types.h>
#include <signal.h>

#ifndef _WIN32
  #if defined(__FreeBSD__) || defined(__APPLE__) || defined(__MACH__)
    #include <sys/wait.h>
  #else
    #include <wait.h>
  #endif
  #include <pwd.h>
#endif
#include <errno.h>
#include <iostream>
#include <sys/types.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include "timing.h"
#include "procs.h"
#include "defines.h"

std::set<pid_t> Util::Procs::plist;
#ifndef _WIN32
bool Util::Procs::handler_set = false;
#endif


static bool childRunning(pid_t p) {
#ifdef _WIN32
  DWORD response;
  GetExitCodeProcess(p, &response);
  return response == STILL_ACTIVE;
#else
  pid_t ret = waitpid(p, 0, WNOHANG);
  if (ret == p) {
    return false;
  }
  if (ret < 0 && errno == EINTR) {
    return childRunning(p);
  }
  return !kill(p, 0);
#endif
}

/// sends sig 0 to process (pid). returns true if process is running
bool Util::Procs::isRunning(pid_t pid){
#ifdef _WIN32
  DWORD response;
  GetExitCodeProcess(pid, &response);
  return response == STILL_ACTIVE;
#else
  return !kill(pid, 0);
#endif
}

/// Called at exit of any program that used a Start* function.
/// Waits up to 1 second, then sends SIGINT signal to all managed processes.
/// After that waits up to 5 seconds for children to exit, then sends SIGKILL to
/// all remaining children. Waits one more second for cleanup to finish, then exits.
void Util::Procs::exit_handler() {
#ifndef _WIN32
  int waiting = 0;
  std::set<pid_t> listcopy = plist;
  std::set<pid_t>::iterator it;
  if (listcopy.empty()) {
    return;
  }

  //wait up to 0.5 second for applications to shut down
  while (!listcopy.empty() && waiting <= 25) {
    for (it = listcopy.begin(); it != listcopy.end(); it++) {
      if (!childRunning(*it)) {
        listcopy.erase(it);
        break;
      }
      if (!listcopy.empty()) {
        Util::sleep(20);
        ++waiting;
      }
    }
  }
  if (listcopy.empty()) {
    return;
  }

  DEBUG_MSG(DLVL_DEVEL, "Sending SIGINT to remaining %d children", (int)listcopy.size());
  //send sigint to all remaining
  if (!listcopy.empty()) {
    for (it = listcopy.begin(); it != listcopy.end(); it++) {
      DEBUG_MSG(DLVL_DEVEL, "SIGINT %d", *it);
      kill(*it, SIGINT);
    }
  }

  DEBUG_MSG(DLVL_DEVEL, "Waiting up to 5 seconds for %d children to terminate.", (int)listcopy.size());
  waiting = 0;
  //wait up to 5 seconds for applications to shut down
  while (!listcopy.empty() && waiting <= 250) {
    for (it = listcopy.begin(); it != listcopy.end(); it++) {
      if (!childRunning(*it)) {
        listcopy.erase(it);
        break;
      }
      if (!listcopy.empty()) {
        Util::sleep(20);
        ++waiting;
      }
    }
  }
  if (listcopy.empty()) {
    return;
  }

  DEBUG_MSG(DLVL_DEVEL, "Sending SIGKILL to remaining %d children", (int)listcopy.size());
  //send sigkill to all remaining
  if (!listcopy.empty()) {
    for (it = listcopy.begin(); it != listcopy.end(); it++) {
      DEBUG_MSG(DLVL_DEVEL, "SIGKILL %d", *it);
      kill(*it, SIGKILL);
    }
  }

  DEBUG_MSG(DLVL_DEVEL, "Waiting up to a second for %d children to terminate.", (int)listcopy.size());
  waiting = 0;
  //wait up to 1 second for applications to shut down
  while (!listcopy.empty() && waiting <= 50) {
    for (it = listcopy.begin(); it != listcopy.end(); it++) {
      if (!childRunning(*it)) {
        listcopy.erase(it);
        break;
      }
      if (!listcopy.empty()) {
        Util::sleep(20);
        ++waiting;
      }
    }
  }
  if (listcopy.empty()) {
    return;
  }
  DEBUG_MSG(DLVL_DEVEL, "Giving up with %d children left.", (int)listcopy.size());
#endif
}

#ifndef _WIN32
/// \todo Implement for _WIN32: https://msdn.microsoft.com/en-us/library/windows/desktop/ms682066(v=vs.85).aspx

/// Sets up exit and childsig handlers.
/// Called by every Start* function.
void Util::Procs::setHandler() {
  if (!handler_set) {
    struct sigaction new_action;
    new_action.sa_handler = childsig_handler;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = 0;
    sigaction(SIGCHLD, &new_action, NULL);
    atexit(exit_handler);
    handler_set = true;
  }
}

/// Used internally to capture child signals and update plist.
void Util::Procs::childsig_handler(int signum) {
  if (signum != SIGCHLD) {
    return;
  }
  int status;
  pid_t ret = -1;
  while (ret != 0) {
    ret = waitpid(-1, &status, WNOHANG);
    if (ret <= 0) { //ignore, would block otherwise
      if (ret == 0 || errno != EINTR) {
        return;
      }
      continue;
    }
    int exitcode;
    if (WIFEXITED(status)) {
      exitcode = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
      exitcode = -WTERMSIG(status);
    } else { // not possible
      return;
    }

    plist.erase(ret);
#if DEBUG >= DLVL_HIGH
    if (!isActive(pname)) {
      DEBUG_MSG(DLVL_HIGH, "Process %d fully terminated", ret);
    } else {
      DEBUG_MSG(DLVL_HIGH, "Child process %d exited", ret);
    }
#endif

  }
}
#endif

#ifdef _WIN32
static std::string argvToCmdline(char * const * a){
  std::string ret;
  unsigned int i = 0;
  while (i < 50 && a[i]){
    std::string tmp = a[i];
    /*
    while (tmp.find('"') != std::string::npos){
      tmp.replace
    }
    */
    if (ret.size()){ret += " ";}
    if (tmp.find(' ') != std::string::npos){
      ret += "\"" + tmp + "\"";
    }else{
      ret += tmp;
    }
    ++i;
  }
  INFO_MSG("Starting: %s", ret.c_str());
  return ret;
}
#endif

/// Runs the given command and returns the stdout output as a string.
std::string Util::Procs::getOutputOf(char * const * argv) {
  std::string ret;
#ifdef _WIN32
  HANDLE read_end = NULL;
  HANDLE write_end = NULL;
  SECURITY_ATTRIBUTES saAttr;
  saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
  saAttr.bInheritHandle = TRUE;
  saAttr.lpSecurityDescriptor = NULL;
  if (!CreatePipe(&read_end, &write_end, &saAttr, 0)) return "";
  if (!SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0)) return "";
  PROCESS_INFORMATION piProcInfo;
  STARTUPINFO siStartInfo;
  ZeroMemory( &piProcInfo, sizeof(PROCESS_INFORMATION));
  ZeroMemory( &siStartInfo, sizeof(STARTUPINFO));
  siStartInfo.cb = sizeof(STARTUPINFO);
  siStartInfo.hStdError = write_end;
  siStartInfo.hStdOutput = write_end;
  siStartInfo.dwFlags |= STARTF_USESTDHANDLES;
  std::string commandline = argvToCmdline(argv);
  if (!CreateProcess(0, (char*)commandline.c_str(), 0, 0, TRUE, 0, 0, 0, &siStartInfo, &piProcInfo)) return "";
  CloseHandle(write_end);
  CloseHandle(piProcInfo.hProcess);
  CloseHandle(piProcInfo.hThread);
  DWORD dwRead = 0;
  char chBuf[4096];
  while (ReadFile(read_end, chBuf, sizeof(chBuf), &dwRead, NULL) && dwRead != 0){
    ret.append(chBuf, dwRead);
  }
  CloseHandle(read_end);
#else
  int pipeout[2];
  if (pipe(pipeout) < 0){
    DEBUG_MSG(DLVL_ERROR, "stdout pipe creation failed for process %s, reason: %s", argv[0], strerror(errno));
    return "";
  }
  pid_t myProc = fork();
  if (myProc == 0){
    int devnull = open("/dev/null", O_RDWR);
    dup2(devnull, STDIN_FILENO);
    dup2(devnull, STDERR_FILENO);
    close(pipeout[0]); // close unused read end
    dup2(pipeout[1], STDOUT_FILENO);
    close(pipeout[1]);
    execvp(argv[0], argv);
    DEBUG_MSG(DLVL_ERROR, "execvp failed for process %s, reason: %s", argv[0], strerror(errno));
    exit(42);
  }
  close(pipeout[1]);
  while (isActive(myProc)) {
    Util::sleep(100);
  }
  FILE * outFile = fdopen(pipeout[0], "r");
  char * fileBuf = 0;
  size_t fileBufLen = 0;
  while (!(feof(outFile) || ferror(outFile)) && (getline(&fileBuf, &fileBufLen, outFile) != -1)) {
    ret += fileBuf;
  }
  fclose(outFile);
  free(fileBuf);
  close(pipeout[0]);
#endif
  return ret;
}

/// Starts a new process with given fds if the name is not already active.
/// \return 0 if process was not started, process PID otherwise.
/// \arg argv Command for this process.
/// \arg fdin Standard input file descriptor. If null, /dev/null is assumed. Otherwise, if arg contains -1, a new fd is automatically allocated and written into this arg. Then the arg will be used as fd.
/// \arg fdout Same as fdin, but for stdout.
/// \arg fdout Same as fdin, but for stderr.
pid_t Util::Procs::runArgs(char * const * argv) {
#ifdef _WIN32
  PROCESS_INFORMATION piProcInfo;
  STARTUPINFO siStartInfo;
  ZeroMemory( &piProcInfo, sizeof(PROCESS_INFORMATION));
  ZeroMemory( &siStartInfo, sizeof(STARTUPINFO));
  siStartInfo.cb = sizeof(STARTUPINFO);
  std::string commandline = argvToCmdline(argv);
  if (!CreateProcess(0, (char*)commandline.c_str(), 0, 0, TRUE, 0, 0, 0, &siStartInfo, &piProcInfo)) return 0;
  CloseHandle(piProcInfo.hThread);
  return piProcInfo.hProcess;
#else
  pid_t pid;
  setHandler();
  pid = fork();
  if (pid == 0) { //child
    execvp(argv[0], argv);
    DEBUG_MSG(DLVL_ERROR, "execvp failed for process %s, reason: %s", argv[0], strerror(errno));
    exit(42);
  } else if (pid == -1) {
    DEBUG_MSG(DLVL_ERROR, "fork failed for process %s, reason: %s", argv[0], strerror(errno));
    return 0;
  } else { //parent
    plist.insert(pid);
    DEBUG_MSG(DLVL_HIGH, "Process %s started, PID %d", argv[0], pid);
  }
  return pid;
#endif
}

/// Stops the process with this pid, if running.
/// \arg name The PID of the process to stop.
void Util::Procs::Stop(pid_t name) {
#ifdef _WIN32
  Murder(name);
#else
  kill(name, SIGTERM);
#endif
}

/// Stops the process with this pid, if running.
/// \arg name The PID of the process to murder.
void Util::Procs::Murder(pid_t name) {
#ifdef _WIN32
  TerminateProcess(name, -1);
#else
  kill(name, SIGKILL);  
#endif
}

/// (Attempts to) stop all running child processes.
void Util::Procs::StopAll() {
  std::set<pid_t> listcopy = plist;
  std::set<pid_t>::iterator it;
  for (it = listcopy.begin(); it != listcopy.end(); it++) {
    Stop(*it);
  }
}

/// Returns the number of active child processes.
int Util::Procs::Count() {
  return plist.size();
}

/// Returns true if a process with this PID is currently active.
bool Util::Procs::isActive(pid_t name) {
  #ifdef _WIN32
  return isRunning(name);
  #else
  return (plist.count(name) == 1 && isRunning(name));
  #endif
}

