#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
KERNEL_ELF="${1:-$ROOT_DIR/target/mos-riscv.elf}"
QEMU_BIN="${QEMU:-qemu-system-riscv64}"
VALIDATOR="${VALIDATOR:-$ROOT_DIR/test/validate-output}"
LOG_DIR="$ROOT_DIR/target/test"
LOG_FILE="$LOG_DIR/qemu.log"

mkdir -p "$LOG_DIR"
rm -f "$LOG_FILE"

set +e
/usr/bin/timeout 4s "$QEMU_BIN" \
  -machine virt \
  -m 2G \
  -nographic \
  -bios default \
  -kernel "$KERNEL_ELF" \
  >"$LOG_FILE" 2>&1
status=$?
set -e

if [[ $status -ne 0 && $status -ne 124 ]]; then
  echo "QEMU exited unexpectedly with status $status" >&2
  cat "$LOG_FILE" >&2
  exit 1
fi

"$VALIDATOR" "$LOG_FILE"
