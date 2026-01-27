#pragma once
#include <string>
#include <sys/types.h>

class DaemonManager {
public:
    DaemonManager();
    ~DaemonManager();

    void start(const std::string& daemonPath,
               const std::string& appId,
               const std::string& accessToken,
               bool developerMode);
    void stop();
    bool isRunning() const;

private:
    pid_t daemon_pid_{-1};
};
