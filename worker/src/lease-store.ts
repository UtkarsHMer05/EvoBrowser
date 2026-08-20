// Phase 2 — Postgres-backed worker-registry + task-lease store (Milestone 31).
//
// The TS-side implementation of the worker's TaskLeaseStore contract
// (worker/src/worker.ts). It talks to the SAME local Phase-2 Postgres the C++
// PgRunStore writes to, using raw PARAMETERIZED SQL that mirrors the C++
// queries exactly — including the `to_timestamp(ms/1000.0) AT TIME ZONE 'UTC'`
// timestamp conversion — so lease timestamps written here are compared
// correctly by the C++ scheduler's expired-lease scan.
//
// Semantics (mirror engine/pg/src/pg_run_store.cpp):
//   - workerHeartbeat: upsert the workers registry row (process liveness).
//   - acquireAttemptLease: take over the queue-wait lease the scheduler
//     initialized at dispatch; a different worker cannot steal an unexpired
//     lease.
//   - renewAttemptLease: only the lease holder may renew, while running.
//
// All timestamps are wall-clock UTC milliseconds.

import pg from "pg";

import { getPhase2DbConfig } from "@/lib/db/phase2";

import type { TaskLeaseStore } from "./worker";

export class PgTaskLeaseStore implements TaskLeaseStore {
  private pool: pg.Pool;

  constructor() {
    const cfg = getPhase2DbConfig();
    this.pool = new pg.Pool({
      host: cfg.host,
      port: cfg.port,
      user: cfg.user,
      password: cfg.password,
      database: cfg.database,
      max: 3,
      connectionTimeoutMillis: 3000,
    });
  }

  async workerHeartbeat(
    workerId: string,
    envPrefix: string,
    nowWallMs: number,
  ): Promise<boolean> {
    const r = await this.pool.query(
      `INSERT INTO workers (worker_id, env_prefix, status, last_heartbeat_at)
       VALUES ($1, $2, 'alive', to_timestamp($3::bigint / 1000.0) AT TIME ZONE 'UTC')
       ON CONFLICT (worker_id) DO UPDATE SET env_prefix = EXCLUDED.env_prefix,
         status = 'alive', last_heartbeat_at = EXCLUDED.last_heartbeat_at`,
      [workerId, envPrefix, String(nowWallMs)],
    );
    return r.rowCount !== null;
  }

  async acquireAttemptLease(
    runId: string,
    nodeId: string,
    attemptNumber: number,
    workerId: string,
    acquiredWallMs: number,
    expiresWallMs: number,
  ): Promise<boolean> {
    const r = await this.pool.query(
      `UPDATE task_attempts ta SET worker_id = $4,
         lease_acquired_at = to_timestamp($5::bigint / 1000.0) AT TIME ZONE 'UTC',
         lease_renewed_at  = to_timestamp($5::bigint / 1000.0) AT TIME ZONE 'UTC',
         lease_expires_at  = to_timestamp($6::bigint / 1000.0) AT TIME ZONE 'UTC'
       FROM node_runs nr
       WHERE ta.node_run_id = nr.id AND nr.run_id = $1 AND nr.node_id = $2
         AND ta.attempt_number = $3::int AND ta.status = 'running'
         AND (ta.worker_id IS NULL OR ta.worker_id = $4
              OR ta.lease_expires_at IS NULL
              OR ta.lease_expires_at <= to_timestamp($5::bigint / 1000.0) AT TIME ZONE 'UTC')`,
      [runId, nodeId, attemptNumber, workerId, String(acquiredWallMs), String(expiresWallMs)],
    );
    return (r.rowCount ?? 0) === 1;
  }

  async renewAttemptLease(
    runId: string,
    nodeId: string,
    attemptNumber: number,
    workerId: string,
    renewedWallMs: number,
    expiresWallMs: number,
  ): Promise<boolean> {
    const r = await this.pool.query(
      `UPDATE task_attempts ta SET
         lease_renewed_at = to_timestamp($5::bigint / 1000.0) AT TIME ZONE 'UTC',
         lease_expires_at = to_timestamp($6::bigint / 1000.0) AT TIME ZONE 'UTC'
       FROM node_runs nr
       WHERE ta.node_run_id = nr.id AND nr.run_id = $1 AND nr.node_id = $2
         AND ta.attempt_number = $3::int AND ta.worker_id = $4
         AND ta.status = 'running'`,
      [runId, nodeId, attemptNumber, workerId, String(renewedWallMs), String(expiresWallMs)],
    );
    return (r.rowCount ?? 0) === 1;
  }

  async close(): Promise<void> {
    await this.pool.end().catch(() => undefined);
  }
}
