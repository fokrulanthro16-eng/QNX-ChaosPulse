#!/usr/bin/env bash
# ==============================================================================
# QNX-ChaosPulse // Headless QEMU Runner with Networking Port Forwarding
# Forwards Target Daemon TCP 9092 -> Host TCP 9092, SSH 22 -> Host 2222
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

ARCH="${1:-x86_64}"
IMAGE_PATH="${QNX_IMAGE:-${REPO_ROOT}/qnx800_virtual.qcow2}"
CORES="${SMP_CORES:-4}"
RAM="${VM_RAM:-4096M}"

echo "==================================================================="
echo "  QNX-CHAOSPULSE // QEMU SDP 8.0 VIRTUAL TARGET RUNNER"
echo "  Architecture:   ${ARCH}"
echo "  SMP Cores:      ${CORES}"
echo "  RAM:            ${RAM}"
echo "  Port Forwards:  TCP 9092 (Telemetry) -> Host 9092"
echo "                  TCP 22   (SSH)       -> Host 2222"
echo "                  TCP 80   (HTTP)      -> Host 8080"
echo "==================================================================="

# Detect QEMU Binary
if [ "${ARCH}" = "x86_64" ]; then
    QEMU_BIN="qemu-system-x86_64"
    MACHINE_OPTS="-machine q35 -cpu host,migratable=no"
elif [ "${ARCH}" = "aarch64" ]; then
    QEMU_BIN="qemu-system-aarch64"
    MACHINE_OPTS="-machine virt,gic-version=3 -cpu cortex-a72"
else
    echo "[ERROR] Unsupported architecture: ${ARCH}. Use 'x86_64' or 'aarch64'."
    exit 1
fi

if ! command -v "${QEMU_BIN}" >/dev/null 2>&1; then
    echo "[ERROR] ${QEMU_BIN} is not installed or not in PATH."
    echo "Install QEMU or run the zero-dependency Host Mock mode via: npm start (in host/)"
    exit 1
fi

# Detect Acceleration (KVM on Linux, HVF on macOS)
ACCEL_OPTS=""
if [ "$(uname -s)" = "Linux" ] && [ -e /dev/kvm ] && [ -r /dev/kvm ]; then
    echo "[INFO] Enabling Linux KVM hardware acceleration."
    ACCEL_OPTS="-enable-kvm"
elif [ "$(uname -s)" = "Darwin" ]; then
    echo "[INFO] Enabling macOS Hypervisor Framework (HVF)."
    ACCEL_OPTS="-accel hvf"
fi

if [ ! -f "${IMAGE_PATH}" ]; then
    echo "[WARN] Target disk image not found at: ${IMAGE_PATH}"
    echo "Running in QEMU test stub mode without backing disk, or provide QNX_IMAGE=/path/to/image.qcow2"
    DISK_OPTS=""
else
    DISK_OPTS="-drive file=${IMAGE_PATH},if=virtio,format=qcow2"
fi

echo "[INFO] Starting QEMU instance..."

exec ${QEMU_BIN} \
    ${MACHINE_OPTS} \
    ${ACCEL_OPTS} \
    -smp "${CORES}" \
    -m "${RAM}" \
    ${DISK_OPTS} \
    -netdev user,id=net0,hostfwd=tcp::9092-:9092,hostfwd=tcp::2222-:22,hostfwd=tcp::8080-:80 \
    -device virtio-net-pci,netdev=net0 \
    -nographic \
    -serial mon:stdio
