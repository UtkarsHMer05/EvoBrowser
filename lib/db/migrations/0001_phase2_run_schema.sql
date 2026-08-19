CREATE TABLE "idempotency_records" (
	"key" text PRIMARY KEY NOT NULL,
	"run_id" text,
	"response" jsonb,
	"created_at" timestamp DEFAULT now() NOT NULL
);
--> statement-breakpoint
CREATE TABLE "node_runs" (
	"id" uuid PRIMARY KEY DEFAULT gen_random_uuid() NOT NULL,
	"run_id" text NOT NULL,
	"node_id" text NOT NULL,
	"node_type" text NOT NULL,
	"status" text DEFAULT 'blocked' NOT NULL,
	"attempt_count" integer DEFAULT 0 NOT NULL,
	"output" jsonb,
	"failure_reason" text,
	"started_at" timestamp,
	"finished_at" timestamp,
	CONSTRAINT "uq_node_runs_run_node" UNIQUE("run_id","node_id")
);
--> statement-breakpoint
CREATE TABLE "task_attempts" (
	"id" uuid PRIMARY KEY DEFAULT gen_random_uuid() NOT NULL,
	"node_run_id" uuid NOT NULL,
	"attempt_number" integer NOT NULL,
	"worker_id" text,
	"status" text DEFAULT 'queued' NOT NULL,
	"output" jsonb,
	"error" text,
	"started_at" timestamp,
	"finished_at" timestamp,
	CONSTRAINT "uq_task_attempts_node_attempt" UNIQUE("node_run_id","attempt_number")
);
--> statement-breakpoint
CREATE TABLE "workflow_runs" (
	"id" text PRIMARY KEY NOT NULL,
	"org_id" text NOT NULL,
	"workflow_id" uuid NOT NULL,
	"workflow_version_id" uuid,
	"engine" text DEFAULT 'legacy' NOT NULL,
	"status" text DEFAULT 'queued' NOT NULL,
	"outcome" text,
	"cancel_reason" text,
	"created_at" timestamp DEFAULT now() NOT NULL,
	"started_at" timestamp,
	"finished_at" timestamp
);
--> statement-breakpoint
CREATE TABLE "workflow_versions" (
	"id" uuid PRIMARY KEY DEFAULT gen_random_uuid() NOT NULL,
	"workflow_id" uuid NOT NULL,
	"org_id" text NOT NULL,
	"version_number" integer NOT NULL,
	"graph" jsonb NOT NULL,
	"graph_hash" text,
	"created_at" timestamp DEFAULT now() NOT NULL,
	CONSTRAINT "uq_workflow_versions_workflow_version" UNIQUE("workflow_id","version_number")
);
--> statement-breakpoint
ALTER TABLE "node_runs" ADD CONSTRAINT "node_runs_run_id_workflow_runs_id_fk" FOREIGN KEY ("run_id") REFERENCES "public"."workflow_runs"("id") ON DELETE no action ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "task_attempts" ADD CONSTRAINT "task_attempts_node_run_id_node_runs_id_fk" FOREIGN KEY ("node_run_id") REFERENCES "public"."node_runs"("id") ON DELETE no action ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "workflow_runs" ADD CONSTRAINT "workflow_runs_workflow_id_workflows_id_fk" FOREIGN KEY ("workflow_id") REFERENCES "public"."workflows"("id") ON DELETE no action ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "workflow_runs" ADD CONSTRAINT "workflow_runs_workflow_version_id_workflow_versions_id_fk" FOREIGN KEY ("workflow_version_id") REFERENCES "public"."workflow_versions"("id") ON DELETE no action ON UPDATE no action;--> statement-breakpoint
ALTER TABLE "workflow_versions" ADD CONSTRAINT "workflow_versions_workflow_id_workflows_id_fk" FOREIGN KEY ("workflow_id") REFERENCES "public"."workflows"("id") ON DELETE no action ON UPDATE no action;--> statement-breakpoint
CREATE INDEX "ix_node_runs_run" ON "node_runs" USING btree ("run_id");--> statement-breakpoint
CREATE INDEX "ix_task_attempts_node_run" ON "task_attempts" USING btree ("node_run_id");--> statement-breakpoint
CREATE INDEX "ix_workflow_runs_org_created" ON "workflow_runs" USING btree ("org_id","created_at");--> statement-breakpoint
CREATE INDEX "ix_workflow_runs_workflow" ON "workflow_runs" USING btree ("workflow_id");--> statement-breakpoint
CREATE INDEX "ix_workflow_runs_status" ON "workflow_runs" USING btree ("status");--> statement-breakpoint
CREATE INDEX "ix_workflow_versions_org" ON "workflow_versions" USING btree ("org_id");