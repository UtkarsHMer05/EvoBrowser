// Phase 2 — local Phase-2 Postgres client for the Next.js app (Milestone 29).
//
// The C++ scheduler and the distributed workers persist engine-neutral run state
// (workflow_runs, node_runs, task_attempts, run_artifacts, live_view_connections)
// to the LOCAL Phase-2 Postgres (scripts/phase2, default 127.0.0.1:5433) — NOT
// to the app's Neon database. `getDb()` (lib/db) points at Neon and only sees
// Phase-1 tables, so Evo run state is invisible to it.
//
// This module gives the app a drizzle client over the SAME local Phase-2
// Postgres the engine writes to, so Evo run/node/artifact reads and writes are
// consistent with the engine. It reuses the shared Drizzle schema (the DDL
// authority) and the node-postgres adapter.
//
// Connection settings mirror the worker / C++ PgRunStore defaults and are
// overridable via the same EVO_PHASE2_PG_* env vars. Credentials stay
// server-side; they are never sent to the browser.
//
// Timestamps are wall-clock UTC (database now()), consistent with M19/M26.

import { drizzle } from "drizzle-orm/node-postgres";
import pg from "pg";

import * as schema from "./schema";

export interface Phase2DbConfig {
  host: string;
  port: number;
  user: string;
  password: string;
  database: string;
}

export function getPhase2DbConfig(): Phase2DbConfig {
  return {
    host: process.env.EVO_PHASE2_PG_HOST ?? "127.0.0.1",
    port: Number(process.env.EVO_PHASE2_PG_PORT ?? 5433),
    user: process.env.EVO_PHASE2_PG_USER ?? "evo",
    password: process.env.EVO_PHASE2_PG_PASSWORD ?? "evo_dev_password",
    database: process.env.EVO_PHASE2_PG_DB ?? "evo_phase2",
  };
}

// The drizzle instance type matches the Phase-1 client so callers can pass
// either interchangeably where a query builder is expected (VersioningDb).
export type Phase2Db = ReturnType<typeof makePhase2Db>;

let cachedPool: pg.Pool | null = null;
let cachedDb: Phase2Db | null = null;

function makePhase2Db(pool: pg.Pool) {
  return drizzle(pool, { schema, casing: "snake_case" });
}

/**
 * Lazily create + cache a drizzle client over the local Phase-2 Postgres.
 * Server-side only. The pool is bounded and reused across requests.
 */
export function getPhase2Db(): Phase2Db {
  if (!cachedDb) {
    const cfg = getPhase2DbConfig();
    cachedPool = new pg.Pool({
      host: cfg.host,
      port: cfg.port,
      user: cfg.user,
      password: cfg.password,
      database: cfg.database,
      max: 5,
      connectionTimeoutMillis: 3000,
    });
    cachedDb = makePhase2Db(cachedPool);
  }
  return cachedDb;
}

/** Close the cached pool (tests / graceful shutdown). */
export async function closePhase2Db(): Promise<void> {
  if (cachedPool) {
    await cachedPool.end().catch(() => undefined);
    cachedPool = null;
    cachedDb = null;
  }
}
