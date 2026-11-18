#define DUCKDB_EXTENSION_MAIN

#include "decide_extension.hpp"

#include "decide_models.hpp"
#include "decide_grammar.hpp"
#include "decide_runtime.hpp"
#include "decide_store.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/vector/constant_vector.hpp"
#include "duckdb/common/vector/flat_vector.hpp"
#include "duckdb/common/vector/list_vector.hpp"
#include "duckdb/common/vector/string_vector.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/config.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

#include <unordered_map>

namespace duckdb {

static constexpr const char *SETTING_MODEL = "decide_model";
static constexpr const char *SETTING_MODEL_DIR = "decide_model_dir";
static constexpr const char *SETTING_MODEL_PATH = "decide_model_path";

// ---------------------------------------------------------------------------------------------------------------
// Settings and model resolution
// ---------------------------------------------------------------------------------------------------------------

static string GetStringSetting(ClientContext &context, const char *name, const string &fallback) {
	Value v;
	if (context.TryGetCurrentSetting(name, v) && !v.IsNull()) {
		auto s = v.ToString();
		if (!s.empty()) {
			return s;
		}
	}
	return fallback;
}

static void ValidateModelSetting(ClientContext &, SetScope, Value &parameter) {
	auto id = parameter.ToString();
	if (!decide::FindModel(id)) {
		throw InvalidInputException("Unknown decide_model '%s'. Available models: %s", id, decide::ModelIdList());
	}
}

static const decide::ModelSpec &SpecOrThrow(const string &id) {
	auto spec = decide::FindModel(id);
	if (!spec) {
		throw InvalidInputException("Unknown model '%s'. Available models: %s", id, decide::ModelIdList());
	}
	return *spec;
}

static string CurrentModelId(ClientContext &context) {
	return GetStringSetting(context, SETTING_MODEL, decide::DefaultModel().id);
}

//! Local GGUF file for `model_id`. decide_model_path overrides the file of the session's current model only.
static string ResolvePath(ClientContext &context, const string &model_id) {
	auto &spec = SpecOrThrow(model_id);
	auto override_path = GetStringSetting(context, SETTING_MODEL_PATH, "");
	if (!override_path.empty() && model_id == CurrentModelId(context)) {
		return override_path;
	}
	auto dir = GetStringSetting(context, SETTING_MODEL_DIR, decide::DefaultModelDir());
	Value auto_download;
	bool download = !(context.TryGetCurrentSetting("decide_auto_download", auto_download) &&
	                  !auto_download.IsNull() && !auto_download.GetValue<bool>());
	return decide_store::EnsureModelFile(context, spec, dir, download);
}

//! decide_download([model]) -> VARCHAR: download (or verify) a model now and return its local path.
static void DownloadFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &context = state.GetContext();
	for (idx_t i = 0; i < args.size(); i++) {
		string id = CurrentModelId(context);
		if (args.ColumnCount() > 0) {
			Value v = args.data[0].GetValue(i);
			if (v.IsNull()) {
				result.SetValue(i, Value(LogicalType::VARCHAR));
				continue;
			}
			id = v.ToString();
		}
		auto &spec = SpecOrThrow(id);
		auto dir = GetStringSetting(context, SETTING_MODEL_DIR, decide::DefaultModelDir());
		result.SetValue(i, Value(decide_store::EnsureModelFile(context, spec, dir, true)));
	}
}

static constexpr const char *SETTING_PARALLEL = "decide_parallel";
static constexpr const char *SETTING_BATCH_TOKENS = "decide_batch_tokens";
static constexpr const char *SETTING_GPU_LAYERS = "decide_gpu_layers";
static constexpr const char *SETTING_THREADS = "decide_threads";
static constexpr const char *SETTING_KV_CACHE = "decide_kv_cache";
static constexpr const char *SETTING_MAX_TOKENS = "decide_max_tokens";
static constexpr const char *SETTING_CACHE_SIZE = "decide_cache_size";

static int64_t GetIntSetting(ClientContext &context, const char *name, int64_t fallback) {
	Value v;
	if (context.TryGetCurrentSetting(name, v) && !v.IsNull()) {
		return v.GetValue<int64_t>();
	}
	return fallback;
}

static void ValidateKvCache(ClientContext &, SetScope, Value &parameter) {
	auto s = parameter.ToString();
	if (s != "f16" && s != "q8_0") {
		throw InvalidInputException("decide_kv_cache must be 'f16' or 'q8_0'");
	}
}

static void ValidatePositive(ClientContext &, SetScope, Value &parameter) {
	if (parameter.GetValue<int64_t>() <= 0) {
		throw InvalidInputException("value must be greater than 0");
	}
}

static decide::EngineConfig MakeEngineConfig(ClientContext &context, const string &model_id) {
	decide::EngineConfig c;
	c.path = ResolvePath(context, model_id);
	c.gpu_layers = (int32_t)GetIntSetting(context, SETTING_GPU_LAYERS, -1);
	c.parallel = (int32_t)GetIntSetting(context, SETTING_PARALLEL, 16);
	c.batch_tokens = (int32_t)GetIntSetting(context, SETTING_BATCH_TOKENS, 4096);
	c.threads = (int32_t)GetIntSetting(context, SETTING_THREADS, 0);
	c.kv_q8 = GetStringSetting(context, SETTING_KV_CACHE, "f16") == "q8_0";
	c.max_tokens = (int32_t)GetIntSetting(context, SETTING_MAX_TOKENS, 1024);
	c.cache_size = (size_t)std::max<int64_t>(0, GetIntSetting(context, SETTING_CACHE_SIZE, 100000));
	return c;
}

// ---------------------------------------------------------------------------------------------------------------
// decide_tokenize(text [, model]) -> INTEGER[]
// ---------------------------------------------------------------------------------------------------------------

static void TokenizeFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &context = state.GetContext();
	const bool has_model = args.ColumnCount() > 1;
	const auto count = args.size();

	UnifiedVectorFormat text_data, model_data;
	args.data[0].ToUnifiedFormat(count, text_data);
	if (has_model) {
		args.data[1].ToUnifiedFormat(count, model_data);
	}
	auto texts = UnifiedVectorFormat::GetData<string_t>(text_data);
	auto models = has_model ? UnifiedVectorFormat::GetData<string_t>(model_data) : nullptr;

	// DuckDB 2.0: read-only accessors by default, *Mutable variants for writing.
	auto list_entries = FlatVector::GetDataMutable<list_entry_t>(result);
	auto &result_validity = FlatVector::ValidityMutable(result);
	idx_t offset = ListVector::GetListSize(result);

	const string default_id = has_model ? string() : CurrentModelId(context);
	std::unordered_map<string, string> path_cache;

	for (idx_t i = 0; i < count; i++) {
		auto ti = text_data.sel->get_index(i);
		if (!text_data.validity.RowIsValid(ti)) {
			result_validity.SetInvalid(i);
			continue;
		}
		string model_id = default_id;
		if (has_model) {
			auto mi = model_data.sel->get_index(i);
			if (!model_data.validity.RowIsValid(mi)) {
				result_validity.SetInvalid(i);
				continue;
			}
			model_id = models[mi].GetString();
		}
		auto it = path_cache.find(model_id);
		if (it == path_cache.end()) {
			it = path_cache.emplace(model_id, ResolvePath(context, model_id)).first;
		}
		std::vector<int32_t> tokens;
		try {
			tokens = decide::Runtime::Get().Tokenize(it->second, texts[ti].GetString());
		} catch (std::exception &e) {
			throw InvalidInputException("decide_tokenize: %s", e.what());
		}
		ListVector::Reserve(result, offset + tokens.size());
		// Fetch the child after Reserve: reserving may reallocate it.
		auto child_data = FlatVector::GetDataMutable<int32_t>(ListVector::GetChildMutable(result));
		for (idx_t k = 0; k < tokens.size(); k++) {
			child_data[offset + k] = tokens[k];
		}
		list_entries[i] = list_entry_t(offset, tokens.size());
		offset += tokens.size();
	}
	ListVector::SetListSize(result, offset);
	if (args.AllConstant()) {
		result.SetVectorType(VectorType::CONSTANT_VECTOR);
	}
}

// ---------------------------------------------------------------------------------------------------------------
// decision_choose / decision_yes_no / decide_prompt
// ---------------------------------------------------------------------------------------------------------------

//! Options from a VARCHAR[] (names only) or a MAP(VARCHAR, VARCHAR) (name -> description).
static void ReadOptions(const Value &options, decide::DecisionRequest &req) {
	req.labels.clear();
	req.descriptions.clear();
	if (options.type().id() == LogicalTypeId::MAP) {
		for (auto &entry : MapValue::GetChildren(options)) {
			auto &kv = StructValue::GetChildren(entry);
			if (kv[0].IsNull()) {
				throw InvalidInputException("decision_choose: option names cannot be NULL");
			}
			req.labels.push_back(kv[0].ToString());
			req.descriptions.push_back(kv[1].IsNull() ? string() : kv[1].ToString());
		}
	} else {
		for (auto &item : ListValue::GetChildren(options)) {
			if (item.IsNull()) {
				throw InvalidInputException("decision_choose: option names cannot be NULL");
			}
			req.labels.push_back(item.ToString());
			req.descriptions.emplace_back();
		}
	}
	if (req.labels.size() < 2) {
		throw InvalidInputException("decision_choose: at least 2 options are required");
	}
	if (req.labels.size() > (size_t)decide::Prompt::MAX_OPTIONS) {
		throw InvalidInputException("decision_choose: at most %d options are supported",
		                            decide::Prompt::MAX_OPTIONS);
	}
	for (size_t a = 0; a < req.labels.size(); a++) {
		for (size_t b = a + 1; b < req.labels.size(); b++) {
			if (req.labels[a] == req.labels[b]) {
				throw InvalidInputException("decision_choose: duplicate option '%s'", req.labels[a]);
			}
		}
	}
}

enum class DecisionKind { CHOOSE, YES_NO };

//! Build one request per valid row, run them grouped by model, and hand back the probabilities per row.
//! `requests[i]` is empty (no labels) for rows whose result is NULL.
static void RunDecisions(DataChunk &args, ExpressionState &state, DecisionKind kind, idx_t model_col,
                         vector<decide::DecisionRequest> &requests, vector<decide::Probabilities> &answers) {
	auto &context = state.GetContext();
	const idx_t count = args.size();
	const bool has_model = model_col < args.ColumnCount();
	requests.assign(count, decide::DecisionRequest());
	answers.assign(count, decide::Probabilities());

	// model id -> row indices
	std::unordered_map<string, vector<idx_t>> by_model;
	const string session_model = CurrentModelId(context);
	for (idx_t i = 0; i < count; i++) {
		Value state_v = args.data[0].GetValue(i);
		Value question_v = args.data[1].GetValue(i);
		if (state_v.IsNull() || question_v.IsNull()) {
			continue;
		}
		string model_id = session_model;
		if (has_model) {
			Value m = args.data[model_col].GetValue(i);
			if (m.IsNull()) {
				continue;
			}
			model_id = m.ToString();
		}
		auto &req = requests[i];
		if (kind == DecisionKind::CHOOSE) {
			Value options_v = args.data[2].GetValue(i);
			if (options_v.IsNull()) {
				continue;
			}
			req.state = state_v.ToString();
			req.instructions = question_v.ToString();
			ReadOptions(options_v, req);
		} else {
			req = decide::Prompt::YesNo(state_v.ToString(), question_v.ToString());
		}
		by_model[model_id].push_back(i);
	}

	for (auto &group : by_model) {
		auto config = MakeEngineConfig(context, group.first);
		vector<decide::DecisionRequest> batch;
		batch.reserve(group.second.size());
		for (auto i : group.second) {
			batch.push_back(requests[i]);
		}
		vector<decide::Probabilities> probs;
		try {
			probs = decide::Runtime::Get().Decide(config, batch);
		} catch (InvalidInputException &) {
			throw;
		} catch (std::exception &e) {
			throw InvalidInputException("decide: %s", e.what());
		}
		for (idx_t k = 0; k < group.second.size(); k++) {
			answers[group.second[k]] = std::move(probs[k]);
		}
	}
}

static LogicalType ChooseResultType() {
	child_list_t<LogicalType> children;
	children.emplace_back("choice", LogicalType::VARCHAR);
	children.emplace_back("confidence", LogicalType::DOUBLE);
	children.emplace_back("probabilities", LogicalType::MAP(LogicalType::VARCHAR, LogicalType::DOUBLE));
	return LogicalType::STRUCT(children);
}

static void ChooseFun(DataChunk &args, ExpressionState &state, Vector &result) {
	vector<decide::DecisionRequest> requests;
	vector<decide::Probabilities> answers;
	RunDecisions(args, state, DecisionKind::CHOOSE, 3, requests, answers);
	const auto &type = result.GetType();
	for (idx_t i = 0; i < args.size(); i++) {
		auto &p = answers[i];
		if (p.empty()) {
			result.SetValue(i, Value(type));
			continue;
		}
		auto &labels = requests[i].labels;
		size_t best = 0;
		vector<Value> keys, values;
		for (size_t s = 0; s < p.size(); s++) {
			if (p[s] > p[best]) {
				best = s;
			}
			keys.emplace_back(labels[s]);
			values.push_back(Value::DOUBLE(p[s]));
		}
		vector<Value> fields {Value(labels[best]), Value::DOUBLE(p[best]),
		                      Value::MAP(LogicalType::VARCHAR, LogicalType::DOUBLE, std::move(keys), std::move(values))};
		result.SetValue(i, Value::STRUCT(type, std::move(fields)));
	}
}

static void YesNoFun(DataChunk &args, ExpressionState &state, Vector &result) {
	vector<decide::DecisionRequest> requests;
	vector<decide::Probabilities> answers;
	RunDecisions(args, state, DecisionKind::YES_NO, 2, requests, answers);
	for (idx_t i = 0; i < args.size(); i++) {
		result.SetValue(i, answers[i].empty() ? Value(LogicalType::DOUBLE) : Value::DOUBLE(answers[i][0]));
	}
}

//! decide_prompt(state, question, options): the exact text the model sees (for parity tests and debugging).
static void PromptFun(DataChunk &args, ExpressionState &, Vector &result) {
	for (idx_t i = 0; i < args.size(); i++) {
		Value s = args.data[0].GetValue(i), q = args.data[1].GetValue(i), o = args.data[2].GetValue(i);
		if (s.IsNull() || q.IsNull() || o.IsNull()) {
			result.SetValue(i, Value(LogicalType::VARCHAR));
			continue;
		}
		decide::DecisionRequest req;
		req.state = s.ToString();
		req.instructions = q.ToString();
		ReadOptions(o, req);
		result.SetValue(i, Value(decide::Prompt::Render(req)));
	}
}

static void PromptYesNoFun(DataChunk &args, ExpressionState &, Vector &result) {
	for (idx_t i = 0; i < args.size(); i++) {
		Value s = args.data[0].GetValue(i), q = args.data[1].GetValue(i);
		result.SetValue(i, (s.IsNull() || q.IsNull())
		                       ? Value(LogicalType::VARCHAR)
		                       : Value(decide::Prompt::Render(decide::Prompt::YesNo(s.ToString(), q.ToString()))));
	}
}

// ---------------------------------------------------------------------------------------------------------------
// decide_state(any) -> VARCHAR: qev render_state. Text as is; anything else as Python json.dumps(x,
// ensure_ascii=False, indent=2), the layout OneJev saw for structured states in training.
// ---------------------------------------------------------------------------------------------------------------

static void JsonString(const string &s, string &out) {
	out += '"';
	for (unsigned char c : s) {
		switch (c) {
		case '"':
			out += "\\\"";
			break;
		case '\\':
			out += "\\\\";
			break;
		case '\n':
			out += "\\n";
			break;
		case '\r':
			out += "\\r";
			break;
		case '\t':
			out += "\\t";
			break;
		case '\b':
			out += "\\b";
			break;
		case '\f':
			out += "\\f";
			break;
		default:
			if (c < 0x20) {
				char buf[8];
				snprintf(buf, sizeof(buf), "\\u%04x", c);
				out += buf;
			} else {
				out += (char)c; // UTF-8 bytes pass through (ensure_ascii=False)
			}
		}
	}
	out += '"';
}

static void JsonValue(const Value &v, idx_t depth, string &out) {
	auto pad = [&](idx_t d) { out += '\n' + string(d * 2, ' '); };
	if (v.IsNull()) {
		out += "null";
		return;
	}
	auto &type = v.type();
	switch (type.id()) {
	case LogicalTypeId::STRUCT: {
		auto &children = StructValue::GetChildren(v);
		if (children.empty()) {
			out += "{}";
			return;
		}
		out += '{';
		for (idx_t i = 0; i < children.size(); i++) {
			pad(depth + 1);
			JsonString(StructType::GetChildName(type, i).GetIdentifierName(), out);
			out += ": ";
			JsonValue(children[i], depth + 1, out);
			if (i + 1 < children.size()) {
				out += ',';
			}
		}
		pad(depth);
		out += '}';
		return;
	}
	case LogicalTypeId::MAP: {
		auto &entries = MapValue::GetChildren(v);
		if (entries.empty()) {
			out += "{}";
			return;
		}
		out += '{';
		for (idx_t i = 0; i < entries.size(); i++) {
			auto &kv = StructValue::GetChildren(entries[i]);
			pad(depth + 1);
			JsonString(kv[0].ToString(), out);
			out += ": ";
			JsonValue(kv[1], depth + 1, out);
			if (i + 1 < entries.size()) {
				out += ',';
			}
		}
		pad(depth);
		out += '}';
		return;
	}
	case LogicalTypeId::LIST:
	case LogicalTypeId::ARRAY: {
		auto &items = type.id() == LogicalTypeId::LIST ? ListValue::GetChildren(v) : ArrayValue::GetChildren(v);
		if (items.empty()) {
			out += "[]";
			return;
		}
		out += '[';
		for (idx_t i = 0; i < items.size(); i++) {
			pad(depth + 1);
			JsonValue(items[i], depth + 1, out);
			if (i + 1 < items.size()) {
				out += ',';
			}
		}
		pad(depth);
		out += ']';
		return;
	}
	case LogicalTypeId::BOOLEAN:
		out += BooleanValue::Get(v) ? "true" : "false";
		return;
	case LogicalTypeId::TINYINT:
	case LogicalTypeId::SMALLINT:
	case LogicalTypeId::INTEGER:
	case LogicalTypeId::BIGINT:
	case LogicalTypeId::HUGEINT:
	case LogicalTypeId::UTINYINT:
	case LogicalTypeId::USMALLINT:
	case LogicalTypeId::UINTEGER:
	case LogicalTypeId::UBIGINT:
	case LogicalTypeId::UHUGEINT:
	case LogicalTypeId::FLOAT:
	case LogicalTypeId::DOUBLE:
	case LogicalTypeId::DECIMAL:
		out += v.ToString();
		return;
	default:
		JsonString(v.ToString(), out); // text, dates, timestamps, uuids... as JSON strings
	}
}

static void StateFun(DataChunk &args, ExpressionState &, Vector &result) {
	auto &input = args.data[0];
	const bool is_text = input.GetType().id() == LogicalTypeId::VARCHAR;
	for (idx_t i = 0; i < args.size(); i++) {
		Value v = input.GetValue(i);
		if (v.IsNull()) {
			result.SetValue(i, Value(LogicalType::VARCHAR));
			continue;
		}
		if (is_text) {
			result.SetValue(i, v);
			continue;
		}
		string out;
		JsonValue(v, 0, out);
		result.SetValue(i, Value(out));
	}
}

static void ClearCacheFun(DataChunk &args, ExpressionState &, Vector &result) {
	decide::Runtime::Get().ClearCache();
	decide::Runtime::Get().ResetStats();
	result.SetVectorType(VectorType::CONSTANT_VECTOR);
	ConstantVector::GetData<bool>(result)[0] = true;
}

// ---------------------------------------------------------------------------------------------------------------
// decide_stats() -> one row with the engine counters
// ---------------------------------------------------------------------------------------------------------------

struct OneRowState : public GlobalTableFunctionState {
	bool done = false;
};

static unique_ptr<FunctionData> StatsBind(ClientContext &, TableFunctionBindInput &, vector<LogicalType> &types,
                                          vector<Identifier> &names) {
	names = {"decisions",     "cache_hits", "deduplicated", "evaluated", "prompt_tokens", "truncated",
	         "process_calls", "seconds",    "tokens_per_s", "decisions_per_s", "loaded_model"};
	types = {LogicalType::UBIGINT, LogicalType::UBIGINT, LogicalType::UBIGINT, LogicalType::UBIGINT,
	         LogicalType::UBIGINT, LogicalType::UBIGINT, LogicalType::UBIGINT, LogicalType::DOUBLE,
	         LogicalType::DOUBLE,  LogicalType::DOUBLE,  LogicalType::VARCHAR};
	return make_uniq<TableFunctionData>();
}

static unique_ptr<GlobalTableFunctionState> OneRowInit(ClientContext &, TableFunctionInitInput &) {
	return make_uniq<OneRowState>();
}

static void StatsFun(ClientContext &, TableFunctionInput &input, DataChunk &output) {
	auto &state = input.global_state->Cast<OneRowState>();
	if (state.done) {
		output.SetCardinality(0);
		return;
	}
	state.done = true;
	auto s = decide::Runtime::Get().Stats();
	idx_t c = 0;
	output.SetValue(c++, 0, Value::UBIGINT(s.decisions));
	output.SetValue(c++, 0, Value::UBIGINT(s.cache_hits));
	output.SetValue(c++, 0, Value::UBIGINT(s.deduplicated));
	output.SetValue(c++, 0, Value::UBIGINT(s.evaluated));
	output.SetValue(c++, 0, Value::UBIGINT(s.prompt_tokens));
	output.SetValue(c++, 0, Value::UBIGINT(s.truncated));
	output.SetValue(c++, 0, Value::UBIGINT(s.process_calls));
	output.SetValue(c++, 0, Value::DOUBLE(s.seconds));
	output.SetValue(c++, 0, Value::DOUBLE(s.seconds > 0 ? s.prompt_tokens / s.seconds : 0));
	output.SetValue(c++, 0, Value::DOUBLE(s.seconds > 0 ? s.evaluated / s.seconds : 0));
	auto info = decide::Runtime::Get().LoadedInfo();
	output.SetValue(c++, 0, info.empty() ? Value(LogicalType::VARCHAR) : Value(info));
	output.SetCardinality(1);
}

// ---------------------------------------------------------------------------------------------------------------
// decide_version() -> VARCHAR
// ---------------------------------------------------------------------------------------------------------------

static string ExtensionVersion() {
#ifdef EXT_VERSION_DECIDE
	return EXT_VERSION_DECIDE;
#else
	return "dev";
#endif
}

static void VersionFun(DataChunk &args, ExpressionState &, Vector &result) {
	auto text = "decide " + ExtensionVersion() + " | llama.cpp " + decide::Runtime::LlamaBuild() +
	            " | backends: " + decide::Runtime::Backends();
	result.SetVectorType(VectorType::CONSTANT_VECTOR);
	ConstantVector::GetData<string_t>(result)[0] = StringVector::AddString(result, text.data(), text.size());
}

// ---------------------------------------------------------------------------------------------------------------
// decide_models() -> one row per known model
// ---------------------------------------------------------------------------------------------------------------

struct ModelsState : public GlobalTableFunctionState {
	idx_t offset = 0;
};

static unique_ptr<FunctionData> ModelsBind(ClientContext &, TableFunctionBindInput &, vector<LogicalType> &types,
                                           vector<Identifier> &names) {
	names = {"model",   "is_default", "is_current", "family",     "size",   "quant",  "file_mb",
	         "license", "notes",      "url",        "sha256",     "local_path", "downloaded", "loaded"};
	types = {LogicalType::VARCHAR, LogicalType::BOOLEAN, LogicalType::BOOLEAN, LogicalType::VARCHAR,
	         LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::DOUBLE,  LogicalType::VARCHAR,
	         LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR,
	         LogicalType::BOOLEAN, LogicalType::BOOLEAN};
	return make_uniq<TableFunctionData>();
}

static unique_ptr<GlobalTableFunctionState> ModelsInit(ClientContext &, TableFunctionInitInput &) {
	return make_uniq<ModelsState>();
}

static void ModelsFun(ClientContext &context, TableFunctionInput &input, DataChunk &output) {
	auto &state = input.global_state->Cast<ModelsState>();
	auto &models = decide::Models();
	auto current = CurrentModelId(context);
	auto dir = GetStringSetting(context, SETTING_MODEL_DIR, decide::DefaultModelDir());
	auto override_path = GetStringSetting(context, SETTING_MODEL_PATH, "");
	idx_t row = 0;
	for (; state.offset < models.size() && row < STANDARD_VECTOR_SIZE; state.offset++, row++) {
		auto &m = models[state.offset];
		bool is_current = current == m.id;
		string path = (is_current && !override_path.empty()) ? override_path : decide::ResolveModelFile(m, dir);
		idx_t c = 0;
		output.SetValue(c++, row, Value(m.id));
		output.SetValue(c++, row, Value::BOOLEAN(m.is_default));
		output.SetValue(c++, row, Value::BOOLEAN(is_current));
		output.SetValue(c++, row, Value(m.family));
		output.SetValue(c++, row, Value(m.size_label));
		output.SetValue(c++, row, Value(m.quant));
		output.SetValue(c++, row, Value::DOUBLE(double(m.size_bytes) / (1024.0 * 1024.0)));
		output.SetValue(c++, row, Value(m.license));
		output.SetValue(c++, row, Value(m.notes));
		output.SetValue(c++, row, Value(decide::DownloadUrl(m)));
		output.SetValue(c++, row, Value(m.sha256));
		output.SetValue(c++, row, path.empty() ? Value(LogicalType::VARCHAR) : Value(path));
		output.SetValue(c++, row, Value::BOOLEAN(!path.empty()));
		output.SetValue(c++, row, Value::BOOLEAN(!path.empty() && decide::Runtime::Get().IsLoaded(path)));
	}
	output.SetCardinality(row);
}

// ---------------------------------------------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------------------------------------------

static void LoadInternal(ExtensionLoader &loader) {
	auto &config = DBConfig::GetConfig(loader.GetDatabaseInstance());
	config.AddExtensionOption(SETTING_MODEL,
	                          "Decision model used by the decide functions: " + decide::ModelIdList(),
	                          LogicalType::VARCHAR, Value(decide::DefaultModel().id), ValidateModelSetting);
	config.AddExtensionOption(SETTING_MODEL_DIR, "Directory holding the GGUF files (<dir>/<file> or <dir>/<model>/<file>)",
	                          LogicalType::VARCHAR, Value(decide::DefaultModelDir()));
	config.AddExtensionOption(SETTING_MODEL_PATH,
	                          "Explicit GGUF file for the current decide_model (overrides decide_model_dir; empty = off)",
	                          LogicalType::VARCHAR, Value(""));

	// DuckDB 2.0: overloads become immutable once inside a FunctionSet, so set properties before adding them.
	ScalarFunctionSet tokenize("decide_tokenize");
	for (auto args : {vector<LogicalType> {LogicalType::VARCHAR},
	                  vector<LogicalType> {LogicalType::VARCHAR, LogicalType::VARCHAR}}) {
		ScalarFunction f(args, LogicalType::LIST(LogicalType::INTEGER), TokenizeFun);
		f.SetVolatile(); // depends on session settings (decide_model, decide_model_dir)
		f.SetFallible();  // DuckDB 2.0: functions that may throw must say so
		tokenize.AddFunction(std::move(f));
	}
	loader.RegisterFunction(tokenize);

	ScalarFunction version("decide_version", vector<LogicalType>(), LogicalType::VARCHAR, VersionFun);
	loader.RegisterFunction(version);

	TableFunction models("decide_models", vector<LogicalType>(), ModelsFun, ModelsBind, ModelsInit);
	loader.RegisterFunction(models);

	// --- performance / engine settings ---
	config.AddExtensionOption(SETTING_PARALLEL, "Sequences evaluated together per llama.cpp call (GPU throughput)",
	                          LogicalType::BIGINT, Value::BIGINT(16), ValidatePositive);
	config.AddExtensionOption(SETTING_BATCH_TOKENS, "Max tokens per llama.cpp call; also the shared KV cache size",
	                          LogicalType::BIGINT, Value::BIGINT(4096), ValidatePositive);
	config.AddExtensionOption(SETTING_GPU_LAYERS, "Model layers offloaded to the GPU (-1 = all, 0 = CPU only)",
	                          LogicalType::BIGINT, Value::BIGINT(-1));
	config.AddExtensionOption(SETTING_THREADS, "CPU threads for llama.cpp (0 = all cores)", LogicalType::BIGINT,
	                          Value::BIGINT(0));
	config.AddExtensionOption(SETTING_KV_CACHE, "KV cache type: 'f16' or 'q8_0' (less VRAM)", LogicalType::VARCHAR,
	                          Value("f16"), ValidateKvCache);
	config.AddExtensionOption(SETTING_MAX_TOKENS, "Max tokens per prompt; longer texts are truncated",
	                          LogicalType::BIGINT, Value::BIGINT(1024), ValidatePositive);
	config.AddExtensionOption(SETTING_CACHE_SIZE, "Answers kept in the in-memory LRU cache (0 = off)",
	                          LogicalType::BIGINT, Value::BIGINT(100000));

	// --- decisions ---
	auto options_types = {LogicalType::LIST(LogicalType::VARCHAR),
	                      LogicalType::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR)};
	ScalarFunctionSet choose("decision_choose");
	for (auto &opt : options_types) {
		for (bool with_model : {false, true}) {
			vector<LogicalType> args {LogicalType::VARCHAR, LogicalType::VARCHAR, opt};
			if (with_model) {
				args.push_back(LogicalType::VARCHAR);
			}
			ScalarFunction f(args, ChooseResultType(), ChooseFun);
			f.SetVolatile();
			f.SetFallible();
			choose.AddFunction(std::move(f));
		}
	}
	loader.RegisterFunction(choose);

	ScalarFunctionSet yes_no("decision_yes_no");
	for (bool with_model : {false, true}) {
		vector<LogicalType> args {LogicalType::VARCHAR, LogicalType::VARCHAR};
		if (with_model) {
			args.push_back(LogicalType::VARCHAR);
		}
		ScalarFunction f(args, LogicalType::DOUBLE, YesNoFun);
		f.SetVolatile();
		f.SetFallible();
		yes_no.AddFunction(std::move(f));
	}
	loader.RegisterFunction(yes_no);

	ScalarFunctionSet prompt("decide_prompt");
	for (auto &opt : options_types) {
		ScalarFunction f({LogicalType::VARCHAR, LogicalType::VARCHAR, opt}, LogicalType::VARCHAR, PromptFun);
		f.SetFallible(); // invalid options throw
		prompt.AddFunction(std::move(f));
	}
	prompt.AddFunction(ScalarFunction({LogicalType::VARCHAR, LogicalType::VARCHAR}, LogicalType::VARCHAR, PromptYesNoFun));
	loader.RegisterFunction(prompt);

	ScalarFunction clear_cache("decide_clear_cache", vector<LogicalType>(), LogicalType::BOOLEAN, ClearCacheFun);
	clear_cache.SetVolatile();
	loader.RegisterFunction(clear_cache);

	TableFunction stats("decide_stats", vector<LogicalType>(), StatsFun, StatsBind, OneRowInit);
	loader.RegisterFunction(stats);

	config.AddExtensionOption("decide_auto_download",
	                          "Download missing models from Hugging Face on first use (SHA-256 verified)",
	                          LogicalType::BOOLEAN, Value::BOOLEAN(true));
	ScalarFunctionSet download("decide_download");
	for (auto args : {vector<LogicalType>(), vector<LogicalType> {LogicalType::VARCHAR}}) {
		ScalarFunction f(args, LogicalType::VARCHAR, DownloadFun);
		f.SetVolatile();
		f.SetFallible();
		download.AddFunction(std::move(f));
	}
	loader.RegisterFunction(download);

	ScalarFunction state_fn("decide_state", {LogicalType::ANY}, LogicalType::VARCHAR, StateFun);
	state_fn.SetFallible();
	loader.RegisterFunction(state_fn);

	// FROM t DECIDE (...), decision_choose(q AMONG (...) GIVEN ...), decision_yes_no(q GIVEN ...)
	decide_grammar::RegisterDecideGrammar(loader.GetDatabaseInstance());
}

void DecideExtension::Load(ExtensionLoader &loader) {
	LoadInternal(loader);
}

std::string DecideExtension::Name() {
	return "decide";
}

std::string DecideExtension::Version() const {
	return ExtensionVersion();
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(decide, loader) {
	duckdb::LoadInternal(loader);
}
}
