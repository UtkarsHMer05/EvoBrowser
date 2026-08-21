#include "evo/pg_run_store.hpp"

#include <libpq-fe.h>

#include <cstdlib>
#include <thread>
#include <vector>

namespace evo {

namespace {

// Terminal node-run statuses (mirrors node_status::is_terminal). Used in
// WHERE clauses so a terminal row can never be overwritten.
constexpr const char* kTerminalNotIn =
    "status NOT IN ('succeeded','failed','canceled','dead_lettered')";

std::string conninfo_of(const PgRunStoreConfig& c) {
  return "host=" + c.host + " port=" + std::to_string(c.port) +
         " user=" + c.user + " password=" + c.password +
         " dbname=" + c.dbname + " connect_timeout=" +
         std::to_string(
             std::chrono::duration_cast<std::chrono::seconds>(c.connect_timeout)
                 .count() +
             1);
}

}  // namespace

PgRunStore::PgRunStore(PgRunStoreConfig config)
    : config_(std::move(config)) {}

PgRunStore::~PgRunStore() {
  std::lock_guard lock(mu_);
  disconnect_locked();
}

void PgRunStore::disconnect_locked() {
  if (conn_) {
    PQfinish(conn_);
    conn_ = nullptr;
  }
}

bool PgRunStore::ensure_connected_locked() {
  if (conn_ && PQstatus(conn_) == CONNECTION_OK) return true;
  disconnect_locked();
  conn_ = PQconnectdb(conninfo_of(config_).c_str());
  if (PQstatus(conn_) != CONNECTION_OK) {
    disconnect_locked();
    return false;
  }
  return true;
}

bool PgRunStore::connect() {
  std::lock_guard lock(mu_);
  return ensure_connected_locked();
}

bool PgRunStore::connected() const {
  std::lock_guard lock(mu_);
  return conn_ && PQstatus(conn_) == CONNECTION_OK;
}

PgRunStore::ExecResult PgRunStore::exec_params(
    const std::string& sql, const std::vector<std::string>& params) {
  std::lock_guard lock(mu_);

  std::vector<const char*> values;
  values.reserve(params.size());
  for (const auto& p : params) values.push_back(p.c_str());

  auto backoff = config_.backoff_base;
  for (int attempt = 0; attempt <= config_.max_retries; ++attempt) {
    if (!ensure_connected_locked()) {
      // Connection failure: back off and retry.
    } else {
      PGresult* res = PQexecParams(
          conn_, sql.c_str(), static_cast<int>(values.size()), nullptr,
          values.empty() ? nullptr : values.data(), nullptr, nullptr, 0);
      const ExecStatusType st = PQresultStatus(res);
      if (st == PGRES_COMMAND_OK || st == PGRES_TUPLES_OK) {
        ExecResult out;
        out.ok = true;
        const char* tuples = PQcmdTuples(res);
        out.rows_affected = (tuples && *tuples) ? std::atoll(tuples) : 0;
        out.res = res;
        return out;  // caller PQclears res
      }
      PQclear(res);
      // Statement error on a healthy connection is not retryable (bad SQL,
      // constraint violation surfaced as error, etc.).
      return {};
    }
    std::this_thread::sleep_for(backoff);
    backoff = std::min(backoff * 2, config_.backoff_cap);
  }
  return {};
}

bool PgRunStore::ensure_workflow(const std::string& workflow_id,
                                 const std::string& org_id,
                                 const std::string& name) {
  auto r = exec_params(
      "INSERT INTO workflows (id, org_id, name) "
      "VALUES ($1::uuid, $2, $3) ON CONFLICT (id) DO NOTHING",
      {workflow_id, org_id, name});
  if (r.res) PQclear(r.res);
  return r.ok;
}

bool PgRunStore::create_run(const RunRecord& run,
                            std::int64_t started_wall_ms) {
  // The app pre-creates the run row with status 'queued' (M27) before the
  // engine starts. When the engine begins executing it must transition that
  // row to 'running' and stamp started_at. ON CONFLICT DO UPDATE with a
  // WHERE guard achieves both: a fresh insert lands as 'running'; a
  // pre-existing 'queued' row is promoted; an already-running or terminal row
  // is left untouched (idempotent, never regresses state).
  //
  // M35: dag_json is persisted on the same statement (NULLIF => NULL for
  // legacy/empty). On conflict the update backfills dag_json only when the
  // existing row lacks it (COALESCE keeps any prior value), so a restarted
  // scheduler can always reconstruct the run's topology from durable state.
  auto r = exec_params(
      "INSERT INTO workflow_runs (id, org_id, workflow_id, "
      "workflow_version_id, engine, status, started_at, dag_json) "
      "VALUES ($1, $2, $3::uuid, NULLIF($4, '')::uuid, $5, $6, "
      "to_timestamp($7::bigint / 1000.0) AT TIME ZONE 'UTC', "
      "NULLIF($8, '')) "
      "ON CONFLICT (id) DO UPDATE SET status = EXCLUDED.status, "
      "started_at = EXCLUDED.started_at, "
      "dag_json = COALESCE(workflow_runs.dag_json, EXCLUDED.dag_json) "
      "WHERE workflow_runs.status = 'queued'",
      {run.run_id, run.org_id, run.workflow_id, run.workflow_version_id,
       run.engine, run.status, std::to_string(started_wall_ms), run.dag_json});
  if (r.res) PQclear(r.res);
  return r.ok;
}

bool PgRunStore::create_node_run(const std::string& run_id,
                                 const std::string& node_id,
                                 const std::string& node_type) {
  auto r = exec_params(
      "INSERT INTO node_runs (run_id, node_id, node_type, status) "
      "VALUES ($1, $2, $3, 'blocked') "
      "ON CONFLICT ON CONSTRAINT uq_node_runs_run_node DO NOTHING",
      {run_id, node_id, node_type});
  if (r.res) PQclear(r.res);
  return r.ok;
}

bool PgRunStore::set_node_status(const std::string& run_id,
                                 const std::string& node_id,
                                 const std::string& status) {
  auto r = exec_params(
      "UPDATE node_runs SET status = $3 "
      "WHERE run_id = $1 AND node_id = $2 AND " +
          std::string(kTerminalNotIn),
      {run_id, node_id, status});
  if (r.res) PQclear(r.res);
  return r.ok && r.rows_affected == 1;
}

bool PgRunStore::set_node_retry_wait(const std::string& run_id,
                                     const std::string& node_id,
                                     std::int64_t retry_wait_until_wall_ms,
                                     const std::string& retry_reason) {
  // Milestone 32: park the node in retry_wait with its backoff due-time +
  // reason. Never applies to a terminal node (kTerminalNotIn).
  auto r = exec_params(
      "UPDATE node_runs SET status = 'retry_wait', "
      "retry_wait_until = to_timestamp($3::bigint / 1000.0) AT TIME ZONE "
      "'UTC', "
      "retry_reason = NULLIF($4, '') "
      "WHERE run_id = $1 AND node_id = $2 AND " +
          std::string(kTerminalNotIn),
      {run_id, node_id, std::to_string(retry_wait_until_wall_ms),
       retry_reason});
  if (r.res) PQclear(r.res);
  return r.ok && r.rows_affected == 1;
}

bool PgRunStore::record_attempt(const std::string& run_id,
                                const std::string& node_id,
                                unsigned attempt_number,
                                const std::string& worker_id,
                                std::int64_t started_wall_ms) {
  // Insert the attempt row idempotently; bump the node's attempt_count only
  // when this call actually created the row (duplicate delivery => 0 rows).
  auto r = exec_params(
      "WITH ins AS ("
      "  INSERT INTO task_attempts (node_run_id, attempt_number, worker_id, "
      "status, started_at) "
      "  SELECT nr.id, $3::int, NULLIF($4, ''), 'running', "
      "to_timestamp($5::bigint / 1000.0) AT TIME ZONE 'UTC' "
      "  FROM node_runs nr WHERE nr.run_id = $1 AND nr.node_id = $2 "
      "  ON CONFLICT ON CONSTRAINT uq_task_attempts_node_attempt DO NOTHING "
      "  RETURNING node_run_id"
      ") "
      "UPDATE node_runs SET attempt_count = attempt_count + 1 "
      "WHERE id IN (SELECT node_run_id FROM ins)",
      {run_id, node_id, std::to_string(attempt_number), worker_id,
       std::to_string(started_wall_ms)});
  if (r.res) PQclear(r.res);
  return r.ok && r.rows_affected == 1;
}

bool PgRunStore::finish_attempt(const std::string& run_id,
                                const std::string& node_id,
                                unsigned attempt_number,
                                const std::string& worker_id,
                                const std::string& status,
                                const std::string& error,
                                std::int64_t finished_wall_ms) {
  auto r = exec_params(
      "UPDATE task_attempts ta SET status = $4, "
      "worker_id = COALESCE(NULLIF($5, ''), ta.worker_id), "
      "error = NULLIF($6, ''), "
      "finished_at = to_timestamp($7::bigint / 1000.0) AT TIME ZONE 'UTC' "
      "FROM node_runs nr "
      "WHERE ta.node_run_id = nr.id AND nr.run_id = $1 AND nr.node_id = $2 "
      "AND ta.attempt_number = $3::int",
      {run_id, node_id, std::to_string(attempt_number), status, worker_id,
       error, std::to_string(finished_wall_ms)});
  if (r.res) PQclear(r.res);
  return r.ok && r.rows_affected == 1;
}

bool PgRunStore::complete_node_run(const std::string& run_id,
                                   const std::string& node_id,
                                   const std::string& status,
                                   const std::string& output_json,
                                   const std::string& failure_reason,
                                   std::int64_t finished_wall_ms) {
  // At-most-once terminal completion: the WHERE excludes terminal rows, so a
  // duplicate success/failure affects 0 rows and reports not-applied.
  auto r = exec_params(
      "UPDATE node_runs SET status = $3, "
      "output = CASE WHEN $3 = 'succeeded' THEN NULLIF($4, '')::jsonb "
      "ELSE output END, "
      "failure_reason = CASE WHEN $3 <> 'succeeded' THEN NULLIF($5, '') "
      "ELSE failure_reason END, "
      "finished_at = to_timestamp($6::bigint / 1000.0) AT TIME ZONE 'UTC' "
      "WHERE run_id = $1 AND node_id = $2 AND " +
          std::string(kTerminalNotIn),
      {run_id, node_id, status, output_json, failure_reason,
       std::to_string(finished_wall_ms)});
  if (r.res) PQclear(r.res);
  return r.ok && r.rows_affected == 1;
}

bool PgRunStore::finish_run(const std::string& run_id, const std::string& status,
                            const std::string& outcome,
                            std::int64_t finished_wall_ms) {
  auto r = exec_params(
      "UPDATE workflow_runs SET status = $2, outcome = NULLIF($3, ''), "
      "finished_at = to_timestamp($4::bigint / 1000.0) AT TIME ZONE 'UTC' "
      "WHERE id = $1",
      {run_id, status, outcome, std::to_string(finished_wall_ms)});
  if (r.res) PQclear(r.res);
  return r.ok && r.rows_affected == 1;
}

bool PgRunStore::mark_cancel_requested(const std::string& run_id,
                                       const std::string& reason,
                                       std::int64_t requested_wall_ms) {
  // Milestone 30: stamp the FIRST cancellation request. The WHERE guard makes
  // repeated requests a no-op (0 rows) that still reports success when the run
  // exists — first request wins, later ones never overwrite the timestamp.
  auto r = exec_params(
      "UPDATE workflow_runs SET cancel_requested_at = "
      "CASE WHEN $3::bigint = 0 THEN NULL ELSE "
      "to_timestamp($3::bigint / 1000.0) AT TIME ZONE 'UTC' END, "
      "cancel_reason = NULLIF($2, '') "
      "WHERE id = $1 AND cancel_requested_at IS NULL",
      {run_id, reason, std::to_string(requested_wall_ms)});
  if (r.res) PQclear(r.res);
  if (!r.ok) return false;
  if (r.rows_affected == 1) return true;
  // 0 rows: either the run does not exist (=> false) or a cancellation was
  // already recorded (=> true, idempotent).
  auto exists = exec_params("SELECT 1 FROM workflow_runs WHERE id = $1",
                            {run_id});
  const bool found = exists.ok && exists.res && PQntuples(exists.res) == 1;
  if (exists.res) PQclear(exists.res);
  return found;
}

// --- Milestone 31: worker registry + task leases ----------------------------

bool PgRunStore::worker_heartbeat(const std::string& worker_id,
                                  const std::string& env_prefix,
                                  std::int64_t now_wall_ms) {
  auto r = exec_params(
      "INSERT INTO workers (worker_id, env_prefix, status, last_heartbeat_at) "
      "VALUES ($1, $2, 'alive', "
      "to_timestamp($3::bigint / 1000.0) AT TIME ZONE 'UTC') "
      "ON CONFLICT (worker_id) DO UPDATE SET env_prefix = EXCLUDED.env_prefix, "
      "status = 'alive', last_heartbeat_at = EXCLUDED.last_heartbeat_at",
      {worker_id, env_prefix, std::to_string(now_wall_ms)});
  if (r.res) PQclear(r.res);
  return r.ok;
}

bool PgRunStore::init_attempt_lease(const std::string& run_id,
                                    const std::string& node_id,
                                    unsigned attempt_number,
                                    std::int64_t expires_wall_ms) {
  // Scheduler-side lease init at dispatch: stamp the queue-wait deadline.
  // Idempotent: only stamps when no lease expiry exists yet (a worker that
  // already acquired/renewed owns the expiry).
  auto r = exec_params(
      "UPDATE task_attempts ta SET "
      "lease_expires_at = to_timestamp($4::bigint / 1000.0) AT TIME ZONE 'UTC' "
      "FROM node_runs nr "
      "WHERE ta.node_run_id = nr.id AND nr.run_id = $1 AND nr.node_id = $2 "
      "AND ta.attempt_number = $3::int AND ta.lease_expires_at IS NULL",
      {run_id, node_id, std::to_string(attempt_number),
       std::to_string(expires_wall_ms)});
  if (r.res) PQclear(r.res);
  // 0 rows is fine: the lease was already initialized/acquired.
  return r.ok;
}

bool PgRunStore::acquire_attempt_lease(const std::string& run_id,
                                       const std::string& node_id,
                                       unsigned attempt_number,
                                       const std::string& worker_id,
                                       std::int64_t acquired_wall_ms,
                                       std::int64_t expires_wall_ms) {
  // Acquire (or idempotently re-acquire) the lease for a running attempt.
  // Steal guard: a DIFFERENT worker may only take over once the current lease
  // has expired (or no lease/worker is recorded yet).
  auto r = exec_params(
      "UPDATE task_attempts ta SET worker_id = $4, "
      "lease_acquired_at = to_timestamp($5::bigint / 1000.0) AT TIME ZONE "
      "'UTC', "
      "lease_renewed_at = to_timestamp($5::bigint / 1000.0) AT TIME ZONE "
      "'UTC', "
      "lease_expires_at = to_timestamp($6::bigint / 1000.0) AT TIME ZONE 'UTC' "
      "FROM node_runs nr "
      "WHERE ta.node_run_id = nr.id AND nr.run_id = $1 AND nr.node_id = $2 "
      "AND ta.attempt_number = $3::int AND ta.status = 'running' "
      "AND (ta.worker_id IS NULL OR ta.worker_id = $4 "
      "OR ta.lease_expires_at IS NULL "
      "OR ta.lease_expires_at <= to_timestamp($5::bigint / 1000.0) AT TIME "
      "ZONE 'UTC')",
      {run_id, node_id, std::to_string(attempt_number), worker_id,
       std::to_string(acquired_wall_ms), std::to_string(expires_wall_ms)});
  if (r.res) PQclear(r.res);
  return r.ok && r.rows_affected == 1;
}

bool PgRunStore::renew_attempt_lease(const std::string& run_id,
                                     const std::string& node_id,
                                     unsigned attempt_number,
                                     const std::string& worker_id,
                                     std::int64_t renewed_wall_ms,
                                     std::int64_t expires_wall_ms) {
  // Only the lease holder may renew, and only while the attempt is running.
  auto r = exec_params(
      "UPDATE task_attempts ta SET "
      "lease_renewed_at = to_timestamp($5::bigint / 1000.0) AT TIME ZONE "
      "'UTC', "
      "lease_expires_at = to_timestamp($6::bigint / 1000.0) AT TIME ZONE 'UTC' "
      "FROM node_runs nr "
      "WHERE ta.node_run_id = nr.id AND nr.run_id = $1 AND nr.node_id = $2 "
      "AND ta.attempt_number = $3::int AND ta.worker_id = $4 "
      "AND ta.status = 'running'",
      {run_id, node_id, std::to_string(attempt_number), worker_id,
       std::to_string(renewed_wall_ms), std::to_string(expires_wall_ms)});
  if (r.res) PQclear(r.res);
  return r.ok && r.rows_affected == 1;
}

std::vector<AttemptLeaseRecord> PgRunStore::scan_expired_attempt_leases(
    const std::string& run_id, std::int64_t now_wall_ms) {
  auto r = exec_params(
      "SELECT nr.node_id, ta.attempt_number, COALESCE(ta.worker_id, ''), "
      "ta.status, "
      "COALESCE(extract(epoch FROM ta.lease_acquired_at) * 1000, 0)::bigint, "
      "COALESCE(extract(epoch FROM ta.lease_renewed_at) * 1000, 0)::bigint, "
      "COALESCE(extract(epoch FROM ta.lease_expires_at) * 1000, 0)::bigint, "
      "COALESCE(extract(epoch FROM ta.lease_expired_at) * 1000, 0)::bigint "
      "FROM task_attempts ta JOIN node_runs nr ON ta.node_run_id = nr.id "
      "WHERE nr.run_id = $1 AND ta.status = 'running' "
      "AND ta.lease_expires_at IS NOT NULL "
      "AND ta.lease_expires_at <= to_timestamp($2::bigint / 1000.0) AT TIME "
      "ZONE 'UTC'",
      {run_id, std::to_string(now_wall_ms)});
  std::vector<AttemptLeaseRecord> out;
  if (r.ok && r.res) {
    const int rows = PQntuples(r.res);
    for (int i = 0; i < rows; ++i) {
      AttemptLeaseRecord rec;
      rec.node_id = PQgetvalue(r.res, i, 0);
      rec.attempt_number =
          static_cast<unsigned>(std::atoi(PQgetvalue(r.res, i, 1)));
      rec.worker_id = PQgetvalue(r.res, i, 2);
      rec.status = PQgetvalue(r.res, i, 3);
      rec.acquired_ms = std::atoll(PQgetvalue(r.res, i, 4));
      rec.renewed_ms = std::atoll(PQgetvalue(r.res, i, 5));
      rec.expires_ms = std::atoll(PQgetvalue(r.res, i, 6));
      rec.expired_ms = std::atoll(PQgetvalue(r.res, i, 7));
      out.push_back(std::move(rec));
    }
  }
  if (r.res) PQclear(r.res);
  return out;
}

bool PgRunStore::mark_attempt_lease_expired(const std::string& run_id,
                                            const std::string& node_id,
                                            unsigned attempt_number,
                                            const std::string& worker_id,
                                            std::int64_t expired_wall_ms) {
  // At-most-once reap: only a still-running attempt held by this worker can
  // transition to lease_expired. A racing completion already moved the status
  // out of 'running', so it can never be double-completed (M31 no-go).
  // worker_id is NULL for a never-claimed attempt (queue-wait lease); an
  // empty `worker_id` argument matches that case (NULL = '' is never true in
  // SQL, so it must be handled explicitly).
  auto r = exec_params(
      "UPDATE task_attempts ta SET status = 'lease_expired', "
      "lease_expired_at = to_timestamp($5::bigint / 1000.0) AT TIME ZONE "
      "'UTC' "
      "FROM node_runs nr "
      "WHERE ta.node_run_id = nr.id AND nr.run_id = $1 AND nr.node_id = $2 "
      "AND ta.attempt_number = $3::int "
      "AND (ta.worker_id = $4 OR ($4 = '' AND ta.worker_id IS NULL)) "
      "AND ta.status = 'running'",
      {run_id, node_id, std::to_string(attempt_number), worker_id,
       std::to_string(expired_wall_ms)});
  if (r.res) PQclear(r.res);
  return r.ok && r.rows_affected == 1;
}

std::optional<AttemptLeaseRecord> PgRunStore::get_attempt_lease(
    const std::string& run_id, const std::string& node_id,
    unsigned attempt_number) {
  auto r = exec_params(
      "SELECT nr.node_id, ta.attempt_number, COALESCE(ta.worker_id, ''), "
      "ta.status, "
      "COALESCE(extract(epoch FROM ta.lease_acquired_at) * 1000, 0)::bigint, "
      "COALESCE(extract(epoch FROM ta.lease_renewed_at) * 1000, 0)::bigint, "
      "COALESCE(extract(epoch FROM ta.lease_expires_at) * 1000, 0)::bigint, "
      "COALESCE(extract(epoch FROM ta.lease_expired_at) * 1000, 0)::bigint "
      "FROM task_attempts ta JOIN node_runs nr ON ta.node_run_id = nr.id "
      "WHERE nr.run_id = $1 AND nr.node_id = $2 AND ta.attempt_number = $3::int",
      {run_id, node_id, std::to_string(attempt_number)});
  std::optional<AttemptLeaseRecord> out;
  if (r.ok && r.res && PQntuples(r.res) == 1) {
    AttemptLeaseRecord rec;
    rec.node_id = PQgetvalue(r.res, 0, 0);
    rec.attempt_number =
        static_cast<unsigned>(std::atoi(PQgetvalue(r.res, 0, 1)));
    rec.worker_id = PQgetvalue(r.res, 0, 2);
    rec.status = PQgetvalue(r.res, 0, 3);
    rec.acquired_ms = std::atoll(PQgetvalue(r.res, 0, 4));
    rec.renewed_ms = std::atoll(PQgetvalue(r.res, 0, 5));
    rec.expires_ms = std::atoll(PQgetvalue(r.res, 0, 6));
    rec.expired_ms = std::atoll(PQgetvalue(r.res, 0, 7));
    out = rec;
  }
  if (r.res) PQclear(r.res);
  return out;
}

std::optional<WorkerRecord> PgRunStore::get_worker(const std::string& worker_id) {
  auto r = exec_params(
      "SELECT worker_id, env_prefix, status, "
      "COALESCE(extract(epoch FROM last_heartbeat_at) * 1000, 0)::bigint "
      "FROM workers WHERE worker_id = $1",
      {worker_id});
  std::optional<WorkerRecord> out;
  if (r.ok && r.res && PQntuples(r.res) == 1) {
    WorkerRecord rec;
    rec.worker_id = PQgetvalue(r.res, 0, 0);
    rec.env_prefix = PQgetvalue(r.res, 0, 1);
    rec.status = PQgetvalue(r.res, 0, 2);
    rec.last_heartbeat_ms = std::atoll(PQgetvalue(r.res, 0, 3));
    out = rec;
  }
  if (r.res) PQclear(r.res);
  return out;
}

// --- Milestone 33: idempotency ledger ---------------------------------------

bool PgRunStore::claim_idempotency_key(const std::string& key,
                                       const std::string& run_id,
                                       const std::string& response_json) {
  if (key.empty()) return false;
  // First claim wins: idempotency_records.key is the PRIMARY KEY, so a
  // duplicate claim hits the unique constraint and affects 0 rows (M33
  // step 2: durable idempotency record with unique constraint).
  auto r = exec_params(
      "INSERT INTO idempotency_records (key, run_id, response) "
      "VALUES ($1, NULLIF($2, ''), NULLIF($3, '')::jsonb) "
      "ON CONFLICT (key) DO NOTHING",
      {key, run_id, response_json});
  if (r.res) PQclear(r.res);
  return r.ok && r.rows_affected == 1;
}

std::optional<std::string> PgRunStore::get_idempotency_response(
    const std::string& key) {
  auto r = exec_params(
      "SELECT COALESCE(response::text, '') FROM idempotency_records "
      "WHERE key = $1",
      {key});
  std::optional<std::string> out;
  if (r.ok && r.res && PQntuples(r.res) == 1) {
    out = std::string(PQgetvalue(r.res, 0, 0));
  }
  if (r.res) PQclear(r.res);
  return out;
}

std::optional<RunRecord> PgRunStore::get_run(const std::string& run_id) {
  auto r = exec_params(
      "SELECT id, org_id, workflow_id::text, "
      "COALESCE(workflow_version_id::text, ''), engine, status, "
      "COALESCE(outcome, ''), "
      "COALESCE(cancel_reason, ''), "
      "COALESCE(extract(epoch FROM cancel_requested_at) * 1000, 0)::bigint, "
      "COALESCE(dag_json, '') "
      "FROM workflow_runs WHERE id = $1",
      {run_id});
  std::optional<RunRecord> out;
  if (r.ok && r.res && PQntuples(r.res) == 1) {
    RunRecord rec;
    rec.run_id = PQgetvalue(r.res, 0, 0);
    rec.org_id = PQgetvalue(r.res, 0, 1);
    rec.workflow_id = PQgetvalue(r.res, 0, 2);
    rec.workflow_version_id = PQgetvalue(r.res, 0, 3);
    rec.engine = PQgetvalue(r.res, 0, 4);
    rec.status = PQgetvalue(r.res, 0, 5);
    rec.outcome = PQgetvalue(r.res, 0, 6);
    rec.cancel_reason = PQgetvalue(r.res, 0, 7);
    rec.cancel_requested_at = std::atoll(PQgetvalue(r.res, 0, 8));
    rec.dag_json = PQgetvalue(r.res, 0, 9);
    out = rec;
  }
  if (r.res) PQclear(r.res);
  return out;
}

std::optional<NodeRunRecord> PgRunStore::get_node_run(
    const std::string& run_id, const std::string& node_id) {
  auto r = exec_params(
      "SELECT node_id, node_type, status, attempt_count, "
      "COALESCE(output::text, ''), COALESCE(failure_reason, ''), "
      "COALESCE(extract(epoch FROM retry_wait_until) * 1000, 0)::bigint, "
      "COALESCE(retry_reason, '') "
      "FROM node_runs WHERE run_id = $1 AND node_id = $2",
      {run_id, node_id});
  std::optional<NodeRunRecord> out;
  if (r.ok && r.res && PQntuples(r.res) == 1) {
    NodeRunRecord rec;
    rec.node_id = PQgetvalue(r.res, 0, 0);
    rec.node_type = PQgetvalue(r.res, 0, 1);
    rec.status = PQgetvalue(r.res, 0, 2);
    rec.attempt_count = std::atoi(PQgetvalue(r.res, 0, 3));
    rec.output_json = PQgetvalue(r.res, 0, 4);
    rec.failure_reason = PQgetvalue(r.res, 0, 5);
    rec.retry_wait_until = std::atoll(PQgetvalue(r.res, 0, 6));
    rec.retry_reason = PQgetvalue(r.res, 0, 7);
    out = rec;
  }
  if (r.res) PQclear(r.res);
  return out;
}

std::size_t PgRunStore::attempt_row_count(const std::string& run_id,
                                          const std::string& node_id) {
  auto r = exec_params(
      "SELECT count(*) FROM task_attempts ta "
      "JOIN node_runs nr ON ta.node_run_id = nr.id "
      "WHERE nr.run_id = $1 AND nr.node_id = $2",
      {run_id, node_id});
  std::size_t out = 0;
  if (r.ok && r.res && PQntuples(r.res) == 1) {
    out = static_cast<std::size_t>(std::atoll(PQgetvalue(r.res, 0, 0)));
  }
  if (r.res) PQclear(r.res);
  return out;
}

std::vector<std::string> PgRunStore::attempt_worker_ids(
    const std::string& run_id, const std::string& node_id) {
  auto r = exec_params(
      "SELECT COALESCE(ta.worker_id, '') FROM task_attempts ta "
      "JOIN node_runs nr ON ta.node_run_id = nr.id "
      "WHERE nr.run_id = $1 AND nr.node_id = $2 "
      "ORDER BY ta.attempt_number",
      {run_id, node_id});
  std::vector<std::string> out;
  if (r.ok && r.res) {
    const int rows = PQntuples(r.res);
    for (int i = 0; i < rows; ++i) {
      out.emplace_back(PQgetvalue(r.res, i, 0));
    }
  }
  if (r.res) PQclear(r.res);
  return out;
}

// --- Milestone 35: reconstruction readers -----------------------------------

std::vector<NodeRunRecord> PgRunStore::list_node_runs(
    const std::string& run_id) {
  auto r = exec_params(
      "SELECT node_id, node_type, status, attempt_count, "
      "COALESCE(output::text, ''), COALESCE(failure_reason, ''), "
      "COALESCE(extract(epoch FROM retry_wait_until) * 1000, 0)::bigint, "
      "COALESCE(retry_reason, '') "
      "FROM node_runs WHERE run_id = $1 ORDER BY node_id",
      {run_id});
  std::vector<NodeRunRecord> out;
  if (r.ok && r.res) {
    const int rows = PQntuples(r.res);
    out.reserve(rows);
    for (int i = 0; i < rows; ++i) {
      NodeRunRecord rec;
      rec.node_id = PQgetvalue(r.res, i, 0);
      rec.node_type = PQgetvalue(r.res, i, 1);
      rec.status = PQgetvalue(r.res, i, 2);
      rec.attempt_count = std::atoi(PQgetvalue(r.res, i, 3));
      rec.output_json = PQgetvalue(r.res, i, 4);
      rec.failure_reason = PQgetvalue(r.res, i, 5);
      rec.retry_wait_until = std::atoll(PQgetvalue(r.res, i, 6));
      rec.retry_reason = PQgetvalue(r.res, i, 7);
      out.push_back(std::move(rec));
    }
  }
  if (r.res) PQclear(r.res);
  return out;
}

std::vector<std::string> PgRunStore::list_active_evo_run_ids() {
  auto r = exec_params(
      "SELECT id FROM workflow_runs "
      "WHERE engine = 'evo' AND status IN ('queued', 'running') "
      "ORDER BY id",
      {});
  std::vector<std::string> out;
  if (r.ok && r.res) {
    const int rows = PQntuples(r.res);
    out.reserve(rows);
    for (int i = 0; i < rows; ++i) {
      out.emplace_back(PQgetvalue(r.res, i, 0));
    }
  }
  if (r.res) PQclear(r.res);
  return out;
}

}  // namespace evo
