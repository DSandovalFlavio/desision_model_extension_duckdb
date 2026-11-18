PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Configuration of extension
EXT_NAME=decide
EXT_CONFIG=${PROJ_DIR}extension_config.cmake

# Include the Makefile from extension-ci-tools (Linux/macOS). On Windows use scripts/build_windows.ps1.
include extension-ci-tools/makefiles/duckdb_extension.Makefile
