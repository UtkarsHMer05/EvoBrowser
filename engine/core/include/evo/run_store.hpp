#pragma once

// Durable engine-neutral run state store (Milestone 26).
//
// The distributed run loop persists logical run/node/attempt state through
// this interface BEFORE unlocking successor dependency counters (M26 step 5:
// "unlock only after durable logical success"). Two implementations:
//
//   InMemoryRunStore — scheduler-core tests; no database required.
//   PgRunStore       — engine/pg/, libpq parameterized SQL against the Phase-2
//                      Postgres schema (M19 migrations own the DDL; the store
//                      never creates or alters tables).
//
// Invariants every implementation must enforce (mirrors the M19 schema):
//   - unique (run_id, node_id) node runs; creation is idempotent,
//   - unique (node_run, attempt_number) attempts; a duplicate delivery does
//     not create a second attempt row,
//   - at most ONE terminal completion per node run: once a node run is
//     terminal (succeeded/failed/dead_lettered/canceled), further
//     completions are rejected (complete_node_run returns false), so a
//     duplicate successful result can never be applied twice.
//
// Timestamps crossing this boundary are wall-clock UTC, passed as
// milliseconds since the Unix epoch. A value of 0 means "unknown" and is
// stored as NULL.

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>

namespace evo {

// Wall-clock UTC milliseconds since the Unix epoch. All timestamps crossing
// the RunStore boundary use this (ARCHITECTURE.md §7: durable timestamps are
// wall-clock UTC, recorded by the writer).
std::int64_t now_wall_ms();

// Persisted status strings. They match the Drizzle schema defaults
// (lib/db/schema.ts) so C++-written rows and TS/app-written rows are
// interchangeable in one audit table.
namespace run_status {
inline constexpr const char* kQueued = "queued";
inline constexpr const char* kRunning = "running";
inline constexpr const char* kSucceeded = "succeeded";
inline constexpr const char* kFailed = "failed";
inline constexpr const char* kCanceled = "canceled";
}  // namespace run_status

namespace node_status {
inline constexpr const char* kBlocked = "blocked";
inline constexpr const char* kReady = "ready";
inline constexpr const char* kDispatched = "dispatched";
inline constexpr const char* kRunning = "running";
inline constexpr const char* kSucceeded = "succeeded";
inline constexpr const char* kFailed = "failed";
inline constexpr const char* kCanceled = "canceled";
inline constexpr const char* kDeadLettered = "dead_lettered";

// Terminal statuses: once persisted, a node run never changes again.
bool is_terminal(const std::string& status);
}  // namespace node_status

struct RunRecord {
  std::string run_id;
  std::string org_id;
  std::string workflow_id;
  std::string workflow_version_id;  // empty => NULL
  std::string engine = "evo";
  std::string status = run_status::kQueued;
  std::string outcome;  // empty => NULL
};

struct NodeRunRecord {
  std::string node_id;
  std::string node_type;
  std::string status = node_status::kBlocked;
  int attempt_count = 0;
  std::string output_json;    // empty => NULL
  std::string failure_reason;  // empty => NULL
};

class RunStore {
 public:
  virtual ~RunStore() = default;

  // Ensure the parent workflow row exists (FK integrity for workflow_runs).
  // In production the app creates workflows; this exists so the engine can
  // run self-contained local integration tests against a bare schema.
  virtual bool ensure_workflow(const std::string& workflow_id,
                               const std::string& org_id,
                               const std::string& name) = 0;

  // Insert the run row (idempotent on run_id). Status should be "running".
  virtual bool create_run(const RunRecord& run,
                          std::int64_t started_wall_ms) = 0;

  // Insert a node run row (idempotent on (run_id, node_id)).
  virtual bool create_node_run(const std::string& run_id,
                               const std::string& node_id,
                               const std::string& node_type) = 0;

  // Non-terminal status transition (e.g. blocked -> dispatched). Never
  // demotes a terminal status.
  virtual bool set_node_status(const std::string& run_id,
                               const std::string& node_id,
                               const std::string& status) = 0;

  // Record an attempt. Idempotent per (node_run, attempt_number): returns
  // true only when this call created the attempt row (a duplicate delivery
  // returns false and does not bump the node's attempt_count).
  virtual bool record_attempt(const std::string& run_id,
                              const std::string& node_id,
                              unsigned attempt_number,
                              const std::string& worker_id,
                              std::int64_t started_wall_ms) = 0;

  // Terminal attempt transition (from the result envelope): worker that ran
  // it, terminal status ("succeeded"/"failed"), error, finish time. Returns
  // false when the attempt row does not exist.
  virtual bool finish_attempt(const std::string& run_id,
                              const std::string& node_id,
                              unsigned attempt_number,
                              const std::string& worker_id,
                              const std::string& status,
                              const std::string& error,
                              std::int64_t finished_wall_ms) = 0;

  // Terminal completion. Returns true only when applied — i.e. the node run
  // was not already terminal (at-most-once success/failure). output_json is
  // ignored for failures; failure_reason is ignored for successes.
  virtual bool complete_node_run(const std::string& run_id,
                                 const std::string& node_id,
                                 const std::string& status,
                                 const std::string& output_json,
                                 const std::string& failure_reason,
                                 std::int64_t finished_wall_ms) = 0;

  // Terminal run transition + outcome.
  virtual bool finish_run(const std::string& run_id, const std::string& status,
                          const std::string& outcome,
                          std::int64_t finished_wall_ms) = 0;

  // --- Audit readers (tests / observability) ---
  virtual std::optional<RunRecord> get_run(const std::string& run_id) = 0;
  virtual std::optional<NodeRunRecord> get_node_run(
      const std::string& run_id, const std::string& node_id) = 0;
  virtual std::size_t attempt_row_count(const std::string& run_id,
                                        const std::string& node_id) = 0;
  // Worker ids recorded on the node's attempt rows (ordered by attempt).
  virtual std::vector<std::string> attempt_worker_ids(
      const std::string& run_id, const std::string& node_id) = 0;
};

// In-memory RunStore for scheduler-core tests. Enforces the same invariants
// as PgRunStore (unique node runs, unique attempts, at-most-once terminal
// completion). Thread-safe.
class InMemoryRunStore final : public RunStore {
 public:
  bool ensure_workflow(const std::string& workflow_id,
                       const std::string& org_id,
                       const std::string& name) override;

  bool create_run(const RunRecord& run, std::int64_t started_wall_ms) override;

  bool create_node_run(const std::string& run_id, const std::string& node_id,
                       const std::string& node_type) override;

  bool set_node_status(const std::string& run_id, const std::string& node_id,
                       const std::string& status) override;

  bool record_attempt(const std::string& run_id, const std::string& node_id,
                      unsigned attempt_number, const std::string& worker_id,
                      std::int64_t started_wall_ms) override;

  bool finish_attempt(const std::string& run_id, const std::string& node_id,
                      unsigned attempt_number, const std::string& worker_id,
                      const std::string& status, const std::string& error,
                      std::int64_t finished_wall_ms) override;

  bool complete_node_run(const std::string& run_id, const std::string& node_id,
                         const std::string& status,
                         const std::string& output_json,
                         const std::string& failure_reason,
                         std::int64_t finished_wall_ms) override;

  bool finish_run(const std::string& run_id, const std::string& status,
                  const std::string& outcome,
                  std::int64_t finished_wall_ms) override;

  std::optional<RunRecord> get_run(const std::string& run_id) override;
  std::optional<NodeRunRecord> get_node_run(
      const std::string& run_id, const std::string& node_id) override;
  std::size_t attempt_row_count(const std::string& run_id,
                                const std::string& node_id) override;
  std::vector<std::string> attempt_worker_ids(
      const std::string& run_id, const std::string& node_id) override;

 private:
  using NodeKey = std::pair<std::string, std::string>;  // (run_id, node_id)

  mutable std::mutex mu_;
  std::set<std::string> workflows_;
  std::map<std::string, RunRecord> runs_;
  std::map<NodeKey, NodeRunRecord> node_runs_;
  // (run_id, node_id) -> attempt numbers that have an attempt row
  std::map<NodeKey, std::set<unsigned>> attempts_;
  // (run_id, node_id) -> attempt_number -> worker id (from record/finish)
  std::map<NodeKey, std::map<unsigned, std::string>> attempt_workers_;
  // (run_id, node_id) -> attempt_number -> terminal status (once finished)
  std::map<NodeKey, std::map<unsigned, std::string>> attempt_status_;
};

}  // namespace evo
