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
  auto r = exec_params(
      "INSERT INTO workflow_runs (id, org_id, workflow_id, "
      "workflow_version_id, engine, status, started_at) "
      "VALUES ($1, $2, $3::uuid, NULLIF($4, '')::uuid, $5, $6, "
      "to_timestamp($7::bigint / 1000.0) AT TIME ZONE 'UTC') "
      "ON CONFLICT (id) DO NOTHING",
      {run.run_id, run.org_id, run.workflow_id, run.workflow_version_id,
       run.engine, run.status, std::to_string(started_wall_ms)});
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

std::optional<RunRecord> PgRunStore::get_run(const std::string& run_id) {
  auto r = exec_params(
      "SELECT id, org_id, workflow_id::text, "
      "COALESCE(workflow_version_id::text, ''), engine, status, "
      "COALESCE(outcome, '') FROM workflow_runs WHERE id = $1",
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
    out = rec;
  }
  if (r.res) PQclear(r.res);
  return out;
}

std::optional<NodeRunRecord> PgRunStore::get_node_run(
    const std::string& run_id, const std::string& node_id) {
  auto r = exec_params(
      "SELECT node_id, node_type, status, attempt_count, "
      "COALESCE(output::text, ''), COALESCE(failure_reason, '') "
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

}  // namespace evo
