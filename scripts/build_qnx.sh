#!/usr/bin/env bash
# ==============================================================================
# QNX-ChaosPulse // Multi-Target Cross-Compilation Script
# Targets: QNX Neutrino 8.0 x86_64, aarch64le, and Host POSIX Fallback
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
TARGET_DIR="${REPO_ROOT}/target"
BIN_DIR="${REPO_ROOT}/build"

mkdir -p "${BIN_DIR}"

CFLAGS_COMMON="-O3 -Wall -Wextra -pthread -I${TARGET_DIR}/include"
SRC_FILES="${TARGET_DIR}/src/cycle_telemetry.c ${TARGET_DIR}/src/pulse_interceptor.c ${TARGET_DIR}/src/cpu_storm_generator.c ${TARGET_DIR}/src/chaospulse_daemon.c"

echo "==================================================================="
echo "  QNX-CHAOSPULSE // SDP 8.0 MULTI-TARGET BUILD PIPELINE"
echo "==================================================================="

BUILD_TARGET="${1:-all}"

build_qnx_x86_64() {
    echo "[BUILD] Compiling for QNX Neutrino 8.0 (x86_64)..."
    if command -v qcc >/dev/null 2>&1; then
        qcc -Vgcc_ntox86_64 ${CFLAGS_COMMON} -D__QNXNTO__ -lm ${SRC_FILES} -o "${BIN_DIR}/chaospulse_daemon_x86_64"
        echo "  --> SUCCESS: ${BIN_DIR}/chaospulse_daemon_x86_64"
    else
        echo "  [WARN] 'qcc' not found in PATH. Ensure QNX SDP 8.0 environment is sourced (qnxsdp-env.sh)."
    fi
}

build_qnx_aarch64le() {
    echo "[BUILD] Compiling for QNX Neutrino 8.0 (aarch64le)..."
    if command -v qcc >/dev/null 2>&1; then
        qcc -Vgcc_ntoaarch64le ${CFLAGS_COMMON} -D__QNXNTO__ -lm ${SRC_FILES} -o "${BIN_DIR}/chaospulse_daemon_aarch64le"
        echo "  --> SUCCESS: ${BIN_DIR}/chaospulse_daemon_aarch64le"
    else
        echo "  [WARN] 'qcc' not found in PATH. Ensure QNX SDP 8.0 environment is sourced (qnxsdp-env.sh)."
    fi
}

build_host_posix() {
    echo "[BUILD] Compiling Host POSIX Fallback Binary..."
    CC_HOST="gcc"
    if ! command -v gcc >/dev/null 2>&1; then
        if command -v clang >/dev/null 2>&1; then
            CC_HOST="clang"
        else
            echo "  [ERROR] Neither gcc nor clang found on host."
            return 1
        fi
    fi

    ${CC_HOST} ${CFLAGS_COMMON} -lm ${SRC_FILES} -o "${BIN_DIR}/chaospulse_daemon_host"
    echo "  --> SUCCESS: ${BIN_DIR}/chaospulse_daemon_host (compiled with ${CC_HOST})"
}

case "${BUILD_TARGET}" in
    qnx-x86_64)
        build_qnx_x86_64
        ;;
    qnx-aarch64)
        build_qnx_aarch64le
        ;;
    host)
        build_host_posix
        ;;
    all)
        build_host_posix || true
        build_qnx_x86_64 || true
        build_qnx_aarch64le || true
        ;;
    *)
        echo "Usage: $0 [all|qnx-x86_64|qnx-aarch64|host]"
        exit 1
        ;;
esac

echo "==================================================================="
echo "  Build finished. Binaries located in ${BIN_DIR}"
echo "==================================================================="
