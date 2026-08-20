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
#include <tuple>
#include <utility>
#include <vector>

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

// Attempt-row status vocabulary (task_attempts.status, Milestone 31).
// NOTE: an attempt's terminal status is not the same axis as the NODE's
// status: `lease_expired` is terminal for the ATTEMPT but the logical node
// is recoverable (re-dispatched as a new attempt), per M31 step 5.
namespace attempt_status {
inline constexpr const char* kRunning = "running";
inline constexpr const char* kSucceeded = "succeeded";
inline constexpr const char* kFailed = "failed";
inline constexpr const char* kLeaseExpired = "lease_expired";
}  // namespace attempt_status

// Worker registry row (Milestone 31): liveness evidence for one worker
// process. Heartbeat cadence/expiry are defined separately from the per-task
// lease duration: a heartbeat proves the PROCESS is alive; a lease proves a
// specific ATTEMPT is being worked.
struct WorkerRecord {
  std::string worker_id;
  std::string env_prefix;
  std::string status = "alive";  // alive | lost
  std::int64_t last_heartbeat_ms = 0;  // wall-clock UTC ms; 0 => never
};

// One attempt's lease evidence (Milestone 31 step 6).
struct AttemptLeaseRecord {
  std::string node_id;
  unsigned attempt_number = 0;
  std::string worker_id;
  std::string status;  // attempt_status::*
  std::int64_t acquired_ms = 0;  // wall-clock UTC ms; 0 => NULL
  std::int64_t renewed_ms = 0;
  std::int64_t expires_ms = 0;
  std::int64_t expired_ms = 0;  // set when the scheduler reaped the lease
};

struct RunRecord {
  std::string run_id;
  std::string org_id;
  std::string workflow_id;
  std::string workflow_version_id;  // empty => NULL
  std::string engine = "evo";
  std::string status = run_status::kQueued;
  std::string outcome;  // empty => NULL
  // Milestone 30: wall-clock UTC ms of the FIRST cancellation request
  // (0 => none/NULL) and its reason (empty => NULL). Written once,
  // idempotently, by mark_cancel_requested.
  std::int64_t cancel_requested_at = 0;
  std::string cancel_reason;
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

  // Record the FIRST cancellation request for a run (Milestone 30). Idempotent:
  // only the first request stamps cancel_requested_at (and cancel_reason); a
  // repeated request returns true but does not overwrite the original
  // timestamp. Returns false only when the run row does not exist. The
  // timestamp is wall-clock UTC milliseconds; 0 => unknown (stored as NULL).
  virtual bool mark_cancel_requested(const std::string& run_id,
                                     const std::string& reason,
                                     std::int64_t requested_wall_ms) = 0;

  // --- Worker registry + task leases (Milestone 31) ---

  // Upsert a worker's liveness row (idempotent on worker_id). Called on
  // registration and on every heartbeat. `now_wall_ms` stamps
  // last_heartbeat_at. Returns false only on a hard store failure.
  virtual bool worker_heartbeat(const std::string& worker_id,
                                const std::string& env_prefix,
                                std::int64_t now_wall_ms) = 0;

  // Initialize the lease for a freshly dispatched attempt (scheduler side):
  // status running, no worker yet, lease_expires_at = the dispatch-time lease
  // deadline. Covers the queue-wait: if no worker claims and acquires the
  // lease before it expires, the scan reaps the attempt and the node is
  // re-dispatched. Idempotent: only stamps when no lease exists yet.
  virtual bool init_attempt_lease(const std::string& run_id,
                                  const std::string& node_id,
                                  unsigned attempt_number,
                                  std::int64_t expires_wall_ms) = 0;

  // Acquire the lease for an attempt: stamp worker_id + lease_acquired_at +
  // lease_renewed_at + lease_expires_at. Idempotent for the SAME worker (a
  // re-acquire refreshes the expiry); a different worker cannot steal an
  // unexpired lease (returns false). All timestamps wall-clock UTC ms.
  virtual bool acquire_attempt_lease(const std::string& run_id,
                                     const std::string& node_id,
                                     unsigned attempt_number,
                                     const std::string& worker_id,
                                     std::int64_t acquired_wall_ms,
                                     std::int64_t expires_wall_ms) = 0;

  // Renew an attempt's lease while work legitimately runs. Only the lease
  // holder (matching worker_id) may renew; the attempt must not be terminal.
  // Updates lease_renewed_at + lease_expires_at.
  virtual bool renew_attempt_lease(const std::string& run_id,
                                   const std::string& node_id,
                                   unsigned attempt_number,
                                   const std::string& worker_id,
                                   std::int64_t renewed_wall_ms,
                                   std::int64_t expires_wall_ms) = 0;

  // Find attempts whose lease has expired (expires_at <= now, still running,
  // not yet reaped). Returns the evidence rows; the caller decides the
  // transition. `now_wall_ms` is the scan instant (wall-clock UTC ms).
  virtual std::vector<AttemptLeaseRecord> scan_expired_attempt_leases(
      const std::string& run_id, std::int64_t now_wall_ms) = 0;

  // Transition an attempt to lease_expired (terminal for the attempt) and
  // stamp lease_expired_at. Only applies when the attempt is still running
  // with an unexpired-or-expired lease held by `worker_id` and not already
  // terminal — so a racing completion can never be double-completed (M31
  // no-go: lease expiry must not double-complete the logical node). Returns
  // true only when applied.
  virtual bool mark_attempt_lease_expired(const std::string& run_id,
                                          const std::string& node_id,
                                          unsigned attempt_number,
                                          const std::string& worker_id,
                                          std::int64_t expired_wall_ms) = 0;

  // Read one attempt's lease evidence (audit/tests). nullopt when the
  // attempt row does not exist.
  virtual std::optional<AttemptLeaseRecord> get_attempt_lease(
      const std::string& run_id, const std::string& node_id,
      unsigned attempt_number) = 0;

  // Read a worker's registry row (audit/tests). nullopt when unknown.
  virtual std::optional<WorkerRecord> get_worker(
      const std::string& worker_id) = 0;

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

  bool mark_cancel_requested(const std::string& run_id,
                             const std::string& reason,
                             std::int64_t requested_wall_ms) override;

  bool worker_heartbeat(const std::string& worker_id,
                        const std::string& env_prefix,
                        std::int64_t now_wall_ms) override;

  bool init_attempt_lease(const std::string& run_id, const std::string& node_id,
                          unsigned attempt_number,
                          std::int64_t expires_wall_ms) override;

  bool acquire_attempt_lease(const std::string& run_id,
                             const std::string& node_id,
                             unsigned attempt_number,
                             const std::string& worker_id,
                             std::int64_t acquired_wall_ms,
                             std::int64_t expires_wall_ms) override;

  bool renew_attempt_lease(const std::string& run_id, const std::string& node_id,
                           unsigned attempt_number,
                           const std::string& worker_id,
                           std::int64_t renewed_wall_ms,
                           std::int64_t expires_wall_ms) override;

  std::vector<AttemptLeaseRecord> scan_expired_attempt_leases(
      const std::string& run_id, std::int64_t now_wall_ms) override;

  bool mark_attempt_lease_expired(const std::string& run_id,
                                  const std::string& node_id,
                                  unsigned attempt_number,
                                  const std::string& worker_id,
                                  std::int64_t expired_wall_ms) override;

  std::optional<AttemptLeaseRecord> get_attempt_lease(
      const std::string& run_id, const std::string& node_id,
      unsigned attempt_number) override;

  std::optional<WorkerRecord> get_worker(const std::string& worker_id) override;

  std::optional<RunRecord> get_run(const std::string& run_id) override;
  std::optional<NodeRunRecord> get_node_run(
      const std::string& run_id, const std::string& node_id) override;
  std::size_t attempt_row_count(const std::string& run_id,
                                const std::string& node_id) override;
  std::vector<std::string> attempt_worker_ids(
      const std::string& run_id, const std::string& node_id) override;

 private:
  using NodeKey = std::pair<std::string, std::string>;  // (run_id, node_id)
  // (run_id, node_id, attempt_number) — attempt lease identity (M31).
  using AttemptKey = std::tuple<std::string, std::string, unsigned>;

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
  // Milestone 31: attempt lease evidence + worker registry.
  std::map<AttemptKey, AttemptLeaseRecord> attempt_leases_;
  std::map<std::string, WorkerRecord> workers_;
};

}  // namespace evo
