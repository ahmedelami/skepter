#!/usr/bin/env bash
set -euo pipefail

print_usage() {
  cat <<'USAGE'
Usage: restore_out_state.sh [options]

Restores build state files (.ninja_log/.ninja_deps/.siso_*) for an output
directory from the latest snapshot in:
  src/out/_state_backups/<out_name>/<timestamp>/

Options:
  --out-dir <name|path>   Output dir (e.g. "M4Release" or "out/M4Release").
  --pid <pid>             Use this running Chromium PID (from this checkout).
  --backup-dir <path>     Restore from this specific backup dir.
  --list                  List available backups for the resolved out dir.
  --restore-args          Also restore args.gn from the backup.
  --force                 Allow restore even if args.gn differs.
  -h, --help              Show help.
USAGE
}

die() {
  echo "$@" >&2
  exit 2
}

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"

out_dir_arg=""
pid_arg=""
backup_dir_arg=""
list_only=0
restore_args=0
force=0

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
    --backup-dir)
      [[ $# -ge 2 ]] || die "--backup-dir requires a value"
      backup_dir_arg="$2"
      shift 2
      ;;
    --list)
      list_only=1
      shift
      ;;
    --restore-args)
      restore_args=1
      shift
      ;;
    --force)
      force=1
      shift
      ;;
    -h|--help)
      print_usage
      exit 0
      ;;
    *)
      die "Unknown arg: $1"
      ;;
  esac
done

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

extract_running_out_dir() {
  local pid_filter="${1:-}"
  local found=0

  while read -r pid rest; do
    [[ -n "$pid" ]] || continue
    if [[ -n "$pid_filter" && "$pid" != "$pid_filter" ]]; then
      continue
    fi

    local exec_path=""
    for tok in $rest; do
      if [[ "$tok" == */Chromium.app/Contents/MacOS/Chromium ]]; then
        exec_path="$tok"
      fi
    done

    [[ -n "$exec_path" ]] || continue
    [[ "$exec_path" == "${SRC_DIR}/out/"*"/Chromium.app/Contents/MacOS/Chromium" ]] || continue

    local out_dir="${exec_path%/Chromium.app/Contents/MacOS/Chromium}"
    echo "${pid}|${out_dir}"
    found=1
  done < <(ps -ax -o pid=,args=)

  [[ $found -eq 1 ]]
}

out_dir=""
running_pid=""

if [[ -n "$out_dir_arg" ]]; then
  out_dir="$(resolve_out_dir "$out_dir_arg")"
elif [[ -n "$pid_arg" ]]; then
  match="$(extract_running_out_dir "$pid_arg" || true)"
  [[ -n "$match" ]] || die "No running Chromium PID $pid_arg found from this checkout."
  IFS='|' read -r running_pid out_dir <<<"$match"
else
  die "Pass --out-dir <name> (e.g. M4Release) or --pid <pid>."
fi

if [[ -z "$out_dir" || ! -d "$out_dir" ]]; then
  die "Output directory not found: $out_dir"
fi

out_dir_rel="$out_dir"
if [[ "$out_dir" == "${SRC_DIR}/"* ]]; then
  out_dir_rel="${out_dir#${SRC_DIR}/}"
fi

is_build_running() {
  local out_rel="$1"
  local out_rel_escaped="${out_rel//\//\\/}"
  local pattern="(^|[[:space:]]|/)(autoninj[a]|nin[j]a)([[:space:]]|$).* -[C] ${out_rel_escaped}([[:space:]]|$)"
  ps -ax -o args= | grep -E "$pattern" >/dev/null 2>&1
}

if is_build_running "${out_dir_rel}"; then
  die "A build looks to be running for ${out_dir_rel}. Stop it before restoring state."
fi

out_name="$(basename "$out_dir")"
backup_root="${SRC_DIR}/out/_state_backups/${out_name}"

if [[ $list_only -eq 1 ]]; then
  if [[ ! -d "$backup_root" ]]; then
    echo "No backups found for ${out_name}." >&2
    exit 1
  fi
  ls -1dt "${backup_root}"/* 2>/dev/null || true
  exit 0
fi

backup_dir=""
if [[ -n "$backup_dir_arg" ]]; then
  if [[ "$backup_dir_arg" == /* ]]; then
    backup_dir="$backup_dir_arg"
  else
    backup_dir="${backup_root}/${backup_dir_arg}"
  fi
else
  backup_dir="$(ls -1dt "${backup_root}"/* 2>/dev/null | head -n 1 || true)"
fi

if [[ -z "$backup_dir" || ! -d "$backup_dir" ]]; then
  die "Backup directory not found: $backup_dir"
fi

if [[ -f "${out_dir}/args.gn" && -f "${backup_dir}/args.gn" ]]; then
  if ! cmp -s "${out_dir}/args.gn" "${backup_dir}/args.gn"; then
    if [[ $force -eq 0 ]]; then
      die "args.gn differs from the backup. Re-run with --force (and optionally --restore-args)."
    fi
    echo "Warning: args.gn differs from the backup; restoring state may not be valid." >&2
  fi
fi

files=(
  ".ninja_log"
  ".ninja_deps"
  ".siso_config"
  ".siso_deps"
  ".siso_filegroups"
  ".siso_fs_state"
  ".siso_fs_state.0"
  ".siso_fs_state.journal"
  "siso.INFO"
)

restored=0
for f in "${files[@]}"; do
  if [[ -f "${backup_dir}/${f}" ]]; then
    cp -p "${backup_dir}/${f}" "${out_dir}/${f}"
    restored=$((restored + 1))
  fi
done

if [[ $restore_args -eq 1 && -f "${backup_dir}/args.gn" ]]; then
  cp -p "${backup_dir}/args.gn" "${out_dir}/args.gn"
  restored=$((restored + 1))
fi

echo "Restored ${restored} file(s) into ${out_dir_rel} from backup ${backup_dir#${SRC_DIR}/}."
