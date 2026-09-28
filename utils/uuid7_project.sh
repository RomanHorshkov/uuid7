#!/usr/bin/env bash
# shellcheck shell=bash
# uuid7_project.sh — what the ABI gate (utils/check_abi.sh, kyr-build share/abi) treats as uuid7's public ABI: the
# one public header app/uuid7.h, and the prefixes of its types, functions and numeric constants. The build scripts do not read
# this file; the linker map's node is UUID7_<major>.
KYR_LIB="uuid7"
KYR_ABI_NAME="uuid7"
KYR_ABI_INCLUDE_DIR="app"
KYR_ABI_UMBRELLA="uuid7.h"
KYR_ABI_TYPE_PREFIX="uuid7_"
KYR_ABI_FUNC_PREFIX="uuid7_"
KYR_ABI_CONST_PREFIX="UUID7_"
KYR_ABI_NODE="UUID7"
