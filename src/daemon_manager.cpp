#include "daemon_manager.h"
#include <fcitx-utils/log.h>
#include <unistd.h>
#include <sys/prctl.h>
#include <signal.h>
#include <cstring>
#include <cerrno>
#include <sys/wait.h>

DaemonManager::DaemonManager() : daemon_pid_(-1) {
}

DaemonManager::~DaemonManager() {
    stop();
}

void DaemonManager::start(const std::string& daemonPath,
                          const std::string& appId,
                          const std::string& accessToken,
                          bool developerMode) {
    if (developerMode) {
        FCITX_INFO() << "Developer mode enabled, skipping daemon auto-start.";
        return;
    }

    // Don't start if already running
    if (isRunning()) {
        FCITX_INFO() << "Daemon already running with PID: " << daemon_pid_;
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        prctl(PR_SET_PDEATHSIG, SIGTERM);

        setenv("ANYTALK_APP_ID", appId.c_str(), 1);
        setenv("ANYTALK_ACCESS_TOKEN", accessToken.c_str(), 1);
        setenv("ANYTALK_RESOURCE_ID", "volc.seedasr.sauc.duration", 0);
        // Ensure RUST_LOG is set so tracing defaults to info if not specified
        setenv("RUST_LOG", "info", 0);

        std::string path = daemonPath;
        if (path.empty()) {
            path = "anytalk-daemon";
        }

        execlp(path.c_str(), path.c_str(), nullptr);

        // Only reached if execlp fails
        // We can't log easily here since we removed redirection,
        // but fcitx might capture stderr or it goes to journal.
        _exit(1);
    } else if (pid > 0) {
        daemon_pid_ = pid;
        FCITX_INFO() << "Started anytalk-daemon with PID: " << daemon_pid_;
    } else {
        FCITX_ERROR() << "Fork failed: " << strerror(errno);
    }
}

void DaemonManager::stop() {
    if (daemon_pid_ > 0) {
        // Check if process is still running
        if (kill(daemon_pid_, 0) == 0) {
            FCITX_INFO() << "Stopping daemon with PID: " << daemon_pid_;
            kill(daemon_pid_, SIGTERM);

            // Wait for process to terminate (with timeout)
            int status;
            for (int i = 0; i < 10; i++) {
                pid_t result = waitpid(daemon_pid_, &status, WNOHANG);
                if (result == daemon_pid_) {
                    FCITX_INFO() << "Daemon terminated successfully";
                    break;
                }
                usleep(100000); // 100ms
            }

            // Force kill if still alive
            if (kill(daemon_pid_, 0) == 0) {
                FCITX_INFO() << "Daemon did not terminate, force killing";
                kill(daemon_pid_, SIGKILL);
                waitpid(daemon_pid_, &status, 0);
            }
        }
        daemon_pid_ = -1;
    }
}

bool DaemonManager::isRunning() const {
    if (daemon_pid_ <= 0) {
        return false;
    }

    // Use kill with signal 0 to check if process exists
    // Returns 0 if process exists, -1 if it doesn't
    return kill(daemon_pid_, 0) == 0;
}
