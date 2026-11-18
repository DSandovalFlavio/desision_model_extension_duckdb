#pragma once

#include "decide_models.hpp"

#include <string>

namespace duckdb {
class ClientContext;

namespace decide_store {

//! Local, verified GGUF for `spec`:
//!  1. a file already under `dir` (<dir>/<file> or <dir>/<id>/<file>) is SHA-256 checked once (a ".verified" stamp
//!     with size and mtime next to it skips the hash afterwards, also across processes);
//!  2. otherwise, if `auto_download`, it is downloaded from Hugging Face through httpfs into <dir>/<id>/<file>
//!     (streamed to a temporary file, hashed while downloading, renamed only when the hash matches).
//! Thread-safe: concurrent callers wait for a single download.
std::string EnsureModelFile(ClientContext &context, const decide::ModelSpec &spec, const std::string &dir,
                            bool auto_download);

//! Lower-case hex SHA-256 of a byte buffer (exposed for tests).
std::string Sha256Hex(const std::string &bytes);

} // namespace decide_store
} // namespace duckdb
