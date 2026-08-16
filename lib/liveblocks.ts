import { Liveblocks } from "@liveblocks/node";

let liveblocksClient: Liveblocks | null = null;

export function getLiveblocksClient() {
  const secret = process.env.LIVEBLOCKS_SECRET_KEY;

  if (!secret) {
    throw new Error("LIVEBLOCKS_SECRET_KEY is not set");
  }

  if (!secret.startsWith("sk_")) {
    throw new Error("LIVEBLOCKS_SECRET_KEY must start with sk_");
  }

  liveblocksClient ??= new Liveblocks({
    secret,
  });

  return liveblocksClient;
}
