#!/bin/bash -e

# Compiles every .metal file in the provided directory with the iOS Metal compiler.
# The runtime compiles the generated shaders with MSL 2.4 (iOS 16), use the same version here.
# Every shader is compiled; distinct errors are summarized at the end so a single run reports
# all generator problems.

SHADER_DIR="$1"
LOG_DIR="$(mktemp -d)"
failures=0
count=0
shown=0
for shader in "$SHADER_DIR"/*.metal; do
    count=$((count + 1))
    name=$(basename "$shader")
    if ! xcrun -sdk iphoneos metal -std=ios-metal2.4 -Werror=return-type -c "$shader" \
            -o /dev/null 2> "$LOG_DIR/$name.log"; then
        failures=$((failures + 1))
        echo "::error file=$shader::Failed to compile $name"
        if [ $shown -lt 5 ]; then
            grep -A3 "error:" "$LOG_DIR/$name.log" | head -n 40
            shown=$((shown + 1))
        fi
    fi
done

echo "Compiled $count shaders, $failures failures"
if [ $failures -ne 0 ]; then
    echo "Distinct errors (count, message, example file):"
    grep -h "error:" "$LOG_DIR"/*.log | sed -E 's/^[^:]*:[0-9]+:[0-9]+: //' | sort | uniq -c \
        | sort -rn | head -n 40
    for message in $(grep -h "error:" "$LOG_DIR"/*.log | sed -E 's/^[^:]*:[0-9]+:[0-9]+: //' \
            | sort -u | head -n 20 | tr ' ' '\001'); do
        message=$(echo "$message" | tr '\001' ' ')
        example=$(grep -l -F "$message" "$LOG_DIR"/*.log | head -n 1)
        echo "--- $message"
        grep -B1 -A3 -F "$message" "$example" | head -n 8
    done
fi
[ $failures -eq 0 ]
