#!/bin/bash
# Test all DigiDollar functional tests and report results

PASS_COUNT=0
FAIL_COUNT=0
PASS_TESTS=()
FAIL_TESTS=()

echo "========================================="
echo "Testing All DigiDollar Functional Tests"
echo "========================================="
echo ""

for test in test/functional/digidollar_*.py; do
    test_name=$(basename "$test")
    echo -n "Testing $test_name ... "

    if timeout 120 python3 "$test" > /tmp/dd_test_output.log 2>&1; then
        echo "✅ PASS"
        PASS_COUNT=$((PASS_COUNT + 1))
        PASS_TESTS+=("$test_name")
    else
        echo "❌ FAIL"
        FAIL_COUNT=$((FAIL_COUNT + 1))
        FAIL_TESTS+=("$test_name")
        # Show last few lines of error
        tail -3 /tmp/dd_test_output.log | grep -E "Error|Exception|Assertion" || echo "  (timeout or other error)"
    fi
done

echo ""
echo "========================================="
echo "SUMMARY"
echo "========================================="
echo "Total Tests: $((PASS_COUNT + FAIL_COUNT))"
echo "✅ Passing: $PASS_COUNT"
echo "❌ Failing: $FAIL_COUNT"
echo ""

if [ $PASS_COUNT -gt 0 ]; then
    echo "Passing tests:"
    for test in "${PASS_TESTS[@]}"; do
        echo "  ✅ $test"
    done
    echo ""
fi

if [ $FAIL_COUNT -gt 0 ]; then
    echo "Failing tests:"
    for test in "${FAIL_TESTS[@]}"; do
        echo "  ❌ $test"
    done
fi

echo ""
echo "Success rate: $(( PASS_COUNT * 100 / (PASS_COUNT + FAIL_COUNT) ))%"
