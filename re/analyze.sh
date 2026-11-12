#!/usr/bin/env bash
# Headless Ghidra import + auto-analysis + text export for the reference executables.
# Sources are only read; Ghidra projects go to re/ghidra, exports to re/export/<name>.
#   bash re/analyze.sh [name...]      names: saga w8 c2 (default: all, in parallel)
#   EXPORT_ONLY=1 bash re/analyze.sh  re-run only the export on already-analysed projects
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# Ghidra 12.2 finds a JDK itself (it picked the system Temurin JDK 25; it uses Java-25-only flags,
# so the portable JDK 21 in .tools/jdk is NOT usable for 12.2). Heap: support/launch.properties (-Xmx2G default).
GH="$ROOT/.tools/ghidra/ghidra_12.2_PUBLIC/support/analyzeHeadless.bat"
declare -A EXE=(
  [saga]="D:/Cultures_Saga/Game.exe"
  [w8]="D:/GOG Games/Northland and 8th Wonder of the World/8th Wonder of the World/Game.exe"
  [c2]="D:/GOG Games/Cultures and Cultures 2/Cultures 2/Cultures2.exe"
)
names=("$@"); [ ${#names[@]} -eq 0 ] && names=(saga w8 c2)
mkdir -p "$ROOT/re/ghidra" "$ROOT/re/export" "$ROOT/re/logs"
for n in "${names[@]}"; do
  (
    if [ "${EXPORT_ONLY:-0}" = 1 ]; then src=(-process "$(basename "${EXE[$n]}")" -noanalysis -readOnly)
    else src=(-import "$(cygpath -w "${EXE[$n]}")" -overwrite); fi
    "$GH" "$(cygpath -w "$ROOT/re/ghidra")" "$n" "${src[@]}" \
      -scriptPath "$(cygpath -w "$ROOT/re/scripts")" \
      -postScript ExportAll.java "$(cygpath -w "$ROOT/re/export/$n")" \
      > "$ROOT/re/logs/$n.log" 2>&1
    echo "$n exit=$?"
  ) &
done
wait
