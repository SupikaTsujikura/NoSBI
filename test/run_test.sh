#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
KERNEL_ELF="${1:-$ROOT_DIR/target-test/mos-riscv.elf}"
QEMU_BIN="${QEMU:-qemu-system-riscv64}"

set +e
/usr/bin/timeout 6s "$QEMU_BIN" \
  -machine virt \
  -m 2G \
  -nographic \
  -bios default \
  -kernel "$KERNEL_ELF"
status=$?
set -e

case "$status" in
  0)
    echo "PASS: kernel self-test completed and QEMU exited cleanly"
    ;;
  124)
    echo "FAIL: test kernel did not terminate before timeout" >&2
    exit 1
    ;;
  *)
    echo "FAIL: QEMU exited unexpectedly with status $status" >&2
    exit 1
    ;;
esac
