#!/bin/bash
# =============================================================================
# RTCon build pipeline
#
# Stages:
#   1. Load config         — parse config.yml, resolve paths, validate
#   2. Whole-project bc    — wllvm build of the target project, extract bitcode
#   3. TCG                 — run mta to produce tcg.dot
#   4. Target selection    — fzf pick OR load predefined targets
#   5. Harness build       — analyze_cfg.py + cgcc rebuild
# =============================================================================

set -u

# ---- pretty printing --------------------------------------------------------
RED="\033[1;31m"; GREEN="\033[1;32m"; YELLOW="\033[1;33m"
BLUE="\033[1;34m"; BOLD="\033[1m"; RESET="\033[0m"

info() { echo -e "${BLUE}[*]${RESET} $*"; }
ok()   { echo -e "${GREEN}[+]${RESET} $*"; }
warn() { echo -e "${YELLOW}[!]${RESET} $*"; }
err()  { echo -e "${RED}[!]${RESET} $*" >&2; }
die()  { err "$*"; exit 1; }
step() { echo; echo -e "${BOLD}${BLUE}=== Step $1/5: $2 ===${RESET}"; }

CONFIG_FILE="config.yml"

# yq helper that returns "" instead of "null" for missing keys
ycfg() { local v; v=$(yq e "$1" "$CONFIG_FILE"); [[ "$v" == "null" ]] && echo "" || echo "$v"; }

# =============================================================================
# Stage 1 — load and validate config
# =============================================================================
load_config() {
    step 1 "Load configuration"

    [[ -f "$CONFIG_FILE" ]] || die "$CONFIG_FILE not found in $(pwd)"

    PROJECTS_ROOT=$(ycfg '.general.projects_root')
    CGCC=$(ycfg '.general.cgcc_path')
    BC_FILE=$(ycfg '.general.bc_file_path')
    LL_FILE=$(ycfg '.general.ll_file_path')
    export LL_FILE

    USE_PREDEFINED_TARGETS=$(ycfg '.target.use_predefined_targets')
    TARGET_FUNC_FILE_REL=$(ycfg '.target.target_func_file')
    TARGET_FUNC=$(ycfg '.target.target_func')
    [[ -n "$TARGET_FUNC_FILE_REL" ]] || TARGET_FUNC_FILE_REL="targets.txt"

    ACTIVE_PROJECT=$(ycfg '.active_project')
    SUBDIR=""
    if [[ -z "$ACTIVE_PROJECT" || "$ACTIVE_PROJECT" == "none" ]]; then
        info "active_project not set — picking a subdirectory of $PROJECTS_ROOT"
        SUBDIR=$(_pick_project_subdir) || die "No project selected"
        # If the picked subdir matches some recipe's `subdir`, adopt that recipe.
        local matched
        matched=$(SUBDIR="$SUBDIR" yq e \
            '.projects // {} | to_entries
             | map(select(.value.subdir == strenv(SUBDIR)))
             | .[0].key // ""' "$CONFIG_FILE")
        if [[ -n "$matched" && "$matched" != "null" ]]; then
            ACTIVE_PROJECT="$matched"
            ok "Picked '$SUBDIR' — matched recipe '$matched'"
        else
            ACTIVE_PROJECT="$SUBDIR"
            ok "Picked '$SUBDIR' — no matching recipe, will use manual build"
        fi
    fi

    HAS_RECIPE=0
    if [[ $(yq e ".projects.${ACTIVE_PROJECT}" "$CONFIG_FILE") != "null" ]]; then
        HAS_RECIPE=1
        SUBDIR=$(ycfg ".projects.${ACTIVE_PROJECT}.subdir")
        BUILD_COMMAND=$(ycfg ".projects.${ACTIVE_PROJECT}.build_command")
        BUILD_DIR_REL=$(ycfg ".projects.${ACTIVE_PROJECT}.build_dir")
        BINARY_NAME=$(ycfg ".projects.${ACTIVE_PROJECT}.binary_name")
        [[ -n "$SUBDIR" ]] || die "projects.${ACTIVE_PROJECT}.subdir is empty"
    else
        warn "No build recipe for '${ACTIVE_PROJECT}' — will fall back to manual build"
        # SUBDIR was set above by the picker; if the user typed an unknown
        # active_project literally in config.yml, treat the name as the subdir.
        [[ -n "$SUBDIR" ]] || SUBDIR="$ACTIVE_PROJECT"
        BUILD_COMMAND=""
        BUILD_DIR_REL=""
        BINARY_NAME=""
    fi

    PROJECT_DIR="${PROJECTS_ROOT}/${SUBDIR}"
    PROJECT_BUILD_DIR=""
    if (( HAS_RECIPE )) && [[ -n "$BUILD_DIR_REL" ]]; then
        PROJECT_BUILD_DIR="${PROJECT_DIR}/${BUILD_DIR_REL}"
    fi

    [[ -d "$PROJECT_DIR" ]] || die "Project dir not found: $PROJECT_DIR (is ./project/${SUBDIR} mounted?)"

    TARGET_FUNC_FILE="${PROJECT_DIR}/${TARGET_FUNC_FILE_REL}"

    [[ -d "$TARGET_FUNC_FILE" ]] && \
        die "target_func_file resolved to a directory ($TARGET_FUNC_FILE). Check target.target_func_file in $CONFIG_FILE."

    if [[ "$USE_PREDEFINED_TARGETS" == "true" && ! -f "$TARGET_FUNC_FILE" ]]; then
        die "use_predefined_targets=true but $TARGET_FUNC_FILE not found"
    fi

    if [[ ! -f "$TARGET_FUNC_FILE" ]]; then
        {
            echo "Do not erase the below line."
            echo "--------------------------------------------------"
            echo ""
        } > "$TARGET_FUNC_FILE"
    fi

    USER_DIR=$(pwd)

    ok "Active project : ${BOLD}${ACTIVE_PROJECT}${RESET}"
    ok "Project dir    : $PROJECT_DIR"
    ok "Targets file   : $TARGET_FUNC_FILE"
    ok "Predefined     : ${USE_PREDEFINED_TARGETS}"
}

# Pick a subdirectory of $PROJECTS_ROOT via fzf. Echos the chosen subdir name
# on stdout. fzf draws to /dev/tty so the picker UI does not pollute stdout.
_pick_project_subdir() {
    [[ -d "$PROJECTS_ROOT" ]] || { err "Projects root not found: $PROJECTS_ROOT"; return 1; }
    command -v fzf >/dev/null 2>&1 || { err "fzf not installed"; return 1; }

    local subdirs=()
    mapfile -t subdirs < <(find "$PROJECTS_ROOT" -mindepth 1 -maxdepth 1 -type d -printf '%f\n' | sort)
    (( ${#subdirs[@]} )) || { err "No subdirectories under $PROJECTS_ROOT"; return 1; }

    # Annotate each entry with whether a recipe exists for it
    local recipe_subdirs
    recipe_subdirs=$(yq e '.projects // {} | .[].subdir // ""' "$CONFIG_FILE" 2>/dev/null)

    local labeled=() d tag
    for d in "${subdirs[@]}"; do
        if grep -Fxq -- "$d" <<<"$recipe_subdirs"; then
            tag="[recipe]"
        else
            tag="[manual build]"
        fi
        labeled+=("$d"$'\t'"$tag")
    done

    local choice
    choice=$(printf '%s\n' "${labeled[@]}" | \
        fzf --layout=reverse-list \
            --header-first \
            --header=' Pick a project from /projects (ENTER to confirm, ESC to abort)' \
            --delimiter=$'\t' \
            --with-nth=1,2 \
            --no-multi) || return 1
    [[ -n "$choice" ]] || return 1
    printf '%s\n' "${choice%%$'\t'*}"
}

# =============================================================================
# Stage 2 — produce whole-project LLVM bitcode (.bc)
# =============================================================================
build_bitcode() {
    step 2 "Build whole-project bitcode"

    if (( HAS_RECIPE )); then
        info "Building $ACTIVE_PROJECT with wllvm"
        export LLVM_COMPILER=clang
        export LLVM_COMPILER_PATH=/SVF/llvm-16.0.0.obj/bin/
        export CC=wllvm CXX=wllvm++
        export CFLAGS="-g -DNDEBUG"
        export CXXFLAGS="-g -DNDEBUG"
        export CCFLAGS="-g -DNDEBUG"

        ( cd "$PROJECT_DIR" && eval "$BUILD_COMMAND" ) || die "Project build failed"

        local chosen
        chosen=$(find "$PROJECT_DIR" -name "$BINARY_NAME" -printf '%T@ %p\n' \
                 | sort -n | tail -1 | cut -f2- -d" ")
        [[ -n "$chosen" ]] || die "$BINARY_NAME not found under $PROJECT_DIR"

        info "Extracting bitcode from: $chosen"
        extract-bc "$chosen" --bitcode
        cp "${chosen}.bc" "$BC_FILE"
    fi

    if [[ ! -f "$BC_FILE" ]]; then
        warn "$BC_FILE not built — dropping into manual build shell"
        echo "-----------------------------------------------"
        echo "Build the project so that wllvm produces a binary you can extract-bc from."
        echo -e "${YELLOW}Type 'exit' when the build is done.${RESET}"
        echo "-----------------------------------------------"

        ( cd "$PROJECT_DIR" && \
          LLVM_COMPILER=clang \
          LLVM_COMPILER_PATH=/SVF/llvm-16.0.0.obj/bin/ \
          CC=wllvm CXX=wllvm++ \
          CFLAGS="-g -DNDEBUG" CXXFLAGS="-g -DNDEBUG" \
          PROMPT_COMMAND='echo $PWD > /tmp/build_dir' \
          /bin/bash )

        local last_dir
        last_dir=$(cat /tmp/build_dir 2>/dev/null || true)
        [[ -n "$last_dir" ]] || die "Could not detect user's last build directory"
        info "User last directory: $last_dir"

        mapfile -t bins < <(find "$last_dir" -type f -executable \
                                 ! -name "*.sh" ! -name "*.py" 2>/dev/null)
        (( ${#bins[@]} )) || die "No executable binaries found in $last_dir"

        local chosen
        if (( ${#bins[@]} == 1 )); then
            chosen="${bins[0]}"
            ok "Found one binary: $chosen"
        else
            echo "[*] Multiple binaries found. Choose one:"
            local i
            for i in "${!bins[@]}"; do printf "  [%d] %s\n" "$((i+1))" "${bins[$i]}"; done
            echo
            local sel
            while :; do
                read -r -p "Enter number (1-${#bins[@]}), or 0 to abort: " sel
                [[ "$sel" =~ ^[0-9]+$ ]] || { echo "Please enter a valid number."; continue; }
                (( sel == 0 )) && die "Aborted by user"
                if (( sel >= 1 && sel <= ${#bins[@]} )); then
                    chosen="${bins[$((sel-1))]}"; break
                fi
                echo "Invalid index."
            done
            ok "Selected binary: $chosen"
        fi

        info "Extracting bitcode from: $chosen"
        extract-bc "$chosen"
        cp "${chosen}.bc" "$BC_FILE"
    fi

    [[ -f "$BC_FILE" ]] || die "$BC_FILE still not found after manual build"

    ok "Generating LLVM IR file: $LL_FILE"
    llvm-dis-18 "$BC_FILE" -o "$LL_FILE"
}

# =============================================================================
# Stage 3 — build the TCG (taint call graph) with mta
# =============================================================================
build_tcg() {
    step 3 "Build TCG"

    setsid mta "$BC_FILE" &
    local mta_pid=$!

    while true; do
        if [[ -f "tcg.dot" ]]; then
            break
        elif [[ -f "ptacg.dot" ]]; then
            mv ptacg.dot tcg.dot
            break
        fi
        sleep 1
    done
    sleep 1

    if ps -p "$mta_pid" > /dev/null 2>&1; then
        kill -9 "$mta_pid" &>/dev/null
    fi
    ok "Finished building TCG"
}

# =============================================================================
# Stage 4 — pick the target function
# =============================================================================
declare -a FUNC_NAMES
declare -a FILE_PATHS

select_target() {
    step 4 "Select target function"

    if [[ "$USE_PREDEFINED_TARGETS" != "true" ]]; then
        _pick_targets_interactively
    else
        _load_predefined_targets
    fi

    echo
    if [[ "$USE_PREDEFINED_TARGETS" == "true" ]]; then
        ok "Loaded ${#FUNC_NAMES[@]} target functions from $TARGET_FUNC_FILE"
    else
        ok "Selected ${#FUNC_NAMES[@]} target functions from function list"
    fi

    if [[ -n "$TARGET_FUNC" ]]; then
        local found=0 f
        for f in "${FUNC_NAMES[@]}"; do
            [[ "$f" == "$TARGET_FUNC" ]] && { found=1; break; }
        done
        (( found )) || die "Target function '$TARGET_FUNC' not found in the list"
    fi

    echo "Available functions:"
    local i
    for i in "${!FUNC_NAMES[@]}"; do
        printf "  [%d] %s    (%s)\n" "$((i+1))" "${FUNC_NAMES[$i]}" "${FILE_PATHS[$i]}"
    done
    echo

    if [[ -n "$TARGET_FUNC" ]]; then
        local line
        line=$(grep ",$TARGET_FUNC," "$TARGET_FUNC_FILE" | head -n 1)
        FILE_PATH=$(echo "$line" | cut -d',' -f1)
        FUNC_NAME=$(echo "$line" | cut -d',' -f2)
        ok "Automatically selected: $FUNC_NAME ($FILE_PATH)"
    else
        _prompt_function_choice
    fi

    if [[ "$USE_PREDEFINED_TARGETS" != "true" ]]; then
        _prompt_user_indices_and_save
    fi
}

# fzf-based multi-select, then capture results into FUNC_NAMES / FILE_PATHS
_pick_targets_interactively() {
    readfunc "$LL_FILE"
    local file="functions.txt"
    [[ -f "$file" ]] || die "$file not found"
    command -v fzf >/dev/null 2>&1 || die "fzf not installed"

    local selected
    selected=$(cat "$file" | \
        fzf --multi \
            --delimiter=',' \
            --with-nth=2,1 \
            --nth=1,2 \
            --layout=reverse-list \
            --header-first \
            --header=$' Search by file name.\n
 Select Functions (TAB / SPACE to mark, ENTER to confirm)
 Ctrl + A to select all, Ctrl + D to deselect all, Ctrl + / to toggle preview' \
            --preview 'echo "File: {1}\nFunction: {2}\nParams: {3}\n\nFunction snippet:
"; readfunc $LL_FILE {1} {2} | highlight -O ansi --syntax=cpp' \
            --preview-window=down:45% \
            --bind 'tab:toggle+down,space:toggle+down' \
            --bind 'ctrl-a:select-all,ctrl-d:deselect-all' \
            --bind 'ctrl-/:toggle-preview')

    [[ -n "$selected" ]] || { echo "No selection"; exit 0; }

    while IFS=',' read -r filename funcname paramcnt; do
        FILE_PATHS+=("$filename")
        FUNC_NAMES+=("$funcname")
    done <<< "$selected"
}

# Read entries from $TARGET_FUNC_FILE (after the "-----" delimiter line)
_load_predefined_targets() {
    local in_section=0 line filepath funcname
    while IFS= read -r line || [[ -n "$line" ]]; do
        line=${line%$'\r'}
        if (( in_section == 0 )); then
            [[ "$line" == *-----* ]] && in_section=1
            continue
        fi
        [[ -z "$line" ]] && continue
        [[ "$line" =~ ^# ]] && continue
        filepath=$(echo "$line" | cut -d',' -f1)
        funcname=$(echo "$line" | cut -d',' -f2)
        FILE_PATHS+=("$filepath")
        FUNC_NAMES+=("$funcname")
    done < "$TARGET_FUNC_FILE"
}

# Interactive selection by number or substring
_prompt_function_choice() {
    local query num choice_index matches m idx
    while true; do
        echo -n "Search function name or num (or 'exit'): "
        read -r query
        [[ "$query" == "exit" ]] && exit 0
        [[ -z "$query" ]] && continue

        if [[ "$query" =~ ^[0-9]+$ ]]; then
            num=$((query-1))
            if (( num >= 0 && num < ${#FUNC_NAMES[@]} )); then
                choice_index="$num"
                ok "Selected: FILE=${FILE_PATHS[$choice_index]} FUNC=${FUNC_NAMES[$choice_index]}"
                FUNC_NAME="${FUNC_NAMES[$choice_index]}"
                FILE_PATH="${FILE_PATHS[$choice_index]}"
                return
            fi
            err "Invalid number."
            continue
        fi

        matches=()
        for i in "${!FUNC_NAMES[@]}"; do
            [[ "${FUNC_NAMES[$i]}" == *"$query"* ]] && matches+=("$i")
        done

        if (( ${#matches[@]} == 0 )); then
            echo "No match found."
            continue
        fi

        echo "Matched functions:"
        idx=1
        for m in "${matches[@]}"; do
            echo "  [$idx] ${FUNC_NAMES[$m]}    (${FILE_PATHS[$m]})"
            ((idx++))
        done

        if (( ${#matches[@]} == 1 )); then
            choice_index="${matches[0]}"
            ok "Selected: FILE=${FILE_PATHS[$choice_index]} FUNC=${FUNC_NAMES[$choice_index]}"
            FUNC_NAME="${FUNC_NAMES[$choice_index]}"
            FILE_PATH="${FILE_PATHS[$choice_index]}"
            return
        fi

        echo -n "Choose number: "
        read -r num
        if ! [[ "$num" =~ ^[0-9]+$ ]]; then echo "Invalid number."; continue; fi
        num=$((num-1))
        if (( num < 0 || num >= ${#matches[@]} )); then
            err "Invalid selection."; continue
        fi

        choice_index="${matches[$num]}"
        ok "Selected: FILE=${FILE_PATHS[$choice_index]} FUNC=${FUNC_NAMES[$choice_index]}"
        FUNC_NAME="${FUNC_NAMES[$choice_index]}"
        FILE_PATH="${FILE_PATHS[$choice_index]}"
        return
    done
}

# Auto-config mode: ask user which params are user-controllable, append to file
_prompt_user_indices_and_save() {
    warn "RTCon currently supports a single function test in auto-config mode."
    echo "=================================================="
    echo "Function details:"
    readfunc "$LL_FILE" "$FILE_PATH" "$FUNC_NAME" | highlight -O ansi --syntax=cpp
    echo "=================================================="
    echo

    local user_indices
    echo -n "Enter user-controllable parameter indices (space-separated, e.g. '0 2', or empty for none): "
    read -r user_indices

    ok "Updating ${TARGET_FUNC_FILE}..."
    {
        echo ""
        echo "--------------------------------------------------"
        # 4th and 5th columns are placeholders so analyze_cfg.py's split(',') stays >=5
        echo "${FILE_PATH},${FUNC_NAME},${user_indices},-1,-1"
    } >> "$TARGET_FUNC_FILE"
}

# =============================================================================
# Stage 5 — generate harness config and rebuild with cgcc
# =============================================================================
build_harness() {
    step 5 "Build harness"

    python3 /source/cfg/analyze_cfg.py \
        --dot tcg.dot \
        --ll "$LL_FILE" \
        --target "$TARGET_FUNC_FILE" \
        --func "$FUNC_NAME" \
        --file "$FILE_PATH"

    local yaml_path="./config/${FUNC_NAME}.yaml"
    if [[ -f "$yaml_path" ]]; then
        ok "Created config file: $yaml_path"
    else
        err "Config file not found: $yaml_path"
        /bin/bash
    fi

    local target_so
    target_so=$(find /source -name "*${FUNC_NAME}*.so" | head -n 1)
    export HARNESS_LIB=$(realpath "$target_so")
    export TAINT_CONFIGURATION_FILE=$(realpath "$yaml_path")
    export TARGET_BINARY="$BINARY_NAME"

    # Swap ar -> cgcc so downstream link steps go through our wrapper
    mv /usr/bin/ar /usr/bin/ar.bak && ln -s "$CGCC" /usr/bin/ar
    export CC=$CGCC CXX=$CGCC
    export CFLAGS="-g -DNDEBUG"
    export CXXFLAGS="-g -DNDEBUG"
    export CCFLAGS="-g -DNDEBUG"
    export LD=/usr/bin/ld

    if (( HAS_RECIPE )); then
        ( cd "$PROJECT_DIR" && eval "$BUILD_COMMAND" )
        if [[ -n "$PROJECT_BUILD_DIR" ]]; then
            cd "$PROJECT_BUILD_DIR"
        else
            cd "$USER_DIR"
        fi
        cp /tmp/gen_harness ./
        /bin/bash
    else
        echo
        echo "-----------------------------------------------"
        echo "To build a harness, build the project with the cgcc compiler wrapper."
        warn "Remove any previously built artifacts to avoid stale files."
        warn "Example: 'make clean' (Make) or 'rm -rf build' (CMake)."
        ok "Done."
        cd "$PROJECT_DIR"
        /bin/bash
    fi
}

# =============================================================================
main() {
    load_config
    build_bitcode
    build_tcg
    select_target
    build_harness
}
main "$@"
