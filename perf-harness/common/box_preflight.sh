# box_preflight.sh: the shared box-safety checks of EnginePerf's engine drivers. SOURCE it; never run it.
# Written 2026-11-27 (supervisor's order via the coordinator: one shared file instead of private copies). Version 2,
# 2026-11-28, after plan-facts/box-preflight-second-review-2026-11-27.md (B-1, B-2, S-1, S-6, S-7, N-1, N-4, N-7, N-10,
# N-11). Desk only: its own self-test (box_preflight_selftest.sh, fixtures only) is the only thing that has run it.
# Revised 2026-11-28 after plan-facts/box-preflight-third-review-2026-11-28.md (marker and version unchanged; the
# callers pin the sha256): S3-5 the caller's own sightings (BOX_SEEN_AT), N3-1 flatpak with XDG_RUNTIME_DIR, N3-7 `10#`,
# N3-9 a timeout on the title checker, box_preflight_all keeps its lines in BOX_OUT (so a caller can run it outside
# `$(...)` and keep BOX_SEEN_AT), box_is_own (N3-8), box_wine_kill's `nowait` mode (S3-1).
#
# What sourcing does: sets the variables below and defines functions. It never exits, writes, signals, claims, wants
# or releases. It NEVER calls quiet-required.sh (no verb at all; v1 called `check`, which is NOT read-only: it moves
# expired slot files to *.expired, can wake the queue head through tmux and runs the title scan, review N-1). The
# claim facts are read from the claim tool's own files instead (box_ok_claims below).
# What the functions do:
#   - the box_ok_* checks and box_claim_has_token / box_queue_count only READ (files, /proc, the process list, the
#     exit codes of pgrep / flatpak ps / systemctl --user list-units / the title checker). Side effect outside this
#     file's control: protected-title-check.sh keeps its own 2-s result cache in $XDG_RUNTIME_DIR/ptc;
#   - the ACTION helpers act, only as named: box_guarded runs the caller's write command after a fresh check;
#     box_rm_name deletes one validated name in one fixed folder; box_wine_kill runs `wineserver -k` in one validated
#     prefix; box_kill_own signals one pid only if it is the caller's own child of the expected name.
#
# FAIL-CLOSED. Every check returns
#   0  clear: this condition does not stop a boot;
#   1  the refusing condition is present (named in BOX_WHY);
#   2  CANNOT TELL (a tool missing or failing, a file unreadable, a malformed value): counts as refusing.
# A caller must treat ANY non-zero return as "do not boot / do not write". BOX_WHY holds one line of reason.
#
# Checks (name used in refusals | exit codes):
#   box_ok_play_lock                      play_lock        0 no live lock | 1 the lock names a live pid | 2 lock unreadable/not a pid
#   box_ok_title                          protected_title  0 checker exit 1 AND no sighting < 180 s (this shell's own
#                                                          BOX_SEEN_AT, or the claim tool's .protected-seen)
#                                                          | 1 checker exit 0 (sets BOX_SEEN_AT), or a sighting < 180 s old
#                                                          | 2 any other exit, no answer within 10 s, no checker, a PTC_*
#                                                          variable set by the caller, .protected-seen unreadable/malformed
#   box_ok_roblox                         roblox           0 no Sober | 1 Sober seen by any method | 2 a method could not tell
#   box_ok_engine                         engine_running   0 no Wine engine of the run tree | 1 one runs | 2 pgrep failed
#   box_ok_hostns                         host_pid_ns      0 $BOX_PROC/1/comm is systemd | 2 otherwise (a pid-namespaced
#                                                          sandbox reads every pid check as clear, review N-10)
#   box_ok_claims <worker> [<own token>]  own_claim        0 no other live claim record under <worker> | 1 one is live | 2 a claim file unreadable/unparsable
#   box_ok_exclusive                      exclusive        0 no LIVE claim slot names EXCLUSIVE | 1 one does | 2 a claim file unreadable/unparsable
#   box_ok_display <display>              display          0 :<n> with n > 0 | 1 :0 (Caden's screen) | 2 not of the form :<digits>
#   box_ok_display_free <display>         display_free     0 no X socket and no X lock | 1 either exists (NOT in box_preflight_all)
#   box_claim_has_token <worker> <token>  (no refusal)     0 a live claim record under <worker> carries <token> | 1 none | 2 cannot tell
#   box_queue_count <worker>              (no refusal)     BOX_QN = queue entries of <worker> in QUIET-QUEUE | 0 read | 2 unreadable
#   box_engines_in_prefix <prefix>        (no refusal)     0 no Wine engine of the run tree uses <prefix> | 1 some do (BOX_LEFT_PIDS)
#                                                          | 2 pgrep failed. A process whose environ cannot be read counts as ours.
#   box_fresh                             (the B-1 re-check) title, roblox, engine_running, then play_lock LAST: 0 all clear
#   box_preflight_all <class> <display> <worker> [<own token>]
#       runs, in this order: host_pid_ns, play_lock, protected_title, roblox, engine_running, own_claim, exclusive
#       (class shared only), display. Prints one line "BOX REFUSED: <name>: <why>" per refusing check (stdout), and
#       "BOX OVERRIDES: <names>" first when an input below was overridden. Returns 0 all clear | 1 refused, every
#       refusal determinable | 2 refused and at least one check could not tell. <class> must be `shared` or `exclusive`.
#       The same lines are left in BOX_OUT: call it as `box_preflight_all ... >/dev/null; r=$?; out=$BOX_OUT`, NOT
#       inside `$(...)`, or a title sighting it makes (BOX_SEEN_AT) is lost with the subshell (review S3-5).
#   BOX_SEEN_AT   (state, not an input; emptied when this file is sourced) the `date +%s` of this shell's last checker
#       exit 0. box_ok_title refuses for BOX_LINGER_S (180 s) after it, as the claim tool does after its own sightings.
#       It lives in the calling shell only: a sighting does not carry to a subshell's parent or to another process (a
#       det_run.sh started by s4d2_run.sh starts with none). The fleet's .protected-seen is NOT written: it belongs to
#       the claim tool's owner (common/README.md).
# Action helpers:
#   box_guarded <what> <command...>  if a stop was already recorded (BOX_STOPPED non-empty): skip, add <what> to
#       BOX_SKIPPED, return 1. Else box_fresh; on a refusal record BOX_STOPPED (sticky: every later guarded step is
#       skipped too), skip, return 1. Else run <command...> AT ONCE: the play lock was read on the line before, no wait.
#   box_rm_name <dir> <name>   rm -f <dir>/<name> only if <dir> is absolute, a real folder and its own realpath (no link
#       anywhere in it) and <name> is one file name of [A-Za-z0-9_.-] (not . or ..): the target cannot leave <dir>.
#   box_dumps_newer <dir> <marker>   prints the *.mdmp file names in <dir> (no subfolders) NEWER than <marker>; 2 if
#       the marker is missing (then nothing may be moved).
#   box_wine_kill <prefix> [nowait]   WINEPREFIX=<prefix> wineserver -k, then wineserver -w (each bounded 30 s), only if
#       <prefix> is absolute, ends in /run/prefix, names no prefix_play, is a real folder and its own realpath. WINEPREFIX
#       is set on that command line itself (exported to it), so it can never be empty or the play prefix. `nowait`: the
#       same checks, then only `wineserver -k` bounded 10 s (no -w): for a repeated kill inside a caller's own timed wait.
#   box_is_own <pid> <name> <parent pid>   0 only if $BOX_PROC/<pid>/stat shows it is not a zombie, its parent is
#       <parent pid> and its name (comm) is <name>; 1 otherwise (reads only).
#   box_kill_own <pid> <name> <parent pid>   kill <pid> only if box_is_own holds (review N-4: a reused pid is never
#       signalled).
#   BOX_QR_UNSET   `env` arguments that drop the claim tool's tuning variables (FORCE, MAXSLOTS, PROTECTED_LINGER,
#       QR_CLAIM_ID, ...) from a caller's environment; drivers prefix their own claim / want / release calls with it.
#
# Inputs, each overridable from the environment ONLY so the self-test can point them at fixtures (real defaults):
#   BOX_PLAY_LOCK    $HOME/Projects/StalkerGameEngine/run/.caden-play.lock   (play/caden_simtick_play.sh writes its pid)
#   BOX_PROC         /proc                         (pids, cgroups, .flatpak-info, environ, stat, comm; no signal is sent)
#   BOX_TITLE_CHECK  $HOME/.claude/overseer/tools/protected-title-check.sh
#   BOX_PSEEN        $HOME/.claude/overseer/.protected-seen   (the claim tool's "title seen at <epoch>" record)
#   BOX_PGREP        pgrep
#   BOX_FLATPAK      flatpak
#   BOX_SYSTEMCTL    systemctl
#   BOX_QUIET_DIR    $HOME/.claude/overseer        (QUIET-REQUIRED, QUIET-REQUIRED-<n>, quiet-claims/<slot>, QUIET-QUEUE)
#   BOX_XROOT        /tmp                          (X sockets .X11-unix/X<n> and locks .X<n>-lock)
#   BOX_RUN          $HOME/Projects/StalkerGameEngine/run        (the run tree whose engines engine_running looks for)
#   BOX_WINESERVER   wineserver
#   BOX_KILL         kill
#   (BOX_QR, v1's claim-tool input, is no longer read; setting it still counts as an override)
# BOX_OVERRIDES lists the ones that were set before sourcing. A driver MUST refuse a real run while it is non-empty
# (s4d2_run.sh and tools/det_run.sh do); only a preflight-only or self-test call may run with overrides.
#
# ENVIRONMENT THE CHECKS TRUST (review S-6): HOME (every default path above), PATH (pgrep, flatpak, systemctl, date,
# grep, awk, sed, realpath, find, timeout, wineserver), UID. Refused: any PTC_* variable (it changes the title checker's
# answer; box_ok_title). Removed for the checker call: INTERVAL, MIN_PCT; XDG_RUNTIME_DIR is set to /run/user/$UID for
# it (its result cache lives there) and for `flatpak ps` (review N3-1: without it flatpak exits 0 and lists NOTHING,
# which would read as "no Sober"). Not refused: a doctored PATH or HOME (a caller that controls those controls the
# driver anyway; no PATH/HOME sanity check, by the coordinator's word).
#
# Where the logic comes from, and where this file is STRICTER than the drivers it was taken from (common/README.md
# has the table): play lock (sf1/wu*/kj*: `[ -e lock ] && kill -0 "$(cat lock)"`): here an unreadable, empty or
# non-numeric lock refuses, and liveness is /proc/<pid>. Title: any exit but 1 refuses, plus the claim tool's 180-s
# linger (quiet-required.sh protected_title_playing, PROTECTED_LINGER default 180). Roblox: see box_ok_roblox. Claims:
# read from the claim tool's files, every RECORD (nested claims included, review S-7). exclusive: live slot lines.
# engine_running: the play launcher's pattern (caden_simtick_play.sh:21), filtered by the Wine-binary rule of
# tools/lib_engine_pids.sh.

BOX_PREFLIGHT_VERSION=2
BOX_PREFLIGHT_MARKER=engineperf-box-preflight-v2
BOX_OVERRIDES=""
for _box_v in BOX_PLAY_LOCK BOX_PROC BOX_TITLE_CHECK BOX_PSEEN BOX_PGREP BOX_FLATPAK BOX_SYSTEMCTL BOX_QUIET_DIR BOX_XROOT \
              BOX_RUN BOX_WINESERVER BOX_KILL BOX_QR; do
  [ -n "${!_box_v+x}" ] && BOX_OVERRIDES="$BOX_OVERRIDES${BOX_OVERRIDES:+ }$_box_v"
done
unset _box_v
: "${BOX_PLAY_LOCK:=$HOME/Projects/StalkerGameEngine/run/.caden-play.lock}"
: "${BOX_PROC:=/proc}"
: "${BOX_TITLE_CHECK:=$HOME/.claude/overseer/tools/protected-title-check.sh}"
: "${BOX_PSEEN:=$HOME/.claude/overseer/.protected-seen}"
: "${BOX_PGREP:=pgrep}"
: "${BOX_FLATPAK:=flatpak}"
: "${BOX_SYSTEMCTL:=systemctl}"
: "${BOX_QUIET_DIR:=$HOME/.claude/overseer}"
: "${BOX_XROOT:=/tmp}"
: "${BOX_RUN:=$HOME/Projects/StalkerGameEngine/run}"
: "${BOX_WINESERVER:=wineserver}"
: "${BOX_KILL:=kill}"
BOX_LINGER_S=180   # quiet-required.sh PROTECTED_LINGER default; fixed here (the caller's PROTECTED_LINGER is not read)
BOX_QR_UNSET=(-u FORCE -u MAXSLOTS -u PROTECTED_LINGER -u QR_CLAIM_ID -u QR_LOCK_WAIT -u QR_ADMIT_TTL -u RENEW_GRACE
              -u WOKEN_HOLD -u QUEUE_TTL -u SLOT2_VRAM_PCT -u SLOT2_MIN_AVAIL_GB -u SLOT2_LOAD_MAX -u VRAM_BUSY_PCT
              -u SLOT2_WHAT -u PANE -u INTERVAL -u MIN_PCT)
BOX_TITLE_TIMEOUT_S=10   # N3-9: the checker samples for ~2 s (its own 2-s cache aside); 5x that, then "cannot tell"
BOX_WHY=""; BOX_STOPPED=""; BOX_SKIPPED=""; BOX_QN=0; BOX_LEFT_PIDS=""; BOX_RECS=""; BOX_LIVE_LINES=""; BOX_OUT=""
BOX_SEEN_AT=""   # S3-5: this shell's own last sighting (never inherited: emptied here)

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

box_ok_title() {   # the fleet checker (exit 0 playing / 1 none), then the claim tool's 180-s memory of a sighting
  local r v bad="" t w now
  for v in $(compgen -v PTC_); do bad="$bad $v"; done
  [ -n "$bad" ] && { BOX_WHY="the caller's environment sets$bad (these change the title checker's answer): refused"; return 2; }
  if ! [ -x "$BOX_TITLE_CHECK" ]; then BOX_WHY="the title checker $BOX_TITLE_CHECK is missing or not executable: cannot tell"; return 2; fi
  env -u INTERVAL -u MIN_PCT XDG_RUNTIME_DIR="/run/user/$UID" timeout "$BOX_TITLE_TIMEOUT_S" "$BOX_TITLE_CHECK" >/dev/null 2>&1 </dev/null; r=$?
  case $r in
    1) ;;
    0) BOX_SEEN_AT=$(date +%s)
       BOX_WHY="a protected title is playing (protected-title-check exit 0)"; return 1 ;;
    124) BOX_WHY="the title checker gave no answer within ${BOX_TITLE_TIMEOUT_S}s: cannot tell"; return 2 ;;
    *) BOX_WHY="the title checker exited $r (only 0 and 1 are answers): cannot tell"; return 2 ;;
  esac
  # S3-5: this shell's own sighting (a game in a menu can read idle on the next 2-s sample), 180 s as the claim tool
  now=$(date +%s)
  if [ -n "$BOX_SEEN_AT" ]; then
    case "$BOX_SEEN_AT" in *[!0-9]*) BOX_WHY="BOX_SEEN_AT '${BOX_SEEN_AT:0:20}' is not a time: cannot tell"; return 2 ;; esac
    if [ $(( now - 10#$BOX_SEEN_AT )) -lt "$BOX_LINGER_S" ]; then
      BOX_WHY="a protected title was seen by this run $(( now - 10#$BOX_SEEN_AT ))s ago; counted as playing for ${BOX_LINGER_S}s"; return 1; fi
  fi
  # quiet-required.sh:238-266: every positive is written to .protected-seen as "<epoch> <hit>", and the claim tool
  # treats the title as still playing for 180 s after it (one 2-s sample of a game in a menu can read idle).
  if [ -e "$BOX_PSEEN" ] || [ -L "$BOX_PSEEN" ]; then
    { [ -f "$BOX_PSEEN" ] && [ -r "$BOX_PSEEN" ]; } || { BOX_WHY="$BOX_PSEEN exists but is not a readable file: cannot tell"; return 2; }
    t=""; w=""; read -r t w < "$BOX_PSEEN" 2>/dev/null
    case "$t" in ''|*[!0-9]*) BOX_WHY="$BOX_PSEEN holds no time ('${t:0:20}'): cannot tell"; return 2 ;; esac
    if [ $(( now - 10#$t )) -lt "$BOX_LINGER_S" ]; then
      BOX_WHY="a protected title was seen $(( now - 10#$t ))s ago (${w:0:80}); the claim tool counts it as playing for ${BOX_LINGER_S}s"; return 1; fi
  fi
  BOX_WHY="no protected title playing (none seen in the last ${BOX_LINGER_S}s)"; return 0
}

box_ok_roblox() {   # Roblox runs under the Sober flatpak (org.vinegarhq.Sober); its game process is named "Main", so
  # `pgrep -x sober` alone NEVER matched it (review S-1). Methods, any hit refuses, any that cannot tell refuses:
  #  a. protected-title-check.sh rule 7 (is_sober): a process in systemd scope app-flatpak-org.vinegarhq.Sober-<n>.scope
  #     (the "0::" line of $BOX_PROC/<pid>/cgroup), else $BOX_PROC/<pid>/root/.flatpak-info naming org.vinegarhq.Sober;
  #  b. the ops worker's proposal ~/Projects/Ops/proposals/sober-check/sober_running.sh (2026-11-27, read as reference,
  #     not sourced): `flatpak ps --columns=application` lists org.vinegarhq.Sober, or `systemctl --user list-units
  #     --state=active 'app-flatpak-org.vinegarhq.Sober-*.scope'` lists a unit; either tool failing = cannot tell;
  #  c. the old `pgrep -x sober`, kept only as one more refusal.
  # (protected-title-check.sh itself already refuses an ACTIVE Sober through box_ok_title; this check is a second,
  # independent refusal that also catches an idle one.)
  local hit="" unk="" r out f
  if ! [ -r "$BOX_PROC/1/cgroup" ]; then unk="$BOX_PROC/1/cgroup is unreadable"
  else
    f=$(grep -l -s -E '^0::.*/app-flatpak-org\.vinegarhq\.Sober-[^/]*\.scope(/.*)?$' "$BOX_PROC"/[0-9]*/cgroup 2>/dev/null | head -1)
    [ -n "$f" ] && hit="a process in a Sober flatpak scope (${f%/cgroup})"
    if [ -z "$hit" ]; then
      f=$(grep -l -s -x 'name=org.vinegarhq.Sober' "$BOX_PROC"/[0-9]*/root/.flatpak-info 2>/dev/null | head -1)
      [ -n "$f" ] && hit="a process of the Sober flatpak (${f%/root/.flatpak-info})"
    fi
  fi
  # N3-1: XDG_RUNTIME_DIR set for flatpak (as for the title checker): without it `flatpak ps` exits 0 and lists
  # nothing. Empty output with exit 0 is NOT read as "cannot tell": with no flatpak app running it is the true answer.
  out=$(XDG_RUNTIME_DIR="/run/user/$UID" timeout 10 "$BOX_FLATPAK" ps --columns=application 2>/dev/null </dev/null); r=$?
  if [ $r = 0 ]; then
    [ -z "$hit" ] && printf '%s\n' "$out" | grep -qx 'org\.vinegarhq\.Sober' && hit="flatpak ps lists org.vinegarhq.Sober"
  else unk="${unk:+$unk; }flatpak ps exited $r"; fi
  out=$(timeout 10 "$BOX_SYSTEMCTL" --user list-units --no-legend --plain --state=active 'app-flatpak-org.vinegarhq.Sober-*.scope' 2>/dev/null </dev/null); r=$?
  if [ $r = 0 ]; then
    [ -z "$hit" ] && [ -n "$out" ] && hit="an active Sober scope (${out%% *})"
  else unk="${unk:+$unk; }systemctl --user list-units exited $r"; fi
  "$BOX_PGREP" -x sober >/dev/null 2>&1 </dev/null; r=$?
  case $r in 0) [ -z "$hit" ] && hit="a process named sober" ;; 1) ;; *) unk="${unk:+$unk; }pgrep -x sober exited $r" ;; esac
  [ -n "$hit" ] && { BOX_WHY="Sober (Roblox) is running: $hit"; return 1; }
  [ -n "$unk" ] && { BOX_WHY="cannot tell whether Sober (Roblox) runs: $unk"; return 2; }
  BOX_WHY="Sober (Roblox) not running (cgroups, .flatpak-info, flatpak ps, systemd scopes, pgrep)"; return 0
}

_box_engine_cands() {   # sets _BOX_CANDS (Wine pids whose argv names the run tree's bin); 0 ok | 2 cannot tell
  local r out p e
  _BOX_CANDS=""
  out=$("$BOX_PGREP" -f "$BOX_RUN/bin/|${BOX_RUN//\//\\\\}\\\\bin" 2>/dev/null </dev/null); r=$?
  case $r in 0|1) ;; *) BOX_WHY="pgrep -f <run tree>/bin exited $r: cannot tell"; return 2 ;; esac
  for p in $out; do
    case "$p" in ''|*[!0-9]*) BOX_WHY="pgrep printed '$p', not a pid: cannot tell"; return 2 ;; esac
    e=$(readlink "$BOX_PROC/$p/exe" 2>/dev/null) || continue   # gone, or not ours to read: a Wine engine of ours is readable
    case "$e" in *wine*) _BOX_CANDS="$_BOX_CANDS $p" ;; esac
  done
  return 0
}

box_ok_engine() {   # an engine started from the run tree's bin (the play launcher's pattern, caden_simtick_play.sh:21),
                    # counted only if its binary is a Wine loader (tools/lib_engine_pids.sh, Overseer 2026-11-23)
  _box_engine_cands || return 2
  [ -n "$_BOX_CANDS" ] && { BOX_WHY="an engine from $BOX_RUN/bin is running (pid${_BOX_CANDS}): Caden's, or another job's"; return 1; }
  BOX_WHY="no engine of $BOX_RUN running"; return 0
}

box_engines_in_prefix() {   # <prefix>: the run tree's Wine engines that use <prefix> (environ unreadable = counted)
  local pf=${1:-} p env wp
  BOX_LEFT_PIDS=""
  [ -n "$pf" ] || { BOX_WHY="box_engines_in_prefix needs a prefix"; return 2; }
  _box_engine_cands || return 2
  for p in $_BOX_CANDS; do
    env=$(tr '\0' '\n' < "$BOX_PROC/$p/environ" 2>/dev/null) || { [ -e "$BOX_PROC/$p" ] && BOX_LEFT_PIDS="$BOX_LEFT_PIDS $p(environ unreadable)"; continue; }
    wp=$(printf '%s\n' "$env" | sed -n 's/^WINEPREFIX=//p' | head -1)
    if [ -z "$wp" ]; then BOX_LEFT_PIDS="$BOX_LEFT_PIDS $p(no WINEPREFIX)"; elif [ "$wp" = "$pf" ]; then BOX_LEFT_PIDS="$BOX_LEFT_PIDS $p"; fi
  done
  [ -n "$BOX_LEFT_PIDS" ] && { BOX_WHY="engine(s) of the run tree in $pf still running: pid$BOX_LEFT_PIDS"; return 1; }
  BOX_WHY="no engine of the run tree uses $pf"; return 0
}

box_ok_hostns() {   # review N-10: from a pid-namespaced sandbox the lock pid, Sober and the engine all read as absent
  local c
  c=$(head -c 32 -- "$BOX_PROC/1/comm" 2>/dev/null | tr -d '\n')
  [ "$c" = systemd ] && { BOX_WHY="host pid namespace ($BOX_PROC/1 is systemd)"; return 0; }
  BOX_WHY="$BOX_PROC/1 is '${c:-unreadable}', not systemd: not the host pid namespace, every pid check would read clear"; return 2
}

_box_line_live() {   # <slot line> <now>: 0 live | 1 expired | 2 unparsable (the claim tool's own reading, quiet-required.sh:375-405)
  local d1 d2 wk m rest t
  read -r d1 d2 wk m rest <<< "$1"
  t=$(date -d "$d1 $d2" +%s 2>/dev/null) || return 2
  [ -n "$wk" ] || return 2
  case "$m" in ''|*[!0-9]*) m=60 ;; esac   # digits only from here, so 10# below is safe (N3-7: '090' is not octal)
  _BOX_LW=$wk; _BOX_LWHAT=$rest
  [ $(( $2 - t )) -gt $(( 10#$m * 60 )) ] && return 1
  return 0
}

_box_records() {   # sets BOX_RECS ("<slot> <worker> <what>" per LIVE claim record) and BOX_LIVE_LINES; 0 | 2 cannot tell
  # Read from the claim tool's files, never through a verb: slot 1 = QUIET-REQUIRED, slot n = QUIET-REQUIRED-<n>
  # (*.expired and other suffixes are not slots). A slot's records are quiet-claims/<slot> when its first line is
  # "#slot <the slot line>" (record: <id> <worker> <start epoch> <mins> <owner> <excl> <what...>, live while
  # start + mins*60 > now, quiet-required.sh:464-526); otherwise the slot line is ONE legacy claim, as the tool reads it.
  local f s l rf recs now
  BOX_RECS=""; BOX_LIVE_LINES=""; now=$(date +%s)
  for f in "$BOX_QUIET_DIR/QUIET-REQUIRED" "$BOX_QUIET_DIR"/QUIET-REQUIRED-*; do
    s=1
    case "$f" in "$BOX_QUIET_DIR/QUIET-REQUIRED") ;; *) s=${f##*/QUIET-REQUIRED-}; case "$s" in ''|*[!0-9]*) continue ;; esac ;; esac
    { [ -e "$f" ] || [ -L "$f" ]; } || continue
    { [ -f "$f" ] && [ -r "$f" ]; } || { BOX_WHY="$f is not a readable file: cannot tell"; return 2; }
    l=$(head -n 1 -- "$f" 2>/dev/null) || { BOX_WHY="$f cannot be read: cannot tell"; return 2; }
    l=${l%$'\r'}
    [ -n "$l" ] || continue
    _box_line_live "$l" "$now"; case $? in
      1) continue ;;
      2) BOX_WHY="$f: claim line '${l:0:60}' is not '<date> <time> <worker> <mins> <what>': cannot tell"; return 2 ;;
    esac
    BOX_LIVE_LINES="$BOX_LIVE_LINES$s $l"$'\n'
    recs=""; rf="$BOX_QUIET_DIR/quiet-claims/$s"
    if [ -e "$rf" ] || [ -L "$rf" ]; then
      { [ -f "$rf" ] && [ -r "$rf" ]; } || { BOX_WHY="$rf is not a readable file: cannot tell"; return 2; }
      if [ "$(head -n 1 -- "$rf" 2>/dev/null)" = "#slot $l" ]; then
        recs=$(tail -n +2 -- "$rf" 2>/dev/null | awk -v n="$now" 'NF >= 7 && $3 ~ /^[0-9]+$/ && $4 ~ /^[0-9]+$/ && $3 + $4 * 60 > n {
          s = $0; for (i = 1; i <= 6; i++) sub(/^[^ ]+ /, "", s); print $2 " " s }')
      fi
    fi
    [ -n "$recs" ] || recs="$_BOX_LW $_BOX_LWHAT"   # legacy slot line (or no live record bound): one claim
    BOX_RECS="$BOX_RECS$(printf '%s\n' "$recs" | sed "s/^/$s /")"$'\n'
  done
  return 0
}

_box_count_claims() {   # <worker> <token>: sets _BOX_N (records under <worker>), _BOX_MINE (carrying <token>), _BOX_OTHER
  local w=$1 tok=$2 s wk what
  _BOX_N=0; _BOX_MINE=0; _BOX_OTHER=""
  while read -r s wk what; do
    [ -n "$s" ] && [ "$wk" = "$w" ] || continue
    _BOX_N=$((_BOX_N + 1))
    if [ -n "$tok" ]; then case "$what" in *"$tok"*) _BOX_MINE=$((_BOX_MINE + 1)); continue ;; esac; fi
    case "$what" in *"handoff pre-claim"*) continue ;; esac   # the queue's pre-claim for a woken head (sf1/kj4p w_holds)
    [ -z "$_BOX_OTHER" ] && _BOX_OTHER="slot $s: ${what:0:100}"
  done <<< "$BOX_RECS"
}

box_ok_claims() {   # <worker> [<own token>]: every live claim RECORD under <worker>, nested ones included (review S-7)
  local w=${1:-} tok=${2:-}
  [ -n "$w" ] || { BOX_WHY="box_ok_claims needs a worker name"; return 2; }
  _box_records || return 2
  _box_count_claims "$w" "$tok"
  if [ -n "$_BOX_OTHER" ]; then
    BOX_WHY="a live claim under '$w' is held that is not this run's ($_BOX_OTHER; $_BOX_N record(s) under '$w', $_BOX_MINE carry this run's token${tok:+ $tok}): another job of ours holds the box"; return 1; fi
  BOX_WHY="no other live claim under '$w' ($_BOX_N record(s) under '$w', $_BOX_MINE carry this run's token)"; return 0
}

box_claim_has_token() {   # <worker> <token>
  local w=${1:-} tok=${2:-}
  { [ -n "$w" ] && [ -n "$tok" ]; } || { BOX_WHY="box_claim_has_token needs a worker and a token"; return 2; }
  _box_records || return 2
  _box_count_claims "$w" "$tok"
  [ "$_BOX_MINE" -gt 0 ] && { BOX_WHY="a claim under '$w' carries $tok ($_BOX_N record(s) under '$w')"; return 0; }
  BOX_WHY="no claim under '$w' carries $tok ($_BOX_N record(s) under '$w')"; return 1
}

box_ok_exclusive() {   # a LIVE slot line naming EXCLUSIVE (the tool appends EXCLUSIVE when any nested record is; an
                       # expired slot no longer refuses, review N-11; a handoff pre-claim naming EXCLUSIVE still does)
  local s l n=0
  _box_records || return 2
  while read -r s l; do
    [ -n "$s" ] || continue
    n=$((n + 1))
    case "$l" in *EXCLUSIVE*) BOX_WHY="claim slot $s names EXCLUSIVE: ${l:0:160}"; return 1 ;; esac
  done <<< "$BOX_LIVE_LINES"
  BOX_WHY="no live claim slot names EXCLUSIVE ($n live)"; return 0
}

box_queue_count() {   # <worker>: BOX_QN = entries of <worker> in QUIET-QUEUE (first field, as the tool's q_has)
  local q="$BOX_QUIET_DIR/QUIET-QUEUE"
  BOX_QN=0
  { [ -e "$q" ] || [ -L "$q" ]; } || { BOX_WHY="no queue file"; return 0; }
  { [ -f "$q" ] && [ -r "$q" ]; } || { BOX_WHY="$q is not a readable file: cannot tell"; return 2; }
  BOX_QN=$(QW="${1:-}" awk '$1 == ENVIRON["QW"] {n++} END {print n + 0}' "$q" 2>/dev/null) || { BOX_WHY="$q cannot be read"; return 2; }
  BOX_WHY="$BOX_QN queue entr(y/ies) under '${1:-}'"; return 0
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

box_fresh() {   # review B-1: re-check just before a write. The play lock is read LAST, so it is the line before the write.
  box_ok_title     || { BOX_WHY="protected_title: $BOX_WHY"; return 1; }
  box_ok_roblox    || { BOX_WHY="roblox: $BOX_WHY"; return 1; }
  box_ok_engine    || { BOX_WHY="engine_running: $BOX_WHY"; return 1; }
  box_ok_play_lock || { BOX_WHY="play_lock: $BOX_WHY"; return 1; }
  return 0
}

box_guarded() {   # <what> <command...>
  local what=${1:-?}; shift
  if [ -n "$BOX_STOPPED" ]; then
    BOX_SKIPPED="$BOX_SKIPPED${BOX_SKIPPED:+; }$what"; BOX_WHY="skipped: a stop is recorded ($BOX_STOPPED)"; return 1; fi
  if ! box_fresh; then
    BOX_STOPPED="before '$what': $BOX_WHY"; BOX_SKIPPED="$BOX_SKIPPED${BOX_SKIPPED:+; }$what"; return 1; fi
  "$@"
}

box_rm_name() {   # <dir> <name>
  local d=${1:-} n=${2:-}
  case "$d" in /?*) ;; *) BOX_WHY="rm refused: folder '$d' is not absolute"; return 2 ;; esac
  { [ -d "$d" ] && ! [ -L "$d" ] && [ "$(realpath -e -- "$d" 2>/dev/null)" = "$d" ]; } || {
    BOX_WHY="rm refused: '$d' is not a real folder that is its own realpath"; return 2; }
  case "$n" in ''|.|..|*[!A-Za-z0-9_.-]*) BOX_WHY="rm refused: '$n' is not one file name of [A-Za-z0-9_.-]"; return 2 ;; esac
  [ -d "$d/$n" ] && ! [ -L "$d/$n" ] && { BOX_WHY="rm refused: $d/$n is a folder"; return 2; }
  rm -f -- "$d/$n"
}

box_dumps_newer() {   # <dir> <marker>
  [ -e "${2:-}" ] || { BOX_WHY="no start marker '${2:-}': no dump can be called this boot's"; return 2; }
  find "$1" -maxdepth 1 -type f -name '*.mdmp' -newer "$2" -printf '%f\n' 2>/dev/null
}

box_wine_kill() {   # <prefix> [nowait]
  local pf=${1:-} mode=${2:-} r
  case "$mode" in ''|nowait) ;; *) BOX_WHY="wineserver -k NOT run: unknown mode '$mode'"; return 2 ;; esac
  case "$pf" in /?*/run/prefix) ;; *) BOX_WHY="wineserver -k NOT run: '$pf' is not an absolute <...>/run/prefix"; return 2 ;; esac
  case "$pf" in *prefix_play*) BOX_WHY="wineserver -k NOT run: '$pf' names prefix_play"; return 2 ;; esac
  { [ -d "$pf" ] && ! [ -L "$pf" ] && [ "$(realpath -e -- "$pf" 2>/dev/null)" = "$pf" ]; } || {
    BOX_WHY="wineserver -k NOT run: '$pf' is not a real folder that is its own realpath"; return 2; }
  if [ "$mode" = nowait ]; then
    WINEPREFIX="$pf" timeout 10 "$BOX_WINESERVER" -k >/dev/null 2>&1 </dev/null; r=$?
    BOX_WHY="wineserver -k in $pf (rc $r, no -w)"; return 0; fi
  WINEPREFIX="$pf" timeout 30 "$BOX_WINESERVER" -k >/dev/null 2>&1 </dev/null; r=$?
  WINEPREFIX="$pf" timeout 30 "$BOX_WINESERVER" -w >/dev/null 2>&1 </dev/null
  BOX_WHY="wineserver -k in $pf (rc $r)"; return 0
}

box_is_own() {   # <pid> <name> <parent pid>: 0 = a live (not zombie) process named <name> whose parent is <parent pid>
  local p=${1:-} c=${2:-} par=${3:-} st cm f=()
  case "$p" in ''|*[!0-9]*) BOX_WHY="no pid"; return 1 ;; esac
  st=$(cat "$BOX_PROC/$p/stat" 2>/dev/null) || { BOX_WHY="pid $p is gone"; return 1; }
  cm=${st#*(}; cm=${cm%)*}
  st=${st##*) }; read -r -a f <<< "$st"   # state, parent, ... (split without globbing)
  if [ "${f[0]:-}" = Z ] || [ "${f[1]:-}" != "$par" ] || [ "$cm" != "$c" ]; then
    BOX_WHY="pid $p is not our $c (state ${f[0]:-?}, parent ${f[1]:-?}, name '$cm')"; return 1; fi
  BOX_WHY="pid $p is our live $c"; return 0
}

box_kill_own() {   # <pid> <name> <parent pid>
  local p=${1:-} c=${2:-}
  case "$p" in ''|*[!0-9]*) BOX_WHY="no pid to kill"; return 1 ;; esac
  [ -e "$BOX_PROC/$p/stat" ] || { BOX_WHY="pid $p is gone: nothing killed"; return 1; }
  box_is_own "$@" || { BOX_WHY="$BOX_WHY: NOT killed"; return 1; }
  "$BOX_KILL" "$p" 2>/dev/null; BOX_WHY="killed our $c (pid $p)"; return 0
}

_box_say() { BOX_OUT="$BOX_OUT${BOX_OUT:+$'\n'}$1"; }
box_preflight_all() {   # <class shared|exclusive> <display> <worker> [<own token>]; lines printed AND left in BOX_OUT
  local cls=${1:-} disp=${2:-} w=${3:-} tok=${4:-} worst=0 r name
  BOX_OUT=""
  [ -n "$BOX_OVERRIDES" ] && _box_say "BOX OVERRIDES: $BOX_OVERRIDES"
  case "$cls" in shared|exclusive) ;; *) _box_say "BOX REFUSED: claim_class: '$cls' is not shared or exclusive"; printf '%s\n' "$BOX_OUT"; return 2 ;; esac
  for name in host_pid_ns play_lock protected_title roblox engine_running own_claim exclusive display; do
    case $name in
      host_pid_ns)     box_ok_hostns ;;
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
      _box_say "BOX REFUSED: $name: $BOX_WHY"
      [ $r = 1 ] && [ $worst = 0 ] && worst=1
      [ $r != 1 ] && worst=2
    fi
  done
  [ -n "$BOX_OUT" ] && printf '%s\n' "$BOX_OUT"
  return $worst
}
