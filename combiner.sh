#!/bin/bash
set -euo pipefail

# 相対パスを絶対化し、"." / ".." / 連続スラッシュを解決する。
normalize_path() {
  local input_path="$1"
  local absolute_path

  if [[ "$input_path" == /* ]]; then
    absolute_path="$input_path"
  else
    absolute_path="$PWD/$input_path"
  fi

  local -a parts=()
  local -a stack=()
  local part
  IFS='/' read -r -a parts <<< "$absolute_path"

  for part in "${parts[@]}"; do
    if [[ -z "$part" || "$part" == "." ]]; then
      continue
    fi
    if [[ "$part" == ".." ]]; then
      if ((${#stack[@]} > 0)); then
        stack=("${stack[@]:0:${#stack[@]}-1}")
      fi
      continue
    fi
    stack+=("$part")
  done

  if ((${#stack[@]} == 0)); then
    printf '/\n'
    return 0
  fi

  local normalized='/'
  normalized+="${stack[0]}"
  local i
  for ((i = 1; i < ${#stack[@]}; i++)); do
    normalized+="/${stack[i]}"
  done
  printf '%s\n' "$normalized"
}

# 起点ディレクトリから親方向へ辿り、最も近い src ディレクトリを返す。
find_nearest_src_dir() {
  local start_dir="$1"
  local current="$start_dir"
  local candidate

  while :; do
    candidate="$current/src"
    if [[ -d "$candidate" ]]; then
      normalize_path "$candidate"
      return 0
    fi
    if [[ "$current" == "/" ]]; then
      return 1
    fi
    current="$(dirname "$current")"
  done
}

# include 探索順に従ってファイルを解決する。失敗時は探索候補を表示する。
resolve_local_include() {
  local include_target="$1"
  local including_file="$2"
  local including_dir
  including_dir="$(dirname "$including_file")"

  local -a searched=()
  local candidate

  candidate="$(normalize_path "$including_dir/$include_target")"
  searched+=("$candidate")
  if [[ -f "$candidate" ]]; then
    printf '%s\n' "$candidate"
    return 0
  fi

  if [[ -n "${SRC_ROOT:-}" ]]; then
    candidate="$(normalize_path "$SRC_ROOT/$include_target")"
    if [[ "$candidate" != "${searched[0]}" ]]; then
      searched+=("$candidate")
    fi
    if [[ -f "$candidate" ]]; then
      printf '%s\n' "$candidate"
      return 0
    fi
  fi

  # メインソースのディレクトリを include ルートとして探索する。
  if [[ -n "${SOURCE_DIR:-}" ]]; then
    candidate="$(normalize_path "$SOURCE_DIR/$include_target")"

    local already_searched=0
    local searched_path
    for searched_path in "${searched[@]}"; do
      if [[ "$searched_path" == "$candidate" ]]; then
        already_searched=1
        break
      fi
    done
    if ((already_searched == 0)); then
      searched+=("$candidate")
    fi

    if [[ -f "$candidate" ]]; then
      printf '%s\n' "$candidate"
      return 0
    fi
  fi

  printf '#include "%s" を "%s" から解決できませんでした。\n' "$include_target" "$including_file" >&2
  printf '探索したパス:\n' >&2
  local path
  for path in "${searched[@]}"; do
    printf '  - %s\n' "$path" >&2
  done
  return 1
}

# 再帰展開スタックから循環依存の経路を構築して表示する。
report_cycle_and_fail() {
  local repeated_file="$1"
  local start_index=-1
  local i
  for i in "${!INCLUDE_STACK[@]}"; do
    if [[ "${INCLUDE_STACK[$i]}" == "$repeated_file" ]]; then
      start_index="$i"
      break
    fi
  done

  local cycle_chain=''
  if ((start_index >= 0)); then
    local j
    for ((j = start_index; j < ${#INCLUDE_STACK[@]}; j++)); do
      if [[ -z "$cycle_chain" ]]; then
        cycle_chain="${INCLUDE_STACK[$j]}"
      else
        cycle_chain+=" > ${INCLUDE_STACK[$j]}"
      fi
    done
  else
    cycle_chain="${INCLUDE_STACK[*]}"
    cycle_chain="${cycle_chain// / > }"
  fi
  cycle_chain+=" > $repeated_file"

  printf '循環依存を検出しました: %s\n' "$cycle_chain" >&2
  return 1
}

# 1ファイルを走査し、#include "..." を再帰展開する。
expand_file() {
  local file_path="$1"

  if [[ -n "${ACTIVE_VISIT[$file_path]:-}" ]]; then
    report_cycle_and_fail "$file_path"
    return 1
  fi

  ACTIVE_VISIT["$file_path"]=1
  INCLUDE_STACK+=("$file_path")

  local line
  while IFS= read -r line || [[ -n "$line" ]]; do
    if [[ "$line" =~ ^[[:space:]]*#include[[:space:]]*\"([^\"]+)\"[[:space:]]*(//.*)?$ ]]; then
      local include_target="${BASH_REMATCH[1]}"
      local resolved_path

      # include 行自体の位置には常に改行1つを残す。
      printf '\n' >> "$TEMP_OUTPUT"

      if ! resolved_path="$(resolve_local_include "$include_target" "$file_path")"; then
        return 1
      fi

      if [[ -z "${EXPANDED_ONCE[$resolved_path]:-}" ]]; then
        if ! expand_file "$resolved_path"; then
          return 1
        fi
        EXPANDED_ONCE["$resolved_path"]=1
      fi
    elif [[ "$line" =~ ^[[:space:]]*#pragma[[:space:]]+once[[:space:]]*$ ]]; then
      # 結合後は単一ファイルになるため、ヘッダ用の#pragma onceを除く。
      printf '\n' >> "$TEMP_OUTPUT"
    else
      printf '%s\n' "$line" >> "$TEMP_OUTPUT"
    fi
  done < "$file_path"

  unset "ACTIVE_VISIT[$file_path]"
  unset "INCLUDE_STACK[${#INCLUDE_STACK[@]}-1]"
}

if [[ $# -ne 1 ]]; then
  printf '使い方: %s <source_file>\n' "$0" >&2
  exit 1
fi

SOURCE_FILE_RAW="$1"
SOURCE_FILE="$(normalize_path "$SOURCE_FILE_RAW")"
SOURCE_BASENAME="$(basename "$SOURCE_FILE")"
OUTPUT_FILE="$(normalize_path "combined/combined_${SOURCE_BASENAME}")"

if [[ ! -f "$SOURCE_FILE" ]]; then
  printf 'ソースファイルが見つかりません: %s\n' "$SOURCE_FILE" >&2
  exit 1
fi

SOURCE_DIR="$(dirname "$SOURCE_FILE")"
SRC_ROOT=''
if found_src_root="$(find_nearest_src_dir "$SOURCE_DIR")"; then
  SRC_ROOT="$found_src_root"
fi

TEMP_OUTPUT="$(mktemp "${TMPDIR:-/tmp}/combiner.XXXXXX")"
cleanup_temp() {
  if [[ -n "${TEMP_OUTPUT:-}" && -e "$TEMP_OUTPUT" ]]; then
    rm -f "$TEMP_OUTPUT"
  fi
}
trap cleanup_temp EXIT

declare -A EXPANDED_ONCE=()
declare -A ACTIVE_VISIT=()
declare -a INCLUDE_STACK=()

DISPLAY_SOURCE="$SOURCE_FILE"
if command -v realpath >/dev/null 2>&1; then
  if relative_source="$(realpath --relative-to="$PWD" -- "$SOURCE_FILE" 2>/dev/null)"; then
    DISPLAY_SOURCE="$relative_source"
  fi
fi

printf '// メインソース: %s\n' "$DISPLAY_SOURCE" > "$TEMP_OUTPUT"
expand_file "$SOURCE_FILE"

mkdir -p "$(dirname "$OUTPUT_FILE")"
mv "$TEMP_OUTPUT" "$OUTPUT_FILE"
TEMP_OUTPUT=''
