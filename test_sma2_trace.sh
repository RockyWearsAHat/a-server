#!/bin/bash
./build/bin/AIOServer -r SMA2.gba 2>&1 | head -500 &
PID=$!
sleep 15
kill $PID 2>/dev/null
wait $PID 2>/dev/null
