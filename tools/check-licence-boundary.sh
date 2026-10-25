#!/bin/sh
# Keeps pump MACHINE's GPL-3.0 code out of every other plugin (see /LICENSE and plugins/machine/NOTICE.md).
#   sh tools/check-licence-boundary.sh        exit 0 when clean, 1 with a list of offending files
#
# Two rules:
#   1. Nothing in common/ or in another plugin's folder may include, link or name MACHINE's sources.
#   2. MACHINE must declare its own licence (getLicense() returns GPL-3.0-only).
# The build, the test list, the docs and the CI packaging legitimately mention MACHINE and are not checked.

cd "$(dirname "$0")/.." || exit 2
fail=0

# Rule 1: shared code and sibling plugins must not reach into MACHINE. Only real dependencies count: an #include
# of a MACHINE file, a use of its namespace, or a build rule naming its folder. Comments that merely mention it
# (like the note in common/PumpPlugin.hpp) are fine.
dep='^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"][^>"]*(machine/|machine_fw|MachineEngine|MachineParams|MachineMeta|PluginMachine)'
dep="$dep"'|pump::machine|namespace[[:space:]]+machine|^[^#/]*(plugins/machine|machine/fw)'
hits=$(find common plugins \
        \( -path plugins/machine -o -path common/third_party \) -prune -o \
        -type f \( -name '*.hpp' -o -name '*.cpp' -o -name '*.h' -o -name '*.c' -o -name '*.mk' -o -name Makefile \) \
        -print | xargs grep -lE "$dep" 2>/dev/null)
if [ -n "$hits" ]; then
    echo "licence boundary: these files outside plugins/machine/ reference MACHINE (GPL-3.0) code:"
    echo "$hits" | sed 's/^/  /'
    fail=1
fi

# Rule 2: MACHINE keeps its own licence string.
if ! grep -q 'getLicense() const override { return "GPL-3.0-only"; }' plugins/machine/PluginMachine.hpp; then
    echo "licence boundary: plugins/machine/PluginMachine.hpp must override getLicense() to return \"GPL-3.0-only\""
    fail=1
fi

# Rule 2b: no other plugin may claim GPL (it would mislabel pump's own code), and the default must not be reset.
others=$(grep -rln 'getLicense' plugins --include=*.hpp --include=*.cpp | grep -v '^plugins/machine/')
if [ -n "$others" ]; then
    echo "licence boundary: only MACHINE may override getLicense(); found in:"
    echo "$others" | sed 's/^/  /'
    fail=1
fi

[ "$fail" -eq 0 ] && echo "licence boundary: ok"
exit "$fail"
