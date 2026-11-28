# box_preflight.sh: the shared box-safety checks of EnginePerf's engine drivers. SOURCE it; never run it.
# Written 2026-11-27 (supervisor's order via the coordinator: one shared file instead of private copies). Desk only:
# its own self-test (box_preflight_selftest.sh, fixtures only) is the only thing that has run it.
#
# What sourcing does: sets the variables below and defines functions. It never exits, never writes a file, never
# signals a process, never claims, wants or releases. Every check only READS (a file, the claim tool's `check`
# text, the process list, the title checker's exit code).
#
# FAIL-CLOSED. Every check returns
#   0  clear: this condition does not stop a boot;
#   1  the refusing condition is present (named in BOX_WHY);
#   2  CANNOT TELL (a tool missing or failing, a file unreadable, a malformed value): counts as refusing.
# A caller must treat ANY non-zero return as "do not boot". BOX_WHY holds one line of reason after each call.
#
# Functions (name used in refusals | exit codes):
#   box_ok_play_lock                      play_lock        0 no live lock | 1 the lock names a live pid | 2 lock unreadable/not a pid
#   box_ok_title                          protected_title  0 checker exit 1 (none) | 1 checker exit 0 (playing) | 2 any other exit, or no checker
#   box_ok_roblox                         roblox           0 pgrep exit 1 | 1 pgrep exit 0 (sober running) | 2 any other pgrep exit
#   box_ok_engine                         engine_running   0 no Wine engine of the run tree | 1 one runs | 2 pgrep failed
#   box_ok_claims <worker> [<own token>]  own_claim        0 no other live claim under <worker> | 1 one is live | 2 `check` failed or unparsable
#   box_ok_exclusive                      exclusive        0 no live claim file names EXCLUSIVE | 1 one does | 2 a claim file unreadable
#   box_ok_display <display>              display          0 :<n> with n > 0 | 1 :0 (Caden's screen) | 2 not of the form :<digits>
#   box_ok_display_free <display>         display_free     0 no X socket and no X lock | 1 either exists (NOT in box_preflight_all)
#   box_claim_has_token <worker> <token>  (no refusal)     0 a live claim under <worker> carries <token> | 1 none | 2 cannot tell
#                                                          (a driver's "is this claim mine" test before a name-only release)
#   box_preflight_all <class> <display> <worker> [<own token>]
#       runs, in this order: play_lock, protected_title, roblox, engine_running, own_claim, exclusive (class shared
#       only), display. Prints one line "BOX REFUSED: <name>: <why>" per refusing check (stdout), and
#       "BOX OVERRIDES: <names>" first when an input below was overridden. Returns 0 all clear | 1 refused, every
#       refusal determinable | 2 refused and at least one check could not tell. <class> must be `shared` or
#       `exclusive` (anything else: refused by name, return 2).
#
# Inputs, each overridable from the environment ONLY so the self-test can point them at fixtures (real defaults):
#   BOX_PLAY_LOCK    $HOME/Projects/StalkerGameEngine/run/.caden-play.lock   (play/caden_simtick_play.sh writes its pid)
#   BOX_PROC         /proc                         (a pid is live when $BOX_PROC/<pid> exists; no signal is sent)
#   BOX_TITLE_CHECK  $HOME/.claude/overseer/tools/protected-title-check.sh
#   BOX_PGREP        pgrep
#   BOX_QR           $HOME/.claude/overseer/tools/quiet-required.sh     (only its read-only `check` verb is called)
#   BOX_QUIET_DIR    $HOME/.claude/overseer        (QUIET-REQUIRED and QUIET-REQUIRED-<n>; *.expired files ignored)
#   BOX_XROOT        /tmp                          (X sockets .X11-unix/X<n> and locks .X<n>-lock)
#   BOX_RUN          $HOME/Projects/StalkerGameEngine/run        (the run tree whose engines engine_running looks for)
# BOX_OVERRIDES lists the ones that were set before sourcing. A driver MUST refuse a real run while it is non-empty
# (s4d2_run.sh does); only a preflight-only or self-test call may run with overrides.
#
# Where the logic comes from, and where this file is STRICTER than the drivers it was taken from (common/README.md
# has the table): play lock (sf1/wu*/kj*: `[ -e lock ] && kill -0 "$(cat lock)"`): here an unreadable, empty or
# non-numeric lock refuses (they read it as "no lock"), and liveness is /proc/<pid> (kill -0 says "dead" for a live
# process of another user). Title (sf1 title_running, kj3p/kj4p title_state): any exit but 1 refuses (wu*, kw1,
# s4c_series, det_run.sh treat a failing checker as "no title"). Roblox: pgrep errors refuse (kj*'s title_state; sf1
# and wu* read them as "not running"). own_claim (sf1/kj3p/kj4p w_holds): unchanged test, but a `check` that fails
# or says held without a parsable line refuses (they read it as "no claim"); with a token, the caller's own claim is
# not counted (kj4p mine_held). exclusive: the brief's `grep -q EXCLUSIVE QUIET-REQUIRED`, extended to the extra
# slot files QUIET-REQUIRED-<n> that `check` also reads, and an unreadable file refuses. display (kj3p/kj4p):
# :<digits>, not 0. engine_running is new here (the play launcher's pattern, caden_simtick_play.sh:21, filtered by the
# Wine-binary rule of tools/lib_engine_pids.sh).

BOX_PREFLIGHT_VERSION=1
BOX_PREFLIGHT_MARKER=engineperf-box-preflight-v1
BOX_OVERRIDES=""
for _box_v in BOX_PLAY_LOCK BOX_PROC BOX_TITLE_CHECK BOX_PGREP BOX_QR BOX_QUIET_DIR BOX_XROOT BOX_RUN; do
  [ -n "${!_box_v+x}" ] && BOX_OVERRIDES="$BOX_OVERRIDES${BOX_OVERRIDES:+ }$_box_v"
done
unset _box_v
: "${BOX_PLAY_LOCK:=$HOME/Projects/StalkerGameEngine/run/.caden-play.lock}"
: "${BOX_PROC:=/proc}"
: "${BOX_TITLE_CHECK:=$HOME/.claude/overseer/tools/protected-title-check.sh}"
: "${BOX_PGREP:=pgrep}"
: "${BOX_QR:=$HOME/.claude/overseer/tools/quiet-required.sh}"
: "${BOX_QUIET_DIR:=$HOME/.claude/overseer}"
: "${BOX_XROOT:=/tmp}"
: "${BOX_RUN:=$HOME/Projects/StalkerGameEngine/run}"
BOX_WHY=""

box_ok_play_lock() {
  local p
  BOX_WHY="no live play lock ($BOX_PLAY_LOCK)"
  { [ -e "$BOX_PLAY_LOCK" ] || [ -L "$BOX_PLAY_LOCK" ]; } || return 0
  if ! [ -f "$BOX_PLAY_LOCK" ] || ! [ -r "$BOX_PLAY_LOCK" ]; then
    BOX_WHY="$BOX_PLAY_LOCK exists but is not a readable file: cannot tell whether Caden is playing"; return 2; fi
  p=$(head -c 64 -- "$BOX_PLAY_LOCK" 2>/dev/null) || { BOX_WHY="$BOX_PLAY_LOCK cannot be read: cannot tell"; return 2; }
  p=${p//[$'\r\n\t ']/}
  case "$p" in
    ''|*[!0-9]*) BOX_WHY="$BOX_PLAY_LOCK holds no pid ('${p:0:20}'): cannot tell whether Caden is playing"; return 2 ;;
  esac
  if [ -e "$BOX_PROC/$p" ]; then
    BOX_WHY="Caden's play session is running ($BOX_PLAY_LOCK names live pid $p)"; return 1; fi
  BOX_WHY="stale play lock ($BOX_PLAY_LOCK names pid $p, which is gone)"; return 0
}

box_ok_title() {
  local r
  if ! [ -x "$BOX_TITLE_CHECK" ]; then BOX_WHY="the title checker $BOX_TITLE_CHECK is missing or not executable: cannot tell"; return 2; fi
  "$BOX_TITLE_CHECK" >/dev/null 2>&1 </dev/null; r=$?
  case $r in
    1) BOX_WHY="no protected title playing"; return 0 ;;
    0) BOX_WHY="a protected title is playing (protected-title-check exit 0)"; return 1 ;;
    *) BOX_WHY="the title checker exited $r (only 0 and 1 are answers): cannot tell"; return 2 ;;
  esac
}

box_ok_roblox() {
  local r
  "$BOX_PGREP" -x sober >/dev/null 2>&1 </dev/null; r=$?
  case $r in
    1) BOX_WHY="Sober (Roblox) not running"; return 0 ;;
    0) BOX_WHY="Sober (Roblox) is running"; return 1 ;;
    *) BOX_WHY="pgrep -x sober exited $r: cannot tell"; return 2 ;;
  esac
}

box_ok_engine() {   # an engine started from the run tree's bin: the play launcher's pattern (caden_simtick_play.sh:21),
                    # and, as tools/lib_engine_pids.sh requires (no bare pgrep -f name match, Overseer 2026-11-23), a
                    # candidate counts only if its binary ($BOX_PROC/<pid>/exe) is a Wine loader: a watcher, grep or
                    # shell whose argv names the path is never an engine
  local r pat out p e n=0
  pat="$BOX_RUN/bin/|${BOX_RUN//\//\\\\}\\\\bin"
  out=$("$BOX_PGREP" -f "$pat" 2>/dev/null </dev/null); r=$?
  case $r in
    0|1) ;;
    *) BOX_WHY="pgrep -f <run tree>/bin exited $r: cannot tell"; return 2 ;;
  esac
  for p in $out; do
    case "$p" in ''|*[!0-9]*) BOX_WHY="pgrep printed '$p', not a pid: cannot tell"; return 2 ;; esac
    e=$(readlink "$BOX_PROC/$p/exe" 2>/dev/null) || continue   # gone, or not ours to read: a Wine engine of ours is readable
    case "$e" in *wine*) BOX_WHY="an engine from $BOX_RUN/bin is running (pid $p, $e): Caden's, or another job's"; return 1 ;; esac
    n=$((n + 1))
  done
  BOX_WHY="no engine of $BOX_RUN running ($n non-Wine match(es) ignored)"; return 0
}

box_ok_claims() {   # <worker> [<own token>]
  local w=${1:-} tok=${2:-} out r lines
  [ -n "$w" ] || { BOX_WHY="box_ok_claims needs a worker name"; return 2; }
  if ! [ -x "$BOX_QR" ]; then BOX_WHY="the claim tool $BOX_QR is missing or not executable: cannot tell"; return 2; fi
  out=$("$BOX_QR" check 2>/dev/null </dev/null); r=$?
  case $r in 0|1) ;; *) BOX_WHY="'quiet-required.sh check' exited $r: cannot tell"; return 2 ;; esac
  lines=$(printf '%s\n' "$out" | sed -n 's/^\(HELD\|SLOT[0-9]*\): //p')
  if [ $r = 0 ] && ! printf '%s\n' "$out" | grep -q '^HELD: '; then
    BOX_WHY="'check' says held (exit 0) but prints no HELD line: cannot tell"; return 2; fi
  # sf1/kj4p w_holds: a claim under our name, EXCEPT the queue's own handoff pre-claim; with a token, our own claim
  if printf '%s\n' "$lines" | awk -v w="$w" -v t="$tok" '$3 == w && !index($0, "handoff pre-claim") && (t == "" || !index($0, t)) {f = 1} END {exit !f}'; then
    BOX_WHY="a live claim under '$w' is held that is not this run's$([ -n "$tok" ] && echo " (token $tok)"): another job of ours holds the box"; return 1; fi
  BOX_WHY="no other live claim under '$w'"; return 0
}

box_claim_has_token() {   # <worker> <token>: 0 a live claim under <worker> carries <token> | 1 none does | 2 cannot tell
  local w=${1:-} tok=${2:-} out r       # (kj4p mine_held; for a driver's release decision, not a refusal check)
  { [ -n "$w" ] && [ -n "$tok" ]; } || { BOX_WHY="box_claim_has_token needs a worker and a token"; return 2; }
  if ! [ -x "$BOX_QR" ]; then BOX_WHY="the claim tool $BOX_QR is missing or not executable: cannot tell"; return 2; fi
  out=$("$BOX_QR" check 2>/dev/null </dev/null); r=$?
  case $r in 0|1) ;; *) BOX_WHY="'quiet-required.sh check' exited $r: cannot tell"; return 2 ;; esac
  if printf '%s\n' "$out" | sed -n 's/^\(HELD\|SLOT[0-9]*\): //p' | awk -v w="$w" -v t="$tok" '$3 == w && index($0, t) {f = 1} END {exit !f}'; then
    BOX_WHY="a claim under '$w' carries $tok"; return 0; fi
  BOX_WHY="no claim under '$w' carries $tok"; return 1
}

box_ok_exclusive() {
  local f n=0
  for f in "$BOX_QUIET_DIR/QUIET-REQUIRED" "$BOX_QUIET_DIR"/QUIET-REQUIRED-*; do
    case "$f" in "$BOX_QUIET_DIR/QUIET-REQUIRED") ;; *) case "${f##*/QUIET-REQUIRED-}" in ''|*[!0-9]*) continue ;; esac ;; esac
    { [ -e "$f" ] || [ -L "$f" ]; } || continue
    if ! [ -f "$f" ] || ! [ -r "$f" ]; then BOX_WHY="$f is not a readable file: cannot tell"; return 2; fi
    grep -q EXCLUSIVE -- "$f" 2>/dev/null; case $? in
      0) BOX_WHY="$f names EXCLUSIVE: $(head -c 160 -- "$f" | tr -d '\r\n')"; return 1 ;;
      1) n=$((n + 1)) ;;
      *) BOX_WHY="grep could not read $f: cannot tell"; return 2 ;;
    esac
  done
  BOX_WHY="no claim file names EXCLUSIVE ($n read)"; return 0
}

box_ok_display() {   # <display>
  local d=${1:-} n
  case "$d" in :[0-9]*) ;; *) BOX_WHY="display '$d' is not of the form :<digits>"; return 2 ;; esac
  n=${d#:}
  case "$n" in *[!0-9]*) BOX_WHY="display '$d' is not of the form :<digits>"; return 2 ;; esac
  [ $((10#$n)) -eq 0 ] && { BOX_WHY="display $d is Caden's screen"; return 1; }
  BOX_WHY="display $d is not :0"; return 0
}

box_ok_display_free() {   # <display>: no socket, no lock (sf1/kj4p disp_free)
  local n=${1#:}
  [ -e "$BOX_XROOT/.X11-unix/X$n" ] && { BOX_WHY="display $1 exists ($BOX_XROOT/.X11-unix/X$n)"; return 1; }
  [ -e "$BOX_XROOT/.X$n-lock" ] && { BOX_WHY="display $1 is locked ($BOX_XROOT/.X$n-lock)"; return 1; }
  BOX_WHY="display $1 free"; return 0
}

box_preflight_all() {   # <class shared|exclusive> <display> <worker> [<own token>]
  local cls=${1:-} disp=${2:-} w=${3:-} tok=${4:-} worst=0 r name
  [ -n "$BOX_OVERRIDES" ] && echo "BOX OVERRIDES: $BOX_OVERRIDES"
  case "$cls" in shared|exclusive) ;; *) echo "BOX REFUSED: claim_class: '$cls' is not shared or exclusive"; return 2 ;; esac
  for name in play_lock protected_title roblox engine_running own_claim exclusive display; do
    case $name in
      play_lock)       box_ok_play_lock ;;
      protected_title) box_ok_title ;;
      roblox)          box_ok_roblox ;;
      engine_running)  box_ok_engine ;;
      own_claim)       box_ok_claims "$w" "$tok" ;;
      exclusive)       [ "$cls" = shared ] || continue; box_ok_exclusive ;;
      display)         box_ok_display "$disp" ;;
    esac
    r=$?
    if [ $r != 0 ]; then
      echo "BOX REFUSED: $name: $BOX_WHY"
      [ $r = 1 ] && [ $worst = 0 ] && worst=1
      [ $r != 1 ] && worst=2
    fi
  done
  return $worst
}
