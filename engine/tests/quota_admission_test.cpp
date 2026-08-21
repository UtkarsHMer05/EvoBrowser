// M36 integration test: multi-tenant admission control over the real gRPC
// ControlService.
//
// Spawns `evo-scheduler-server` with a per-org active-run cap of 1 and a long
// local synthetic sleep (so a submitted run stays ACTIVE), then drives it over
// a real gRPC channel:
//   1. Submit run #1 for org-a -> ACCEPTED.
//   2. Submit run #2 for org-a while #1 is still active -> REJECTED with
//      grpc::RESOURCE_EXHAUSTED (the per-org cap is exhausted).
//   3. Submit run #3 for org-b -> ACCEPTED (a different org has its own quota).
//   4. Health detail carries the quota gate's JSON snapshot (queue depth +
//      rejected/deferred counters, M36 step 6).
//   5. After run #1 reaches a terminal state, a NEW submission for org-a is
//      ACCEPTED again (the slot was released).
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
using evo::execution::v1::GetRunRequest;
using evo::execution::v1::GetRunResponse;
using evo::execution::v1::HealthRequest;
using evo::execution::v1::HealthResponse;
using evo::execution::v1::SubmitRunRequest;
using evo::execution::v1::SubmitRunResponse;

namespace {

int pick_free_port() {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return 50099;
  struct sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  if (bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
    close(fd);
    return 50099;
  }
  socklen_t len = sizeof(addr);
  if (getsockname(fd, reinterpret_cast<struct sockaddr*>(&addr), &len) < 0) {
    close(fd);
    return 50099;
  }
  int port = ntohs(addr.sin_port);
  close(fd);
  return port;
}

// A synthetic-only DAG (runs in-process via the local executor). One node so
// the run stays ACTIVE for ~EVO_LOCAL_SLEEP_MS.
const char* kSyntheticDagJson = R"json({
  "nodes": [
    {"id": "start", "kind": "trigger", "type": "start"},
    {"id": "n0", "kind": "action", "type": "bench:sleep"}
  ],
  "edges": [
    {"from": "start", "to": "n0"}
  ]
})json";

bool is_terminal(evo::execution::v1::RunStatus st) {
  return st == evo::execution::v1::RunStatus::RUN_SUCCEEDED ||
         st == evo::execution::v1::RunStatus::RUN_FAILED ||
         st == evo::execution::v1::RunStatus::RUN_CANCELED;
}

}  // namespace

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  const char* bin = std::getenv("EVO_SCHEDULER_SERVER_BIN");
  if (!bin) {
    printf("SKIP: M36 admission (set EVO_SCHEDULER_SERVER_BIN to enable)\n");
    return 0;
  }

  const int port = pick_free_port();
  const std::string addr = "127.0.0.1:" + std::to_string(port);
  // Per-org active-run cap of 1; hold each local run ACTIVE for ~2s so the
  // second submission lands while the first is still running.
  std::string env_cmd = "EVO_SCHEDULER_ADDR=" + addr +
                        " EVO_QUOTA_MAX_ACTIVE_RUNS_PER_ORG=1"
                        " EVO_LOCAL_SLEEP_MS=2000"
                        " '" + std::string(bin) + "'";
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
  if (pid < 0) {
    fprintf(stderr, "FAIL: fork for server subprocess failed\n");
    return 1;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  int failures = 0;
  auto fail = [&](const char* msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    ++failures;
  };

  grpc::ChannelArguments args;
  std::shared_ptr<grpc::Channel> channel =
      grpc::CreateCustomChannel(addr, grpc::InsecureChannelCredentials(), args);
  auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(5);
  if (!channel->WaitForConnected(deadline)) {
    fail("server did not become reachable");
    kill(pid, SIGKILL);
    int status = 0;
    waitpid(pid, &status, 0);
    return 1;
  }
  std::unique_ptr<ControlService::Stub> stub = ControlService::NewStub(channel);

  auto submit = [&](const std::string& run_id, const std::string& org_id,
                    SubmitRunResponse* resp) {
    grpc::ClientContext ctx;
    SubmitRunRequest req;
    req.set_org_id(org_id);
    req.set_workflow_version_id("v1");
    req.set_run_id(run_id);
    req.set_dag_json(kSyntheticDagJson);
    return stub->SubmitRun(&ctx, req, resp);
  };

  // --- 1. First submission for org-a: ACCEPTED -----------------------------
  {
    SubmitRunResponse resp;
    grpc::Status s = submit("m36-run-a1", "org-a", &resp);
    if (!s.ok() || !resp.accepted()) fail("org-a run #1 should be accepted");
    else printf("PASS: org-a run #1 accepted\n");
  }

  // --- 2. Second submission for org-a while #1 active: RESOURCE_EXHAUSTED --
  {
    SubmitRunResponse resp;
    grpc::Status s = submit("m36-run-a2", "org-a", &resp);
    if (s.error_code() != grpc::RESOURCE_EXHAUSTED) {
      fail("org-a run #2 must be rejected with RESOURCE_EXHAUSTED");
    } else {
      printf("PASS: org-a run #2 rejected RESOURCE_EXHAUSTED (%s)\n",
             s.error_message().c_str());
    }
  }

  // --- 3. Submission for org-b: ACCEPTED (own quota) -----------------------
  {
    SubmitRunResponse resp;
    grpc::Status s = submit("m36-run-b1", "org-b", &resp);
    if (!s.ok() || !resp.accepted()) {
      fail("org-b run should be accepted (separate per-org quota)");
    } else {
      printf("PASS: org-b run accepted (separate quota)\n");
    }
  }

  // --- 4. Health detail carries the quota snapshot -------------------------
  {
    grpc::ClientContext ctx;
    HealthRequest req;
    HealthResponse resp;
    grpc::Status s = stub->Health(&ctx, req, &resp);
    if (!s.ok() || !resp.ok()) {
      fail("Health RPC failed");
    } else if (resp.detail().find("\"quota\"") == std::string::npos ||
               resp.detail().find("rejected_runs") == std::string::npos) {
      fail("Health detail must carry the quota gate snapshot");
    } else {
      printf("PASS: Health detail carries quota snapshot\n");
    }
  }

  // --- 5. After org-a run #1 is terminal, a new org-a submit is accepted ---
  {
    bool terminal = false;
    for (int i = 0; i < 150; ++i) {
      grpc::ClientContext gctx;
      GetRunRequest greq;
      greq.set_run_id("m36-run-a1");
      GetRunResponse gr;
      if (stub->GetRun(&gctx, greq, &gr).ok() && is_terminal(gr.status())) {
        terminal = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!terminal) fail("org-a run #1 did not reach terminal state");

    // The slot is released by the runner thread right after it publishes
    // done=true (which GetRun reports), so retry briefly to ride that race.
    bool accepted = false;
    for (int i = 0; i < 50 && !accepted; ++i) {
      SubmitRunResponse resp;
      grpc::Status s = submit("m36-run-a3", "org-a", &resp);
      accepted = s.ok() && resp.accepted();
      if (!accepted) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!accepted) {
      fail("org-a run #3 must be accepted after #1 released its slot");
    } else {
      printf("PASS: org-a run #3 accepted after slot release\n");
    }
  }

  // Terminate the server subprocess and reap it.
  kill(pid, SIGTERM);
  int status = 0;
  for (int i = 0; i < 60 && !WIFEXITED(status) && !WIFSIGNALED(status); ++i) {
    pid_t r = waitpid(pid, &status, WNOHANG);
    if (r == 0) std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  if (!WIFEXITED(status) && !WIFSIGNALED(status)) kill(pid, SIGKILL);
  if (failures == 0) printf("ALL M36 ADMISSION TESTS PASSED\n");
  return failures == 0 ? 0 : 1;
}
