#!/bin/sh
# Soak test: let the auto-player bot play a whole game.  usage: tools/bot.sh [seed-frames] [skill 0|1] [extra harness cmds]
cd "$(dirname "$0")/.."
SYMS=$(arm-none-eabi-nm paws-off.elf | awk '$3 ~ /^(pl|racs|picks|jst|jx|jy|gs|score|night)$/ {printf "sym:%s=%s ", $3, $1}')
SEED=${1:-100}; SKILL=${2:-1}; [ $# -gt 0 ] && shift; [ $# -gt 0 ] && shift
if [ $# -gt 0 ]; then ./tools/harness paws-off.gba $SYMS $SEED +START 3 -START "$@"; else ./tools/harness paws-off.gba $SYMS $SEED +START 3 -START bot:40000:$SKILL; fi
rm -f paws-off.sav   # the harness writes a save file; do not leave bot scores behind
