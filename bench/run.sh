#!/usr/bin/env bash
# One-command benchmark: sim up, bridge up, wait for every vehicle, record pose
# samples, report, tear down. Exits with the report's exit code.
#
#   bench/run.sh m1-clean
#
# Binaries come from the dev preset (build/dev). Set BUILD_DIR to use another
# build tree, for example BUILD_DIR=build/asan. Build first:
#   cmake --preset dev && cmake --build --preset dev
# Results go to bench/results/<UTC date>T<HHMMSS>Z-<scenario>/ (git-ignored).
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: bench/run.sh <scenario>   (scenarios: bench/scenarios/*.env)" >&2
  exit 2
fi
scenario=$1
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
env_file="$root/bench/scenarios/$scenario.env"
[[ -f $env_file ]] || { echo "no scenario '$scenario' ($env_file)" >&2; exit 2; }
# shellcheck disable=SC1090
source "$env_file"

build_dir=${BUILD_DIR:-$root/build/dev}
[[ $build_dir = /* ]] || build_dir=$root/$build_dir
bridge_bin=$build_dir/apps/bridge/skyrelay-bridge
probe_bin=$build_dir/apps/bench-probe/skyrelay-bench-probe
for b in "$bridge_bin" "$probe_bin"; do
  [[ -x $b ]] || { echo "missing $b; build it first (see header of bench/run.sh)" >&2; exit 2; }
done

results="$root/bench/results/$(date -u +%Y-%m-%dT%H%M%SZ)-$scenario"
mkdir -p "$results"
compose=(docker compose -f "$root/$SIM_COMPOSE")

bridge_pid=
cleanup() {
  local rc=$?
  trap - EXIT INT TERM
  if [[ -n $bridge_pid ]] && kill -0 "$bridge_pid" 2>/dev/null; then
    kill -INT "$bridge_pid" 2>/dev/null || true
    wait "$bridge_pid" 2>/dev/null || true
  fi
  "${compose[@]}" down --remove-orphans >"$results/sim-down.log" 2>&1 || true
  exit "$rc"
}
trap cleanup EXIT
trap 'exit 130' INT TERM

host_info() {
  {
    echo "date_utc: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "scenario: $scenario ($SCENARIO_DESCRIPTION)"
    echo "cpu: $(awk -F: '/model name/ {gsub(/^ /,"",$2); print $2; exit}' /proc/cpuinfo)"
    echo "cores: $(nproc)"
    echo "ram: $(awk '/MemTotal/ {printf "%.1f GiB", $2/1048576}' /proc/meminfo)"
    echo "kernel: $(uname -sr)"
    echo "docker: $(docker --version)"
    echo "git_commit: $(git -C "$root" rev-parse HEAD)$([[ -z $(git -C "$root" status --porcelain) ]] || echo ' (dirty)')"
    echo "px4: $(sed -n 's/^ARG PX4_VERSION=//p' "$root/sim/px4-sitl/Dockerfile")"
  } >"$results/host.txt"
}
host_info

echo "bench: starting simulation"
"${compose[@]}" up -d

echo "bench: starting bridge (log: $results/bridge.log)"
"$bridge_bin" --config "$root/$BRIDGE_CONFIG" >"$results/bridge.log" 2>&1 &
bridge_pid=$!

echo "bench: waiting for $VEHICLES vehicles (up to ${CONNECT_TIMEOUT_S}s)"
"$probe_bin" --wait-connected "$VEHICLES" --timeout "$CONNECT_TIMEOUT_S"

echo "bench: recording for ${DURATION_S}s"
"$probe_bin" --duration "$DURATION_S" --out "$results/pose.csv"

echo "bench: report"
rc=0
python3 "$root/bench/report/latency.py" "$results/pose.csv" \
  --out "$results/report.md" --gate-p99-ms "$GATE_P99_MS" || rc=$?
echo "bench: results in $results (exit $rc)"
exit "$rc"
