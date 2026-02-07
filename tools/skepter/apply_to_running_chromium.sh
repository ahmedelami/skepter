#!/usr/bin/env bash
set -euo pipefail

print_usage() {
  cat <<'USAGE'
Usage: apply_to_running_chromium.sh [options]

Builds the Chromium output directory that corresponds to the currently running
Chromium.app from this checkout (macOS), using the correct build driver for that
out dir (Siso vs Ninja). Designed to keep builds incremental and avoid
accidentally "kicking" an out dir into a cold rebuild.

Options:
  --out-dir <name|path>   Output dir (e.g. "M4Release" or "out/M4Release").
  --pid <pid>             Use this running Chromium PID (from this checkout).
  --target <ninja_target> Ninja target to build (default: chrome).
  --allow-large           Proceed even if the out dir looks "cold".
  --backup-only           Only snapshot build state (no build).
  --verbose               Show executed commands (passed to siso/ninja).
  --backup[=auto|always|none]
                          Backup build state before building (default: auto).
  -- <args...>            Extra args passed through to the build tool.
  -h, --help              Show help.
USAGE
}

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"

target="chrome"
out_dir_arg=""
pid_arg=""
allow_large=0
backup_mode="auto"
verbose=0
extra_args=()
backup_only=0

die() {
  echo "$@" >&2
  exit 2
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --out-dir)
      [[ $# -ge 2 ]] || die "--out-dir requires a value"
      out_dir_arg="$2"
      shift 2
      ;;
    --pid)
      [[ $# -ge 2 ]] || die "--pid requires a value"
      pid_arg="$2"
      shift 2
      ;;
    --target)
      [[ $# -ge 2 ]] || die "--target requires a value"
      target="$2"
      shift 2
      ;;
    --allow-large)
      allow_large=1
      shift
      ;;
    --backup-only)
      backup_only=1
      shift
      ;;
    --verbose)
      verbose=1
      shift
      ;;
    --backup=*)
      backup_mode="${1#*=}"
      case "$backup_mode" in
        auto|always|none) ;;
        *) die "Invalid --backup mode: $backup_mode" ;;
      esac
      shift
      ;;
    --backup)
      if [[ $# -ge 2 && "${2:-}" != -* ]]; then
        backup_mode="$2"
        shift 2
      else
        backup_mode="always"
        shift 1
      fi
      case "$backup_mode" in
        auto|always|none) ;;
        *) die "Invalid --backup mode: $backup_mode" ;;
      esac
      ;;
    --)
      shift
      extra_args+=("$@")
      break
      ;;
    -h|--help)
      print_usage
      exit 0
      ;;
    *)
      echo "Unknown arg: $1" >&2
      print_usage >&2
      exit 2
      ;;
  esac
done

if [[ -x /opt/homebrew/bin/python3 ]]; then
  export PATH="/opt/homebrew/bin:${PATH}"
fi

resolve_out_dir() {
  local arg="$1"
  if [[ -z "$arg" ]]; then
    return 1
  fi

  if [[ "$arg" == /* ]]; then
    echo "$arg"
    return 0
  fi

  if [[ "$arg" == out/* ]]; then
    echo "${SRC_DIR}/${arg}"
    return 0
  fi

  echo "${SRC_DIR}/out/${arg}"
}

extract_running_info() {
  local pid_filter="${1:-}"
  local found=0

  # Lines look like: "<pid> <args...>"
  while read -r pid rest; do
    [[ -n "$pid" ]] || continue
    if [[ -n "$pid_filter" && "$pid" != "$pid_filter" ]]; then
      continue
    fi

    local exec_path=""
    local user_data_dir=""
    for tok in $rest; do
      if [[ "$tok" == */Chromium.app/Contents/MacOS/Chromium ]]; then
        exec_path="$tok"
      elif [[ "$tok" == --user-data-dir=* ]]; then
        user_data_dir="${tok#--user-data-dir=}"
      fi
    done

    [[ -n "$exec_path" ]] || continue
    [[ "$exec_path" == "${SRC_DIR}/out/"*"/Chromium.app/Contents/MacOS/Chromium" ]] || continue

    local out_dir="${exec_path%/Chromium.app/Contents/MacOS/Chromium}"
    echo "${pid}|${out_dir}|${user_data_dir}"
    found=1
  done < <(ps -ax -o pid=,args=)

  [[ $found -eq 1 ]]
}

out_dir=""
running_pid=""
running_user_data_dir=""

if [[ -n "$out_dir_arg" ]]; then
  out_dir="$(resolve_out_dir "$out_dir_arg")"
elif [[ -n "$pid_arg" ]]; then
  matches="$(extract_running_info "$pid_arg" || true)"
  if [[ -z "$matches" ]]; then
    echo "No running Chromium PID $pid_arg found from this checkout." >&2
    exit 1
  fi
  # One line only when pid is specified.
  IFS='|' read -r running_pid out_dir running_user_data_dir <<<"$matches"
else
  matches="$(extract_running_info "" || true)"
  if [[ -z "$matches" ]]; then
    echo "No running Chromium.app found from this checkout under ${SRC_DIR}/out/." >&2
    echo "Tip: pass --out-dir <name> (e.g. --out-dir M4Release)." >&2
    exit 1
  fi

  match_count="$(printf '%s\n' "$matches" | wc -l | tr -d ' ')"
  if [[ "$match_count" != "1" ]]; then
    echo "Multiple running Chromium.app instances found from this checkout:" >&2
    printf '%s\n' "$matches" | while IFS='|' read -r pid od udd; do
      if [[ -n "$udd" ]]; then
        echo "  pid=$pid out_dir=$od user_data_dir=$udd" >&2
      else
        echo "  pid=$pid out_dir=$od" >&2
      fi
    done
    echo "Pass --pid <pid> or --out-dir <name> to choose one." >&2
    exit 1
  fi

  IFS='|' read -r running_pid out_dir running_user_data_dir <<<"$matches"
fi

if [[ -z "$out_dir" || ! -d "$out_dir" ]]; then
  echo "Output directory not found: $out_dir" >&2
  exit 1
fi

out_dir_rel="$out_dir"
if [[ "$out_dir" == "${SRC_DIR}/"* ]]; then
  out_dir_rel="${out_dir#${SRC_DIR}/}"
fi

use_siso=0
if [[ -f "${out_dir}/.siso_config" ]]; then
  use_siso=1
fi

is_build_running() {
  local out_rel="$1"
  local out_rel_escaped="${out_rel//\//\\/}"
  local pattern="(^|[[:space:]]|/)(autoninj[a]|nin[j]a)([[:space:]]|$).* -[C] ${out_rel_escaped}([[:space:]]|$)"
  ps -ax -o args= | grep -E "$pattern" >/dev/null 2>&1
}

if is_build_running "${out_dir_rel}"; then
  echo "A build already looks to be running for ${out_dir_rel}. Refusing to run concurrently." >&2
  exit 1
fi

min_log_size=100000
ninja_log="${out_dir}/.ninja_log"
state_log="$ninja_log"
state_log_desc=".ninja_log"
if [[ $use_siso -eq 1 ]]; then
  state_log="${out_dir}/.siso_deps"
  state_log_desc=".siso_deps"
fi
if [[ $backup_only -eq 0 ]]; then
  if [[ ! -f "$state_log" ]]; then
    if [[ $allow_large -eq 0 ]]; then
      echo "Missing ${out_dir_rel}/${state_log_desc}; this out dir looks cold and may trigger a huge rebuild." >&2
      echo "Re-run with --allow-large to proceed." >&2
      exit 1
    fi
  else
    log_size="$(stat -f%z "$state_log" 2>/dev/null || echo 0)"
    if [[ "$log_size" -lt "$min_log_size" && $allow_large -eq 0 ]]; then
      echo "${out_dir_rel}/${state_log_desc} is very small (${log_size} bytes); this looks like a cold/reset out dir." >&2
      echo "Refusing to proceed without --allow-large." >&2
      exit 1
    fi
  fi
else
  if [[ ! -f "$state_log" ]]; then
    echo "Warning: missing ${out_dir_rel}/${state_log_desc}; snapshot will not include it." >&2
  fi
fi

maybe_backup_state() {
  [[ "$backup_mode" != "none" ]] || return 0

  local out_name
  out_name="$(basename "$out_dir")"

  local backup_root="${SRC_DIR}/out/_state_backups/${out_name}"
  mkdir -p "$backup_root"

  if [[ "$backup_mode" == "auto" ]]; then
    local latest
    latest="$(ls -1dt "${backup_root}"/* 2>/dev/null | head -n 1 || true)"
    if [[ -n "$latest" ]]; then
      local now latest_mtime
      now="$(date +%s)"
      latest_mtime="$(stat -f%m "$latest" 2>/dev/null || echo 0)"
      if [[ $((now - latest_mtime)) -lt 86400 ]]; then
        return 0
      fi
    fi
  fi

  local ts backup_dir
  ts="$(date +%Y%m%d_%H%M%S)"
  backup_dir="${backup_root}/${ts}"
  mkdir -p "$backup_dir"

  local files=(
    "${out_dir}/args.gn"
    "${out_dir}/.ninja_log"
    "${out_dir}/.ninja_deps"
  )
  if [[ $use_siso -eq 1 ]]; then
    files+=(
      "${out_dir}/.siso_config"
      "${out_dir}/.siso_deps"
      "${out_dir}/.siso_filegroups"
      "${out_dir}/.siso_fs_state"
      "${out_dir}/.siso_fs_state.0"
      "${out_dir}/.siso_fs_state.journal"
      "${out_dir}/siso.INFO"
    )
  fi

  for f in "${files[@]}"; do
    if [[ -e "$f" ]]; then
      cp -p "$f" "$backup_dir/"
    fi
  done

  echo "$backup_dir"
}

backup_dir_created="$(maybe_backup_state || true)"
if [[ -n "$backup_dir_created" ]]; then
  echo "State backup: ${backup_dir_created#${SRC_DIR}/}"
fi

if [[ $backup_only -eq 1 ]]; then
  exit 0
fi

cd "$SRC_DIR"

echo "Out dir: ${out_dir_rel}"
if [[ -n "$running_pid" ]]; then
  echo "Running PID: ${running_pid}"
fi
if [[ -n "$running_user_data_dir" ]]; then
  echo "User data dir: ${running_user_data_dir}"
fi

if [[ $use_siso -eq 1 ]]; then
  echo "Build driver: siso"
  cmd=(third_party/siso/cipd/siso ninja --offline)
  [[ $verbose -eq 1 ]] && cmd+=(-v)
  cmd+=(-C "$out_dir_rel")
  cmd+=("${extra_args[@]}")
  cmd+=("$target")
  exec "${cmd[@]}"
else
  echo "Build driver: ninja"
  cmd=(third_party/ninja/ninja -C "$out_dir_rel")
  [[ $verbose -eq 1 ]] && cmd+=(-v)
  cmd+=("${extra_args[@]}")
  cmd+=("$target")
  exec "${cmd[@]}"
fi
