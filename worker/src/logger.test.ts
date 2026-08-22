// Milestone 38: structured JSON logging + secret redaction for the TS worker.
//
// Pure unit test (no Redis/Postgres): verifies the logger's parsing, JSON
// shape, and redaction rules. Run via `tsx worker/src/logger.test.ts`.

import assert from "node:assert/strict";

import {
  createWorkerLogger,
  isSecretKey,
  parseWorkerLine,
  redactSecrets,
  REDACTED,
} from "./logger";

let passed = 0;
const ok = (label: string) => {
  passed++;
  console.log(`  ok   ${label}`);
};

// --- 1. Secret-like keys are recognized ------------------------------------
for (const k of [
  "password",
  "db_password",
  "PASSWORD",
  "api_key",
  "apiKey",
  "engine_token",
  "authorization",
  "client_secret",
  "credential",
  "private_key",
]) {
  assert.equal(isSecretKey(k), true, `${k} should be secret-like`);
}
ok("secret-like keys recognized (password/token/api_key/credential/...)");

// --- 2. Identifier/correlation keys are NOT flagged ------------------------
for (const k of ["run_id", "org_id", "node_id", "trace_id", "worker_id", "event"]) {
  assert.equal(isSecretKey(k), false, `${k} should not be secret-like`);
}
ok("identifier keys not flagged (run_id/org_id/trace_id/...)");

// --- 3. redactSecrets masks secret values in free text ---------------------
assert.equal(
  redactSecrets("connecting password=hunter2 host=db"),
  `connecting password=${REDACTED} host=db`,
);
assert.equal(
  redactSecrets("Authorization: Bearer abc123.xyz"),
  `Authorization: Bearer ${REDACTED}`,
);
assert.equal(
  redactSecrets("url=postgres://evo:hunter2@127.0.0.1:5433/evo"),
  `url=postgres://evo:${REDACTED}@127.0.0.1:5433/evo`,
);
assert.equal(
  redactSecrets("run=r-1 org=org-a"),
  "run=r-1 org=org-a",
  "identifiers pass through untouched",
);
ok("redactSecrets masks password/Bearer/URL credentials, keeps identifiers");

// --- 4. parseWorkerLine extracts worker_id + key=value fields --------------
{
  const { workerId, msg, fields } = parseWorkerLine(
    "[w-1] task done run=r-1 node=n0 attempt=2",
  );
  assert.equal(workerId, "w-1");
  assert.equal(msg, "task done");
  assert.deepEqual(fields, { run: "r-1", node: "n0", attempt: "2" });
}
{
  // The generic "[worker]" prefix is not an identity.
  const { workerId, msg } = parseWorkerLine("[worker] draining...");
  assert.equal(workerId, undefined);
  assert.equal(msg, "draining...");
}
ok("parseWorkerLine extracts worker_id + fields; [worker] prefix ignored");

// --- 5. createWorkerLogger emits parseable JSON with redaction -------------
{
  const lines: string[] = [];
  const logger = createWorkerLogger({
    service: "evo-worker",
    write: (l) => lines.push(l),
  });

  logger.log("[w-9] claimed task run=r-1 node=n0 trace_id=t-1 password=hunter2");
  assert.equal(lines.length, 1);
  const rec = JSON.parse(lines[0]) as Record<string, unknown>;
  assert.equal(rec.service, "evo-worker");
  assert.equal(rec.level, "info");
  assert.equal(typeof rec.ts_ms, "number");
  assert.equal(rec.worker_id, "w-9");
  assert.equal(rec.run, "r-1");
  assert.equal(rec.trace_id, "t-1");
  assert.equal(rec.password, REDACTED, "secret field redacted");
  assert.ok(
    !lines[0].includes("hunter2"),
    "raw secret never appears in the line",
  );
}
ok("logger.log emits parseable JSON with correlation fields + redaction");

// --- 6. logEvent emits explicit structured events --------------------------
{
  const lines: string[] = [];
  const logger = createWorkerLogger({
    service: "evo-worker",
    write: (l) => lines.push(l),
  });
  logger.logEvent("warn", "lease_renew_failed", {
    run_id: "r-1",
    node_id: "n0",
    api_key: "should-not-appear",
  });
  const rec = JSON.parse(lines[0]) as Record<string, unknown>;
  assert.equal(rec.level, "warn");
  assert.equal(rec.event, "lease_renew_failed");
  assert.equal(rec.run_id, "r-1");
  assert.equal(rec.api_key, REDACTED);
  assert.ok(!lines[0].includes("should-not-appear"));
}
ok("logEvent emits explicit structured events with redaction");

console.log(`\nALL M38 WORKER LOGGER TESTS PASSED! (${passed}/${passed})`);
