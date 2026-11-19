#pragma once

namespace duckdb {
class DatabaseInstance;

namespace decide_grammar {

//! Registers the "decide_amox" grammar extension and activates it on every connection:
//!   FROM t DECIDE (CHOOSE <question> AMONG (key 'description', ...) AS name, ASK <question> AS name GIVEN col, ...)
//!   amox_choose(<question> AMONG (...) GIVEN col, ...)   and   amox_yes_no(<question> GIVEN col, ...)
//! The clause is lowered to plain amox_choose / amox_yes_no calls, so it shares their batching and cache.
void RegisterDecideGrammar(DatabaseInstance &db);

} // namespace decide_grammar
} // namespace duckdb
