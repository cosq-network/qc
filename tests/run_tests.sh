#!/bin/bash
QC=../build/qc
FAILED=0

for f in *.c; do
  if [[ "$f" == "test_stdqc_c.c" ]]; then continue; fi
  if [[ "$f" == *"fail"* ]]; then
    echo "Testing $f (expecting failure)..."
    $QC "$f" -o "${f%.c}.exe" > /dev/null 2>&1
    if [ $? -eq 0 ]; then
      echo "ERROR: $f passed but should have failed!"
      FAILED=$((FAILED + 1))
    else
      echo "SUCCESS: $f failed as expected."
    fi
  else
    echo "Testing $f..."
    $QC "$f" -o "${f%.c}.exe"
    if [ $? -ne 0 ]; then
      echo "ERROR: $f failed to compile/link!"
      FAILED=$((FAILED + 1))
    else
      ./"${f%.c}.exe"
      if [ $? -ne 0 ] && [[ "$f" != "hello.c" ]]; then
        echo "ERROR: $f failed to run correctly!"
        FAILED=$((FAILED + 1))
      else
        echo "SUCCESS: $f compiled and ran."
      fi
    fi
  fi
done

if [ $FAILED -eq 0 ]; then
  echo "All basic tests passed!"
else
  echo "$FAILED basic tests failed."
fi

echo "------------------------------------------------"
echo "Verifying stdqc library with qc..."
echo "------------------------------------------------"

STDQC_FAILED=0

# Verify C library using qc as driver
$QC -I../stdqc/include/c test_stdqc_c.c ../build/stdqc/libstdqc.a -o test_stdqc_c_qc
if [ $? -ne 0 ]; then
    echo "ERROR: Failed to compile/link stdqc C test with qc"
    STDQC_FAILED=$((STDQC_FAILED + 1))
else
    ./test_stdqc_c_qc
    if [ $? -ne 0 ]; then
        echo "ERROR: stdqc C test failed to run"
        STDQC_FAILED=$((STDQC_FAILED + 1))
    else
        echo "SUCCESS: stdqc C test passed (via qc)."
    fi
fi

# Verify C++ library
g++ -fno-exceptions -fno-rtti -I../stdqc/include/c -I../stdqc/include/cxx test_stdqc_cpp.cpp ../build/stdqc/libstdqc.a -o test_stdqc_cpp
if [ $? -ne 0 ]; then
    echo "ERROR: Failed to compile stdqc C++ test"
    STDQC_FAILED=$((STDQC_FAILED + 1))
else
    ./test_stdqc_cpp
    if [ $? -ne 0 ]; then
        echo "ERROR: stdqc C++ test failed to run"
        STDQC_FAILED=$((STDQC_FAILED + 1))
    else
        echo "SUCCESS: stdqc C++ test passed."
    fi
fi

if [ $STDQC_FAILED -eq 0 ]; then
  echo "All stdqc tests passed!"
  exit $((FAILED))
else
  echo "$STDQC_FAILED stdqc tests failed."
  exit $((FAILED + STDQC_FAILED))
fi
