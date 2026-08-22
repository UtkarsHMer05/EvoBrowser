#pragma once

// Milestone 38: a minimal loopback HTTP server exposing the scheduler's
// Prometheus metrics at GET /metrics.
//
// This is the "small trusted adapter" from M38 step 3: it runs inside the
// scheduler process on a dedicated loopback port (EVO_METRICS_PORT, default
// 9090) and serves the text rendered by a MetricsRegistry. It is deliberately
// tiny — one thread, blocking accept, HTTP/1.0-style responses, GET /metrics
// only (everything else is 404). No external HTTP dependency.
//
// Security: binds 127.0.0.1 only by default (never 0.0.0.0), matching the
// gRPC server's local-only posture. Metrics carry counters/gauges and label
// values (org ids) — never secrets.

#include <atomic>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "evo/log.hpp"

namespace evo::service {

// A minimal HTTP/1.0 server serving a single GET route. Thread-safe start/stop.
class MetricsHttpServer {
 public:
  // `render` produces the response body for GET /metrics on each request.
  MetricsHttpServer(int port, std::function<std::string()> render)
      : port_(port), render_(std::move(render)) {}

  ~MetricsHttpServer() { stop(); }

  MetricsHttpServer(const MetricsHttpServer&) = delete;
  MetricsHttpServer& operator=(const MetricsHttpServer&) = delete;

  // Bind + start the accept loop on a background thread. Returns false if the
  // port cannot be bound (the scheduler still runs; metrics are just not
  // exposed over HTTP).
  bool start() {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) return false;
    int yes = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // loopback only
    addr.sin_port = htons(static_cast<uint16_t>(port_));
    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) <
        0) {
      ::close(listen_fd_);
      listen_fd_ = -1;
      return false;
    }
    if (::listen(listen_fd_, 16) < 0) {
      ::close(listen_fd_);
      listen_fd_ = -1;
      return false;
    }
    running_.store(true);
    thread_ = std::thread([this] { this->accept_loop(); });
    evo::log::info("metrics_http_listening",
                   {{"service", "evo-scheduler"},
                    {"port", std::to_string(port_)}});
    return true;
  }

  void stop() {
    if (!running_.exchange(false)) return;
    if (listen_fd_ >= 0) {
      ::shutdown(listen_fd_, SHUT_RDWR);
      ::close(listen_fd_);
      listen_fd_ = -1;
    }
    if (thread_.joinable()) thread_.join();
  }

 private:
  void accept_loop() {
    while (running_.load()) {
      int fd = ::accept(listen_fd_, nullptr, nullptr);
      if (fd < 0) {
        if (!running_.load()) break;
        continue;
      }
      handle(fd);
      ::close(fd);
    }
  }

  void handle(int fd) {
    // Read the request line (we only need the method + path). Bounded read.
    char buf[2048];
    const ssize_t n = ::recv(fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) return;
    buf[n] = '\0';

    // Parse "GET /metrics HTTP/1.x".
    bool is_metrics_get = false;
    if (std::strncmp(buf, "GET ", 4) == 0) {
      const char* path = buf + 4;
      if (std::strncmp(path, "/metrics", 8) == 0 &&
          (path[8] == ' ' || path[8] == '\r' || path[8] == '\n' ||
           path[8] == '?')) {
        is_metrics_get = true;
      }
    }

    std::string body;
    std::string status;
    std::string content_type;
    if (is_metrics_get) {
      body = render_ ? render_() : std::string();
      status = "200 OK";
      content_type = "text/plain; version=0.0.4; charset=utf-8";
    } else {
      body = "not found\n";
      status = "404 Not Found";
      content_type = "text/plain; charset=utf-8";
    }

    std::string resp = "HTTP/1.0 " + status + "\r\n" +
                       "Content-Type: " + content_type + "\r\n" +
                       "Content-Length: " + std::to_string(body.size()) +
                       "\r\n" + "Connection: close\r\n\r\n" + body;
    ::send(fd, resp.data(), resp.size(), 0);
  }

  int port_;
  std::function<std::string()> render_;
  int listen_fd_ = -1;
  std::atomic<bool> running_{false};
  std::thread thread_;
};

}  // namespace evo::service
