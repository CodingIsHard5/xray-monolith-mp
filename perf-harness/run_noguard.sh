#!/usr/bin/env bash
# No-link guard (Overseer order 2026-11-22, after the axr_options.ltx write-through into Caden's GAMMA install).
# Exit 0 only if nothing under the run tree can write into ~/Games:
#   (1) no symlink under run/ resolves into ~/Games unless it is a read-only binary asset (extension list below);
#   (2) no regular file under run/ has a hard-link count above 1.
# Usage: tools/run_noguard.sh [run dir]   (prints the first offenders and exits 7 on failure)
RUN="${1:-$(cd "$(dirname "$0")/.." && pwd)/run}"; G="${NOGUARD_GAMES:-$HOME/Games}"
ASSETS="dds ogf omf ogg wav thm anm skl skls bmp tga png jpg jpeg ppm avi ogm mp4 mp3 bik db0 db1 db2 db3 db4 db5 db6 db7 db8 db9 dbmap db geom geomx cform spawn ai gct graph hom details som ps_static wallmarks snd_static level game blend blend1 aup3 object pdn xcf psd ico exe dll rar zip 7z sfk"
bad=$(find "$RUN" -type l -printf '%p\t%l\n' 2>/dev/null | awk -F'\t' -v g="$G" -v a="$ASSETS" '
  BEGIN{n=split(a,x," "); for(i=1;i<=n;i++) ok[x[i]]=1}
  index($2,g"/")==1 { b=$1; sub(/.*\//,"",b); e=(b ~ /\./) ? tolower(b) : "(none)"; sub(/.*\./,"",e)
    if (!(e in ok) && $1 !~ /\/db\/[^\/]*$/) print "link into games: " $1 }' | head -5)
hl=$(find "$RUN" -type f -links +1 2>/dev/null | head -5 | sed 's/^/hard link: /')
[ -z "$bad$hl" ] && { echo "noguard ok: no writable path from $RUN into $G"; exit 0; }
echo "REFUSED by no-link guard:"; printf '%s\n' "$bad" "$hl" | grep .; exit 7
