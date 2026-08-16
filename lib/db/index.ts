import { neon } from "@neondatabase/serverless";
import { drizzle } from "drizzle-orm/neon-http";

import * as schema from "./schema";

let dbClient: ReturnType<typeof drizzle> | null = null;

export function getDb() {
  const databaseUrl = process.env.DATABASE_URL;

  if (!databaseUrl) {
    throw new Error("DATABASE_URL is not set");
  }

  if (!dbClient) {
    // Pooled HTTP connection — safe for serverless/edge and Next.js Server Components.
    const sql = neon(databaseUrl);
    dbClient = drizzle({ client: sql, schema, casing: "snake_case" });
  }

  return dbClient;
}

export { schema };
