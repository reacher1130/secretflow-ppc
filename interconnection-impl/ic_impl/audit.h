#pragma once
#include <cstdlib>
#include <chrono>
#include <atomic>
#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"
namespace ic_impl {
inline void Audit(const std::string& stage, const std::string& event, nlohmann::json detail = nlohmann::json::object()) {
  static std::atomic<uint64_t> seq{0};
  auto env=[](const char* key){const char* p=std::getenv(key);return p?std::string(p):std::string();};
  detail["schema"]="bfia-audit-v1"; detail["seq"]=++seq;
  detail["timestamp_ms"]=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  detail["task_id"]=env("config.task_id"); detail["trace_id"]=env("config.trace_id");
  detail["party"]=env("BFIA_SELF_PARTY"); detail["algorithm"]=env("BFIA_ALGORITHM");
  detail["stage"]=stage; detail["event"]=event;
  SPDLOG_INFO("BFIA_EVENT {}", detail.dump());
}
}
