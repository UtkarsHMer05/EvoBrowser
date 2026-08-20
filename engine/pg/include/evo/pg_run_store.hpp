#pragma once

// Postgres RunStore (Milestone 26) over libpq.
//
// Persists the engine-neutral Phase-2 run state (M19 schema: workflow_runs,
// node_runs, task_attempts, workflows) with PARAMETERIZED SQL only — every
// value crosses as a bind parameter (PQexecParams); no string interpolation
// of user-controlled data ever reaches a query. The store never creates or
// alters tables: the committed Drizzle migrations own the DDL
// (scripts/phase2/migrate-local.sh applies them to the local stack).
//
// Invariants are enforced by the schema's unique constraints plus
// conditional statements:
//   - create_run / create_node_run / record_attempt use
//     INSERT ... ON CONFLICT DO NOTHING (idempotent; duplicate delivery
//     creates no second row),
//   - complete_node_run / set_node_status UPDATE ... WHERE status NOT IN
//     (terminal set) and report PQcmdTuples, so at most one terminal
//     completion applies per node run (M26 step 4),
//   - finish_run is a plain terminal UPDATE keyed by run id.
//
// Timestamps: wall-clock UTC milliseconds since the Unix epoch are converted
// with `to_timestamp(ms/1000.0) AT TIME ZONE 'UTC'` (timestamp without tz,
// matching the Drizzle columns). 0 => NULL.
//
// Connection handling mirrors RedisTransport: one PGconn guarded by a mutex,
// bounded reconnect/backoff per operation (base 50ms, cap 2s, max 5). A
// failed operation returns false; the caller applies its own policy.

#include <chrono>
#include <mutex>
#include <string>
#include <vector>

#include "evo/run_store.hpp"

struct pg_conn;    // libpq forward declaration (PGconn)
struct pg_result;  // libpq forward declaration (PGresult)

namespace evo {

struct PgRunStoreConfig {
  std::string host = "127.0.0.1";
  int port = 5433;  // Phase-2 local default (scripts/phase2)
  std::string user = "evo";
  std::string password = "evo_dev_password";  // local dev default; NOT a secret
  std::string dbname = "evo_phase2";
  std::chrono::milliseconds connect_timeout{3000};
  int max_retries = 5;
  std::chrono::milliseconds backoff_base{50};
  std::chrono::milliseconds backoff_cap{2000};
};

class PgRunStore final : public RunStore {
 public:
  explicit PgRunStore(PgRunStoreConfig config = {});
  ~PgRunStore() override;

  PgRunStore(const PgRunStore&) = delete;
  PgRunStore& operator=(const PgRunStore&) = delete;

  // Attempt the initial connection. Returns true when connected. Operations
  // also reconnect lazily, so this is optional but lets callers fail fast.
  bool connect();
  bool connected() const;

  bool ensure_workflow(const std::string& workflow_id,
                       const std::string& org_id,
                       const std::string& name) override;

  bool create_run(const RunRecord& run, std::int64_t started_wall_ms) override;

  bool create_node_run(const std::string& run_id, const std::string& node_id,
                       const std::string& node_type) override;

  bool set_node_status(const std::string& run_id, const std::string& node_id,
                       const std::string& status) override;

  bool set_node_retry_wait(const std::string& run_id,
                           const std::string& node_id,
                           std::int64_t retry_wait_until_wall_ms,
                           const std::string& retry_reason) override;

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
  // Execute a parameterized statement with bounded reconnect/backoff.
  // Returns the PGresult (caller PQclears) or nullptr on hard failure.
  // `want_rows` > 0 asserts PQcmdTuples == want_rows when the statement
  // succeeds (used for at-most-once UPDATEs and INSERT ... DO NOTHING).
  struct ExecResult {
    bool ok = false;
    long long rows_affected = 0;  // PQcmdTuples when ok
    struct pg_result* res = nullptr;
  };
  ExecResult exec_params(const std::string& sql,
                         const std::vector<std::string>& params);

  bool ensure_connected_locked();
  void disconnect_locked();

  PgRunStoreConfig config_;
  mutable std::mutex mu_;
  pg_conn* conn_ = nullptr;  // owned; guarded by mu_
};

}  // namespace evo
