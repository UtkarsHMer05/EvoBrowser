// M38 integration test: service-to-service engine-token auth over the real
// gRPC ControlService (auth-negative tests).
//
// Spawns `evo-scheduler-server` with EVO_ENGINE_TOKEN set, then drives it over
// a real gRPC channel:
//   1. Health with NO token -> UNAUTHENTICATED.
//   2. Health with a WRONG token -> UNAUTHENTICATED.
//   3. Health with the CORRECT token -> OK.
//   4. SubmitRun with the correct token -> accepted (auth passes through to
//      normal processing).
//   5. SubmitRun with NO token -> UNAUTHENTICATED (auth applies to every RPC).
//
// Also verifies the backwards-compatible default: a server WITHOUT
// EVO_ENGINE_TOKEN accepts calls with no token (auth disabled).
//
// Skipped when EVO_SCHEDULER_SERVER_BIN is unset.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <grpcpp/grpcpp.h>

#include "evo/execution.grpc.pb.h"

using evo::execution::v1::ControlService;
using evo::execution::v1::HealthRequest;
using evo::execution::v1::HealthResponse;
using evo::execution::v1::SubmitRunRequest;
using evo::execution::v1::SubmitRunResponse;

namespace {

const char* kToken = "m38-test-engine-token";

int pick_free_port() {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return 50098;
  struct sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  if (bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
    close(fd);
    return 50098;
  }
  socklen_t len = sizeof(addr);
  if (getsockname(fd, reinterpret_cast<struct sockaddr*>(&addr), &len) < 0) {
    close(fd);
    return 50098;
  }
  int port = ntohs(addr.sin_port);
  close(fd);
  return port;
}

const char* kSyntheticDagJson = R"json({
  "nodes": [
    {"id": "start", "kind": "trigger", "type": "start"}
  ],
  "edges": []
})json";

// Spawn the server with an optional EVO_ENGINE_TOKEN. Returns the pid.
pid_t spawn_server(const std::string& addr, bool with_token) {
  std::string env_cmd = "EVO_SCHEDULER_ADDR=" + addr +
                        " EVO_METRICS_PORT=0";
  if (with_token) {
    env_cmd += " EVO_ENGINE_TOKEN=";
    env_cmd += kToken;
  }
  env_cmd += " '" + std::string(std::getenv("EVO_SCHEDULER_SERVER_BIN")) + "'";
  pid_t pid = fork();
  if (pid == 0) {
    int devnull = open("/dev/null", O_WRONLY);
    if (devnull >= 0) {
      dup2(devnull, STDOUT_FILENO);
      dup2(devnull, STDERR_FILENO);
      close(devnull);
    }
    execl("/bin/sh", "sh", "-c", env_cmd.c_str(), (char*)nullptr);
    _exit(127);
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  return pid;
}

void reap(pid_t pid) {
  kill(pid, SIGTERM);
  int status = 0;
  for (int i = 0; i < 60 && !WIFEXITED(status) && !WIFSIGNALED(status); ++i) {
    pid_t r = waitpid(pid, &status, WNOHANG);
    if (r == 0) std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  if (!WIFEXITED(status) && !WIFSIGNALED(status)) kill(pid, SIGKILL);
}

std::unique_ptr<ControlService::Stub> connect(const std::string& addr) {
  std::shared_ptr<grpc::Channel> channel = grpc::CreateChannel(
      addr, grpc::InsecureChannelCredentials());
  auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(5);
  if (!channel->WaitForConnected(deadline)) return nullptr;
  return ControlService::NewStub(channel);
}

grpc::Status health(ControlService::Stub* stub, const char* token) {
  grpc::ClientContext ctx;
  if (token) ctx.AddMetadata("authorization", std::string("Bearer ") + token);
  HealthRequest req;
  HealthResponse resp;
  return stub->Health(&ctx, req, &resp);
}

grpc::Status submit(ControlService::Stub* stub, const char* token,
                    const std::string& run_id, SubmitRunResponse* resp) {
  grpc::ClientContext ctx;
  if (token) ctx.AddMetadata("authorization", std::string("Bearer ") + token);
  SubmitRunRequest req;
  req.set_org_id("org-a");
  req.set_workflow_version_id("v1");
  req.set_run_id(run_id);
  req.set_dag_json(kSyntheticDagJson);
  return stub->SubmitRun(&ctx, req, resp);
}

}  // namespace

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  const char* bin = std::getenv("EVO_SCHEDULER_SERVER_BIN");
  if (!bin) {
    printf("SKIP: M38 auth (set EVO_SCHEDULER_SERVER_BIN to enable)\n");
    return 0;
  }

  int failures = 0;
  auto fail = [&](const char* msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    ++failures;
  };

  // --- Server WITH a token: auth is enforced -------------------------------
  {
    const int port = pick_free_port();
    const std::string addr = "127.0.0.1:" + std::to_string(port);
    pid_t pid = spawn_server(addr, /*with_token=*/true);
    if (pid < 0) {
      fail("fork for token server failed");
      return 1;
    }
    auto stub = connect(addr);
    if (!stub) {
      fail("token server did not become reachable");
      reap(pid);
      return 1;
    }

    // 1. No token -> UNAUTHENTICATED.
    if (health(stub.get(), nullptr).error_code() != grpc::UNAUTHENTICATED) {
      fail("Health without token must be UNAUTHENTICATED");
    } else {
      printf("PASS: Health without token rejected UNAUTHENTICATED\n");
    }

    // 2. Wrong token -> UNAUTHENTICATED.
    if (health(stub.get(), "wrong-token").error_code() !=
        grpc::UNAUTHENTICATED) {
      fail("Health with wrong token must be UNAUTHENTICATED");
    } else {
      printf("PASS: Health with wrong token rejected UNAUTHENTICATED\n");
    }

    // 3. Correct token -> OK.
    if (!health(stub.get(), kToken).ok()) {
      fail("Health with correct token must succeed");
    } else {
      printf("PASS: Health with correct token accepted\n");
    }

    // 4. SubmitRun with correct token -> accepted.
    {
      SubmitRunResponse resp;
      grpc::Status s = submit(stub.get(), kToken, "m38-auth-ok", &resp);
      if (!s.ok() || !resp.accepted()) {
        fail("SubmitRun with correct token must be accepted");
      } else {
        printf("PASS: SubmitRun with correct token accepted\n");
      }
    }

    // 5. SubmitRun with NO token -> UNAUTHENTICATED.
    {
      SubmitRunResponse resp;
      grpc::Status s = submit(stub.get(), nullptr, "m38-auth-no", &resp);
      if (s.error_code() != grpc::UNAUTHENTICATED) {
        fail("SubmitRun without token must be UNAUTHENTICATED");
      } else {
        printf("PASS: SubmitRun without token rejected UNAUTHENTICATED\n");
      }
    }

    reap(pid);
  }

  // --- Server WITHOUT a token: auth disabled (backwards compatible) --------
  {
    const int port = pick_free_port();
    const std::string addr = "127.0.0.1:" + std::to_string(port);
    pid_t pid = spawn_server(addr, /*with_token=*/false);
    if (pid < 0) {
      fail("fork for no-token server failed");
      return 1;
    }
    auto stub = connect(addr);
    if (!stub) {
      fail("no-token server did not become reachable");
      reap(pid);
      return 1;
    }
    if (!health(stub.get(), nullptr).ok()) {
      fail("Health without token must succeed when auth is disabled");
    } else {
      printf("PASS: no-token server accepts calls (auth disabled default)\n");
    }
    reap(pid);
  }

  if (failures == 0) printf("ALL M38 AUTH TESTS PASSED\n");
  return failures == 0 ? 0 : 1;
}
