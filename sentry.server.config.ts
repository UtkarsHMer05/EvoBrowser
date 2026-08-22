import * as Sentry from "@sentry/nextjs";

Sentry.init({
  dsn: process.env.SENTRY_DSN,

  tracesSampleRate: process.env.NODE_ENV === "development" ? 1.0 : 0.1,

  // Attach local variable values to stack frames
  includeLocalVariables: true,

  enableLogs: true,
  // Keep debug off: the build tree-shakes Sentry's debug logger out of the
  // bundle (withSentryConfig webpack.treeshake.removeDebugLogging), and
  // enabling `debug` here makes the SDK warn at startup and floods the dev
  // console with trace spans. Error tracking + tracing work without it.
  debug: false,
});
