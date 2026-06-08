#!/bin/bash
QC=../build/qc
FAILED=0

for f in *.c; do
  if [[ "$f" == *"fail"* ]]; then
    echo "Testing $f (expecting failure)..."
    $QC "$f" -o "${f%.c}.o" > /dev/null 2>&1
    if [ $? -eq 0 ]; then
      echo "ERROR: $f passed but should have failed!"
      FAILED=$((FAILED + 1))
    else
      echo "SUCCESS: $f failed as expected."
    fi
  else
    echo "Testing $f..."
    $QC "$f" -o "${f%.c}.o"
    if [ $? -ne 0 ]; then
      echo "ERROR: $f failed to compile!"
      FAILED=$((FAILED + 1))
    else
      echo "SUCCESS: $f compiled."
    fi
  fi
done

if [ $FAILED -eq 0 ]; then
  echo "All tests passed!"
  exit 0
else
  echo "$FAILED tests failed."
  exit 1
fi
