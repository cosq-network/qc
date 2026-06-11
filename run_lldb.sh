#!/bin/bash
lldb build/qc -b -o "run" -o "bt" -o "quit" -- tests/test_raii_temp.cpp -fsyntax-only
