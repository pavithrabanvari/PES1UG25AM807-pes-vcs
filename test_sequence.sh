#!/usr/bin/env bash
# test_sequence.sh — End-to-end integration test
# PROVIDED — do not modify

set -euo pipefail
PASS=0; FAIL=0

check() {
    local label="$1"; shift
    if "$@" &>/dev/null; then
        echo "PASS: $label"; ((PASS++))
    else
        echo "FAIL: $label"; ((FAIL++))
    fi
}

check_output() {
    local label="$1"; local expected="$2"; shift 2
    local actual
    actual=$("$@" 2>&1) || true
    if echo "$actual" | grep -qF "$expected"; then
        echo "PASS: $label"; ((PASS++))
    else
        echo "FAIL: $label (expected '$expected', got '$actual')"; ((FAIL++))
    fi
}

echo "=== Running integration tests ==="

# clean slate
rm -rf .pes file.txt hello.txt bye.txt

echo "--- Repository Initialization ---"
./pes init
check     ".pes/objects exists"    test -d .pes/objects
check     ".pes/refs/heads exists" test -d .pes/refs/heads
check     ".pes/HEAD exists"       test -f .pes/HEAD

echo "--- Staging Files ---"
echo "file content" > file.txt
echo "hello world"  > hello.txt
./pes add file.txt hello.txt
check_output "Status after add" "staged:" ./pes status

echo "--- First Commit ---"
export PES_AUTHOR="Your Name <PES1UG24AM129>"
./pes commit -m "Initial commit"
check_output "First commit in log" "Initial commit" ./pes log

echo "Log after first commit:"
./pes log

echo "--- Second Commit ---"
echo "updated" >> file.txt
./pes add file.txt
./pes commit -m "Update file.txt"
check_output "Second commit in log" "Update file.txt" ./pes log

echo "--- Third Commit ---"
echo "farewell" > bye.txt
./pes add bye.txt
./pes commit -m "Add farewell"
check_output "Third commit in log" "Add farewell" ./pes log

echo ""
echo "--- Full History ---"
./pes log

echo ""
echo "--- Reference Chain ---"
echo "HEAD:"
cat .pes/HEAD
echo "refs/heads/main:"
cat .pes/refs/heads/main

echo ""
echo "--- Object Store ---"
echo "Objects created:"
find .pes/objects -type f | wc -l
find .pes/objects -type f | sort

echo ""
if [ "$FAIL" -eq 0 ]; then
    echo "=== All integration tests completed ==="
else
    echo "=== $FAIL test(s) failed ==="
    exit 1
fi
