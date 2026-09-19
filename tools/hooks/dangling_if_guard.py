#!/usr/bin/env python3
"""dangling_if_guard — refuse a commit that inserts a statement under an UNBRACED if/for/while.

WHY THIS EXISTS. 808a1d18 added a gated diagnostic under a bare `if` in
CALifeUpdateManager::teleport_object and did not add braces:

    if (object->m_bOnline)
        if (strstr(Core.Params, "-coop_anchordump"))
            Msg("[SQCALLER] switch_offline(object %d) ...", object->ID);
        switch_offline(object);          <-- NO LONGER GUARDED

The `m_bOnline` guard now covers only the Msg. switch_offline ran on already-offline objects and tripped its
own R_ASSERT(m_bOnline): the dedicated server crashed on 6 boots out of 6, ~40 s in, identically with the
diagnostic flag ON and OFF -- because the flag only ever gated the Msg, never the call. The commit message
said "Diagnostic only, no behaviour change."

It also SUPPRESSED ITS OWN EVIDENCE. With m_bOnline false the outer `if` is false, so no line is logged, and
then the unconditional call asserts. The run showed zero [SQCALLER] lines next to a proven switch_offline
call, which was read as a fifth uninstrumented caller and written up as a finding before the reproduction
caught it. A whole investigation was spent on an artefact of this one missing pair of braces.

WHAT IT CHECKS. In each staged C++ file, a control line `if (...)` / `for (...)` / `while (...)` with no
trailing `{`, whose body is a single unbraced statement, followed by a FURTHER statement at the SAME
indentation as that body. That trailing statement looks like part of the body and is not.

ONLY NEW SITES. The offending sites in the staged content are compared against those in HEAD; the commit is
refused only for a site that was not already there. The tree has legacy instances, and a hook that blocks
every commit until they are all clean is a hook that gets switched off.
"""
import re
import subprocess
import sys

CONTROL = re.compile(r'^([ \t]*)(?:\}\s*)?(?:else\s+)?(if|for|while)\s*\(.*\)\s*$')
COMMENT = re.compile(r'^[ \t]*(//|/\*|\*)')


def indent_width(s):
    """Tabs are the tree's indent unit; count them and spaces alike, but consistently."""
    n = 0
    for ch in s:
        if ch == '\t':
            n += 4
        elif ch == ' ':
            n += 1
        else:
            break
    return n


def code_lines(lines):
    """Indices of lines that are neither blank, nor comment-only, nor preprocessor."""
    out = []
    for i, ln in enumerate(lines):
        s = ln.strip()
        if not s or COMMENT.match(ln) or s.startswith('#'):
            continue
        out.append(i)
    return out


def find_sites(text):
    """Return {signature} for each dangling-body site. Signature is text, not line number, so it
    survives the line shifts an ordinary edit causes."""
    lines = text.split('\n')
    idx = code_lines(lines)
    pos = {ln: k for k, ln in enumerate(idx)}
    sites = set()

    for i in idx:
        m = CONTROL.match(lines[i])
        if not m:
            continue
        # A WRAPPED CONDITION IS NOT A CONTROL LINE. `if ((a & B) && !(c & D)` ends in ')' from an inner
        # paren while the condition continues on the next line; treating it as complete flagged correct
        # code in HWCaps.cpp and dx10StateManager.cpp. Require the parentheses to balance.
        if lines[i].count('(') != lines[i].count(')'):
            continue
        ctrl_ind = indent_width(m.group(1))
        k = pos[i]
        if k + 1 >= len(idx):
            continue
        b = idx[k + 1]
        body = lines[b]
        body_ind = indent_width(body)
        # braced body, or a body that is not actually indented under the control line: not our shape
        if body.strip().startswith('{') or body_ind <= ctrl_ind:
            continue
        # consume the body statement, which may span lines (a wrapped Msg(...) call, for instance)
        j = k + 1
        while j < len(idx):
            st = lines[idx[j]].strip()
            if st.endswith(';') or st.endswith('}'):
                break
            # a nested unbraced control line continues the same statement
            j += 1
        if j + 1 >= len(idx):
            continue
        nxt = lines[idx[j + 1]]
        nxt_s = nxt.strip()
        if nxt_s.startswith(('}', 'else', '#')):
            continue
        # A PREPROCESSOR BRANCH IS NOT A SEQUENCE. `for (...) \n #ifdef X \n a; \n #else \n b;` compiles to
        # ONE body, but with the '#' lines skipped it reads as two statements at the same indent. Seen in
        # dx10StateManager.cpp. If anything between the control line and the candidate is a directive, the
        # indentation is not evidence of anything.
        if any(lines[n].lstrip().startswith('#') for n in range(i, idx[j + 1] + 1)):
            continue
        # THE SHAPE: the next statement sits at exactly the body's indentation, so it reads as part of
        # the body and is not.
        if indent_width(nxt) == body_ind:
            sites.add('%s  >>>  %s' % (lines[i].strip(), nxt_s))
    return sites


def git(*args):
    r = subprocess.run(['git'] + list(args), capture_output=True, text=True)
    return r.stdout if r.returncode == 0 else None


def main():
    out = git('diff', '--cached', '--name-only', '--diff-filter=ACM', '--',
              '*.cpp', '*.h', '*.inl')
    if not out:
        return 0
    files = [f for f in out.splitlines() if f and not f.startswith('src/3rd party/')]
    bad = []
    for f in files:
        new = git('show', ':' + f)
        if new is None:
            continue
        old = git('show', 'HEAD:' + f) or ''
        for sig in sorted(find_sites(new) - find_sites(old)):
            bad.append((f, sig))

    if not bad:
        return 0

    print('pre-commit: REFUSED — a statement was inserted under an UNBRACED if/for/while.', file=sys.stderr)
    print('pre-commit: the line after the body reads as part of it but is NOT guarded.', file=sys.stderr)
    print('pre-commit: this is 808a1d18, which crashed the dedicated server 6 boots out of 6', file=sys.stderr)
    print('pre-commit: and hid its own log line while doing it. Add the braces.', file=sys.stderr)
    for f, sig in bad:
        print('pre-commit:   %s\n pre-commit:     %s' % (f, sig), file=sys.stderr)
    return 1


if __name__ == '__main__':
    sys.exit(main())
