#include "ipc_client.h"
#include "constants.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <string>
#include <cerrno>
#include <json-c/json.h>
#include <fcitx-utils/log.h>

static std::string anytalk_socket_path() {
  const char *runtime_dir = std::getenv("XDG_RUNTIME_DIR");
  if (runtime_dir && *runtime_dir) {
    return std::string(runtime_dir) + "/anytalk.sock";
  }
  const char *uid = std::getenv("UID");
  if (uid && *uid) {
    return std::string("/run/user/") + uid + "/anytalk.sock";
  }
  return "/tmp/anytalk.sock";
}

IpcClient::IpcClient() = default;

void IpcClient::setCallbacks(TextCb partial, TextCb final, TextCb status) {
  on_partial_ = std::move(partial);
  on_final_ = std::move(final);
  on_status_ = std::move(status);
}

void IpcClient::start() {
  if (running_) return;
  running_ = true;
  recv_thread_ = std::thread(&IpcClient::recvLoop, this);
}

void IpcClient::stop() {
  running_ = false;
  {
    std::lock_guard<std::mutex> lock(sock_mutex_);
    closeSocketLocked();
  }
  if (recv_thread_.joinable()) {
    recv_thread_.join();
  }
}

void IpcClient::connectSocket() {
  {
    std::lock_guard<std::mutex> lock(sock_mutex_);
    if (sock_ >= 0) return;
  }

  int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    FCITX_ERROR() << "Failed to create socket";
    return;
  }

  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  std::string path = anytalk_socket_path();
  std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path.c_str());

  FCITX_DEBUG() << "Connecting to " << path;

  if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
    ::close(fd);
    return;
  }
  FCITX_DEBUG() << "Connected to " << path;
  {
    std::lock_guard<std::mutex> lock(sock_mutex_);
    if (sock_ >= 0) {
      ::close(fd);
      return;
    }
    sock_ = fd;
    recv_buffer_.clear();
  }
}

void IpcClient::sendJson(const std::string &json) {
  bool need_connect = false;
  {
    std::lock_guard<std::mutex> lock(sock_mutex_);
    need_connect = sock_ < 0;
  }
  if (need_connect) {
    connectSocket();
  }

  int fd = -1;
  {
    std::lock_guard<std::mutex> lock(sock_mutex_);
    fd = sock_;
  }
  if (fd < 0) {
    FCITX_ERROR() << "Socket not connected, cannot send: " << json;
    return;
  }
  FCITX_DEBUG() << "Sending JSON: " << json;
  auto send_all = [&](const char *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
      ssize_t n = ::send(fd, data + sent, len - sent, 0);
      if (n > 0) {
        sent += static_cast<size_t>(n);
        continue;
      }
      if (n < 0 && errno == EINTR) {
        continue;
      }
      return false;
    }
    return true;
  };
  if (!send_all(json.data(), json.size()) || !send_all("\n", 1)) {
    std::lock_guard<std::mutex> lock(sock_mutex_);
    if (sock_ == fd) {
      closeSocketLocked();
    }
  }
}

void IpcClient::sendStart() {
  std::string json = std::string("{\"") + Constants::JSON_KEY_TYPE + "\":\"" +
                     Constants::MSG_TYPE_START + "\",\"" + Constants::JSON_KEY_MODE + "\":\"" +
                     Constants::JSON_KEY_TOGGLE + "\"}";
  sendJson(json);
}

void IpcClient::sendStop() {
  std::string json = std::string("{\"") + Constants::JSON_KEY_TYPE + "\":\"" +
                     Constants::MSG_TYPE_STOP + "\"}";
  sendJson(json);
}

void IpcClient::sendCancel() {
  std::string json = std::string("{\"") + Constants::JSON_KEY_TYPE + "\":\"" +
                     Constants::MSG_TYPE_CANCEL + "\"}";
  sendJson(json);
}

bool IpcClient::ensureConnected() {
  bool need_connect = false;
  {
    std::lock_guard<std::mutex> lock(sock_mutex_);
    need_connect = sock_ < 0;
  }
  if (need_connect) {
    connectSocket();
    {
      std::lock_guard<std::mutex> lock(sock_mutex_);
      need_connect = sock_ < 0;
    }
    if (need_connect) {
      ::usleep(200 * 1000);
      return false;
    }
  }
  return true;
}

bool IpcClient::receiveData(char* buffer, size_t buffer_size, ssize_t& bytes_read) {
  int fd = -1;
  {
    std::lock_guard<std::mutex> lock(sock_mutex_);
    fd = sock_;
  }
  if (fd < 0) {
    return false;
  }

  bytes_read = ::recv(fd, buffer, buffer_size, 0);
  if (bytes_read <= 0) {
    {
      std::lock_guard<std::mutex> lock(sock_mutex_);
      if (sock_ == fd) {
        closeSocketLocked();
      }
    }
    if (on_status_) {
      on_status_(Constants::STATE_IDLE);
    }
    return false;
  }
  return true;
}

void IpcClient::handleJsonMessage(const std::string& line) {
  if (line.empty()) {
    return;
  }

  json_object *obj = json_tokener_parse(line.c_str());
  if (!obj) {
    return;
  }

  json_object *type_obj = nullptr;
  if (json_object_object_get_ex(obj, Constants::JSON_KEY_TYPE, &type_obj)) {
    const char *type = json_object_get_string(type_obj);
    if (type && std::strcmp(type, Constants::MSG_TYPE_PARTIAL) == 0) {
      json_object *text_obj = nullptr;
      if (json_object_object_get_ex(obj, Constants::JSON_KEY_TEXT, &text_obj) && on_partial_) {
        on_partial_(json_object_get_string(text_obj));
      }
    } else if (type && std::strcmp(type, Constants::MSG_TYPE_FINAL) == 0) {
      json_object *text_obj = nullptr;
      if (json_object_object_get_ex(obj, Constants::JSON_KEY_TEXT, &text_obj) && on_final_) {
        on_final_(json_object_get_string(text_obj));
      }
    } else if (type && std::strcmp(type, Constants::MSG_TYPE_STATUS) == 0) {
      json_object *state_obj = nullptr;
      if (json_object_object_get_ex(obj, Constants::JSON_KEY_STATE, &state_obj) && on_status_) {
        on_status_(json_object_get_string(state_obj));
      }
    }
  }
  json_object_put(obj);
}

void IpcClient::processMessages() {
  size_t pos = 0;
  while ((pos = recv_buffer_.find('\n')) != std::string::npos) {
    std::string line = recv_buffer_.substr(0, pos);
    recv_buffer_.erase(0, pos + 1);
    handleJsonMessage(line);
  }
}

void IpcClient::recvLoop() {
  while (running_) {
    if (!ensureConnected()) {
      continue;
    }

    char buf[4096];
    ssize_t n = 0;
    if (!receiveData(buf, sizeof(buf), n)) {
      continue;
    }

    recv_buffer_.append(buf, static_cast<size_t>(n));
    processMessages();
  }
}

void IpcClient::closeSocketLocked() {
  if (sock_ >= 0) {
    ::shutdown(sock_, SHUT_RDWR);
    ::close(sock_);
    sock_ = -1;
  }
  recv_buffer_.clear();
}
