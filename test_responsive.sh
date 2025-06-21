#!/bin/bash
echo "Testing Openterface responsiveness and video display..."
echo "Starting application in background..."

# Run the application in background
/home/bresilla/code/openterface/build/main connect --video=/dev/video0 --serial=/dev/ttyUSB0 &
APP_PID=$!

echo "Application PID: $APP_PID"
echo "Waiting 5 seconds to check if window is responsive..."
sleep 5

# Check if process is still running
if kill -0 $APP_PID 2>/dev/null; then
    echo "✓ Application is still running"
    echo "Check if window manager shows 'not responding' dialog"
    echo "Press Ctrl+C to stop the test"
    
    # Wait for user to terminate
    wait $APP_PID
else
    echo "✗ Application crashed or was killed"
fi