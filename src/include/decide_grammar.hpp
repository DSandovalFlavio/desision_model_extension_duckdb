#pragma once

namespace duckdb {
class DatabaseInstance;

namespace decide_grammar {

//! Registers the "decide" grammar extension and activates it on every connection:
//!   FROM t DECIDE (CHOOSE <question> AMONG (key 'description', ...) AS name, ASK <question> AS name GIVEN col, ...)
//!   decision_choose(<question> AMONG (...) GIVEN col, ...)   and   decision_yes_no(<question> GIVEN col, ...)
//! The clause is lowered to plain decision_choose / decision_yes_no calls, so it shares their batching and cache.
void RegisterDecideGrammar(DatabaseInstance &db);

} // namespace decide_grammar
} // namespace duckdb
