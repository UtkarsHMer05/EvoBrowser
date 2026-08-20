CREATE TABLE "workers" (
	"worker_id" text PRIMARY KEY NOT NULL,
	"env_prefix" text NOT NULL,
	"status" text DEFAULT 'alive' NOT NULL,
	"registered_at" timestamp DEFAULT now() NOT NULL,
	"last_heartbeat_at" timestamp
);
--> statement-breakpoint
ALTER TABLE "task_attempts" ADD COLUMN "lease_acquired_at" timestamp;--> statement-breakpoint
ALTER TABLE "task_attempts" ADD COLUMN "lease_renewed_at" timestamp;--> statement-breakpoint
ALTER TABLE "task_attempts" ADD COLUMN "lease_expires_at" timestamp;--> statement-breakpoint
ALTER TABLE "task_attempts" ADD COLUMN "lease_expired_at" timestamp;--> statement-breakpoint
CREATE INDEX "ix_workers_env_prefix" ON "workers" USING btree ("env_prefix");--> statement-breakpoint
CREATE INDEX "ix_task_attempts_lease_expires" ON "task_attempts" USING btree ("lease_expires_at");