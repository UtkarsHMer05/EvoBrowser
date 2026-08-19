// M22 cross-language fixture generator.
//
// Writes deterministic golden TaskEnvelope/ResultEnvelope byte fixtures to
// engine/tests/fixtures/ (committed). The TypeScript cross-language test
// decodes these with protobufjs and re-encodes them, asserting byte-identical
// output — proving C++ <-> TS encoder/decoder compatibility for the M22
// envelope fields.
//
// Determinism: TaskEnvelope/ResultEnvelope have no map fields, and this
// generator sets fields in field-number order, so SerializeAsString output is
// byte-stable across runs and platforms.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "evo/execution.pb.h"

using evo::execution::v1::ResultEnvelope;
using evo::execution::v1::TaskEnvelope;

namespace {

void set_wall(google::protobuf::Timestamp* ts, long long seconds, int nanos) {
  ts->set_seconds(seconds);
  ts->set_nanos(nanos);
}

int write_file(const std::filesystem::path& path, const std::string& bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    fprintf(stderr, "FAIL: cannot write %s\n", path.string().c_str());
    return 1;
  }
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  std::filesystem::path dir = "tests/fixtures";
  if (argc > 1) dir = argv[1];
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);

  // --- TaskEnvelope fixture -------------------------------------------------
  TaskEnvelope task;
  task.set_run_id("run-fixture-001");
  task.set_workflow_version_id("wfv-fixture-001");
  task.set_org_id("org_fixture");
  task.set_node_id("n0");
  task.set_attempt_number(1);
  task.set_resource_class(evo::execution::v1::BROWSER);
  task.set_affinity_key("run:run-fixture-001");
  task.set_trace_id("trace-fixture");
  task.set_span_id("span-fixture");
  task.set_node_type("act");
  task.set_node_payload_json("{\"instruction\":\"Click the sign in button\"}");
  set_wall(task.mutable_became_ready_at(), 1787100000LL, 123456789);

  const std::string task_bytes = task.SerializeAsString();
  // Byte-stability self-check: re-encode and compare.
  TaskEnvelope task2;
  if (!task2.ParseFromString(task_bytes)) {
    fprintf(stderr, "FAIL: TaskEnvelope fixture did not re-parse\n");
    return 1;
  }
  if (task2.SerializeAsString() != task_bytes) {
    fprintf(stderr, "FAIL: TaskEnvelope encoding not byte-stable\n");
    return 1;
  }

  // --- ResultEnvelope fixture (failure with M22 fields) ---------------------
  ResultEnvelope result;
  result.set_run_id("run-fixture-001");
  result.set_node_id("n0");
  result.set_attempt_number(2);
  result.set_trace_id("trace-fixture");
  result.set_completed(false);
  result.set_output("{\"partial\":true}");
  result.set_error("upstream 503");
  result.set_status(ResultEnvelope::NODE_FAILED);
  set_wall(result.mutable_finished_at(), 1787100060LL, 987654321);
  result.set_abandoned(false);
  result.set_error_class(evo::execution::v1::ERROR_TRANSIENT);
  result.set_retryable(true);
  result.set_worker_id("worker-fixture-7");
  set_wall(result.mutable_started_at(), 1787100030LL, 111222333);

  const std::string result_bytes = result.SerializeAsString();
  ResultEnvelope result2;
  if (!result2.ParseFromString(result_bytes)) {
    fprintf(stderr, "FAIL: ResultEnvelope fixture did not re-parse\n");
    return 1;
  }
  if (result2.SerializeAsString() != result_bytes) {
    fprintf(stderr, "FAIL: ResultEnvelope encoding not byte-stable\n");
    return 1;
  }

  if (write_file(dir / "task_envelope.bin", task_bytes) != 0) return 1;
  if (write_file(dir / "result_envelope.bin", result_bytes) != 0) return 1;

  printf("wrote %s (%zu bytes) and %s (%zu bytes)\n",
         (dir / "task_envelope.bin").string().c_str(), task_bytes.size(),
         (dir / "result_envelope.bin").string().c_str(), result_bytes.size());
  printf("ENVELOPE FIXTURES GENERATED (byte-stable)\n");
  return 0;
}
