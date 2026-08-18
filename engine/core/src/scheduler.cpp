#include "evo/scheduler.hpp"

#include <algorithm>
#include <cstddef>
#include <string>

#include "evo/json.hpp"

namespace evo {

bool RunLog::all_ok() const {
  for (const auto& r : runs) {
    if (!r.ok()) return false;
  }
  return !runs.empty();
}

std::string RunLog::to_json_string() const {
  json::Array arr;
  for (const auto& r : runs) {
    json::Object o;
    o.emplace("id", json::Value(r.id.value));
    o.emplace("type", json::Value(r.type));
    o.emplace("seq", json::Value(static_cast<double>(r.sequence)));
    o.emplace("ok", json::Value(r.ok()));
    o.emplace("output", json::Value(r.result.output));
    arr.push_back(json::Value(std::move(o)));
  }
  json::Object doc;
  doc.emplace("runs", json::Value(std::move(arr)));
  return json::serialize(json::Value(std::move(doc)));
}

Scheduler::Scheduler(Dag dag, std::map<std::string, TaskFn> tasks)
    : dag_(std::move(dag)), tasks_(std::move(tasks)) {}

RunLog Scheduler::run() {
  RunLog log;
  const auto& order = dag_.topo_order();
  log.runs.reserve(order.size());

  for (std::size_t seq = 0; seq < order.size(); ++seq) {
    const NodeId& id = order[seq];
    const NodeSpec* spec = dag_.node(id);  // always present (id comes from the Dag)

    NodeRun rec;
    rec.id = id;
    rec.type = spec ? spec->type : std::string{};
    rec.sequence = seq;
    rec.started_at = std::chrono::steady_clock::now();

    auto it = tasks_.find(rec.type);
    if (it == tasks_.end()) {
      rec.result = TaskResult{false, "unregistered task type: " + rec.type};
    } else {
      rec.result = it->second(spec ? *spec : NodeSpec{});
    }
    rec.finished_at = std::chrono::steady_clock::now();

    log.runs.push_back(std::move(rec));
    if (!log.runs.back().ok()) break;  // sequential halt on first failure
  }
  return log;
}

}  // namespace evo
