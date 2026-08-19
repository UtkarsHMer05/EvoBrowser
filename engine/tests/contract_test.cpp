// Milestone 16: golden round-trip tests for the generated protobuf contract
// (engine/proto/evo/execution.proto). Verifies that messages serialize and
// deserialize back to an equal message (wire-format stability), that the
// service + RPC shape is present, and that wall-clock UTC timestamps (per
// ARCHITECTURE.md §7 and M16 rule 16) round-trip through google.protobuf.Timestamp.

#include <chrono>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>

#include "evo/execution.pb.h"

#include <iostream>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
  std::cout << (ok ? "  ok   " : "  FAIL ") << what << '\n';
  if (!ok) ++failures;
}

// Field numbers are the versioning axis (M16); assert they match the schema.
void test_field_numbers_stable() {
  std::cout << "golden: field numbers stable\n";
  const auto* d = google::protobuf::DescriptorPool::generated_pool()
                      ->FindMessageTypeByName("evo.execution.v1.SubmitRunRequest");
  check(d != nullptr, "SubmitRunRequest descriptor present");
  if (d) {
    check(d->field(0)->number() == 1 && d->field(0)->name() == "org_id",
          "SubmitRunRequest field 1 == org_id");
    check(d->field(2)->number() == 3 && d->field(2)->name() == "run_id",
          "SubmitRunRequest field 3 == run_id");
    check(d->field(5)->number() == 6 && d->field(5)->name() == "requested_at",
          "SubmitRunRequest field 6 == requested_at");
    check(d->field(6)->number() == 7 && d->field(6)->name() == "trace_id",
          "SubmitRunRequest field 7 == trace_id");
  }
  const auto* te = google::protobuf::DescriptorPool::generated_pool()
                       ->FindMessageTypeByName("evo.execution.v1.TaskEnvelope");
  check(te != nullptr, "TaskEnvelope descriptor present");
  if (te) {
    check(te->field(0)->name() == "run_id" && te->field(0)->number() == 1,
          "TaskEnvelope field 1 == run_id");
    check(te->field(7)->name() == "trace_id" && te->field(7)->number() == 8,
          "TaskEnvelope field 8 == trace_id");
    check(te->field(10)->name() == "node_payload_json" && te->field(10)->number() == 11,
          "TaskEnvelope field 11 == node_payload_json (no secrets in payload)");
    check(te->field(11)->name() == "became_ready_at" && te->field(11)->number() == 12,
          "TaskEnvelope field 12 == became_ready_at (wall-clock)");
  }
}

void test_submitrun_roundtrip() {
  std::cout << "golden: SubmitRun/Response round-trip\n";
  evo::execution::v1::SubmitRunRequest req;
  req.set_org_id("org_abc");
  req.set_workflow_version_id("wf_1");
  req.set_run_id("run_42");
  req.set_dag_json(R"({"nodes":[],"edges":[]})");
  req.set_trace_id("trace-1");
  auto* rts = req.mutable_requested_at();
  rts->set_seconds(1700000000);
  rts->set_nanos(123456789);

  std::string wire;
  check(req.SerializeToString(&wire), "serialize to wire");
  check(!wire.empty(), "wire is non-empty");

  evo::execution::v1::SubmitRunRequest back;
  check(back.ParseFromString(wire), "parse back from wire");
  check(back.org_id() == req.org_id(), "org_id round-trips");
  check(back.run_id() == req.run_id(), "run_id round-trips");
  check(back.dag_json() == req.dag_json(), "dag_json round-trips");
  check(back.trace_id() == req.trace_id(), "trace_id round-trips");
  // Timestamps are wall-clock UTC (proto Timestamp); equality preserved.
  check(back.requested_at().seconds() == req.requested_at().seconds() &&
            back.requested_at().nanos() == req.requested_at().nanos(),
        "requested_at wall-clock timestamp round-trips");

  // Response
  evo::execution::v1::SubmitRunResponse resp;
  resp.set_run_id("run_42");
  resp.set_accepted(true);
  resp.set_message("ok");
  std::string rwire;
  resp.SerializeToString(&rwire);
  evo::execution::v1::SubmitRunResponse rback;
  rback.ParseFromString(rwire);
  check(rback.accepted(), "response accepted flag round-trips");
  check(rback.run_id() == "run_42", "response run_id round-trips");
}

void test_envelope_roundtrip() {
  std::cout << "golden: TaskEnvelope/ResultEnvelope round-trip\n";
  evo::execution::v1::TaskEnvelope te;
  te.set_run_id("run_X");
  te.set_workflow_version_id("wf_2");
  te.set_org_id("org_Z");
  te.set_node_id("node_1");
  te.set_attempt_number(1);
  te.set_resource_class(evo::execution::v1::BROWSER);
  te.set_affinity_key("run:run_X");
  te.set_trace_id("trace-Y");
  te.set_span_id("span-1");
  te.set_node_type("act");
  te.set_node_payload_json(R"({"selector":"#btn"})");
  // Wall-clock UTC timestamp (proto google.protobuf.Timestamp).
  auto* ts = te.mutable_became_ready_at();
  ts->set_seconds(1234567);
  ts->set_nanos(890);

  std::string wire;
  te.SerializeToString(&wire);
  evo::execution::v1::TaskEnvelope back;
  back.ParseFromString(wire);
  check(back.run_id() == "run_X", "run_id round-trips");
  check(back.resource_class() == evo::execution::v1::BROWSER,
        "resource_class BROWSER round-trips");
  check(back.affinity_key() == "run:run_X", "affinity_key round-trips");
  check(back.attempt_number() == 1, "attempt_number round-trips");
  check(back.node_payload_json() == R"({"selector":"#btn"})",
        "node_payload_json round-trips (no secrets embedded)");
  check(back.became_ready_at().seconds() == 1234567 &&
            back.became_ready_at().nanos() == 890,
        "became_ready_at wall-clock timestamp round-trips");

  evo::execution::v1::ResultEnvelope re;
  re.set_run_id("run_X");
  re.set_node_id("node_1");
  re.set_attempt_number(1);
  re.set_trace_id("trace-Y");
  re.set_completed(true);
  re.set_output("clicked");
  re.set_status(evo::execution::v1::ResultEnvelope::OK);
  std::string rw;
  re.SerializeToString(&rw);
  evo::execution::v1::ResultEnvelope rback;
  rback.ParseFromString(rw);
  check(rback.completed(), "completed round-trips");
  check(rback.status() == evo::execution::v1::ResultEnvelope::OK,
        "status OK round-trips");
  check(rback.abandoned() == false, "abandoned defaults false");
}

void test_service_and_enums() {
  std::cout << "contract: service + enum surface\n";
  const auto* svc = google::protobuf::DescriptorPool::generated_pool()
                       ->FindServiceByName("evo.execution.v1.ControlService");
  check(svc != nullptr, "ControlService present");
  if (svc) {
    check(svc->method_count() == 4, "ControlService has 4 RPCs (Submit/Cancel/Get/Health)");
    check(svc->method(0)->name() == "SubmitRun", "SubmitRun RPC present");
    check(svc->method(1)->name() == "CancelRun", "CancelRun RPC present");
    check(svc->method(2)->name() == "GetRun", "GetRun RPC present");
    check(svc->method(3)->name() == "Health", "Health RPC present");
  }
  // Enum coverage.
  evo::execution::v1::NodeStatus ns;
  ns.set_state(evo::execution::v1::NODE_STATE_SUCCEEDED);
  check(ns.state() == evo::execution::v1::NODE_STATE_SUCCEEDED,
        "NodeState SUCCEEDED round-trips");
  ns.set_resource_class(evo::execution::v1::EXTERNAL_IO);
  check(ns.resource_class() == evo::execution::v1::EXTERNAL_IO,
        "ResourceClass EXTERNAL_IO round-trips");
  evo::execution::v1::GetRunResponse gr;
  gr.set_status(evo::execution::v1::RUN_SUCCEEDED);
  check(gr.status() == evo::execution::v1::RUN_SUCCEEDED,
        "RunStatus RUN_SUCCEEDED round-trips");
  gr.set_outcome(evo::execution::v1::SUCCEEDED);
  check(gr.outcome() == evo::execution::v1::SUCCEEDED,
        "RunOutcome SUCCEEDED round-trips");
}

}  // namespace

int main() {
  // protobuf::util not linked unless we use it — keep dependency honest.
  test_field_numbers_stable();
  test_submitrun_roundtrip();
  test_envelope_roundtrip();
  test_service_and_enums();
  google::protobuf::ShutdownProtobufLibrary();

  if (failures != 0) {
    std::cout << failures << " contract check(s) failed\n";
    return 1;
  }
  std::cout << "all contract tests passed\n";
  return 0;
}
