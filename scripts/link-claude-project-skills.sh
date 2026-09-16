#!/usr/bin/env bash
# Make Claude Code see the same skills as Codex / ZCode / Cursor.
#
# Claude Code only scans <cwd>/.claude/skills/ plus the same path in parent
# directories, and it never reads .agents/skills/ or .cursor/skills/. The repo
# root .claude used to be a symlink to ../.claude, which gave Claude Code the
# workspace skills but left no place for this repo's own skills.
#
# Two different rules apply depending on where Claude Code runs, so the layout
# has to satisfy both:
#
#   * WSL / Linux: parent-directory scanning stops at the git root, so
#     ../.claude is invisible. Linux symlinks resolve normally.
#   * Windows against \\wsl.localhost: the walk does reach ../.claude, but
#     Windows cannot follow Linux symlinks at all.
#
# Therefore: real copies for everything Claude Code cannot get any other way
# (this repo's own skills), and symlinks for what the Windows parent scan
# already provides (the openvela workspace skills, commands and agents). The
# symlinks resolve in WSL and are skipped as unreadable on Windows, so neither
# platform ends up with duplicates.
#
# Re-run this after editing anything under .agents/skills/.
set -eu

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$REPO_ROOT/.agents/skills"
DEST="$REPO_ROOT/.claude/skills"
WS_CLAUDE="$(cd "$REPO_ROOT/.." && pwd)/.claude"

if [ -L "$REPO_ROOT/.claude" ]; then
  cat >&2 <<EOF
ERROR: $REPO_ROOT/.claude is still a symlink to ../.claude.
Remove it first, then re-run this script:
  rm '$REPO_ROOT/.claude'          # restore with: ln -s ../.claude .claude
EOF
  exit 1
fi

if [ ! -d "$SRC" ]; then
  echo "ERROR: $SRC not found. Run this from inside the contest repo." >&2
  exit 1
fi

mkdir -p "$DEST"

# --- 1. This repo's skills: real copies, readable from both platforms --------

wanted=""
skipped=0
for path in "$SRC"/*/; do
  name="$(basename "$path")"
  case "$name" in
    trellis-*)
      # Claude Code gets /trellis:continue and /trellis:finish-work from
      # ../.claude/commands/, and CLAUDE.md documents that /trellis:start
      # intentionally does not exist here. These copies would add a second,
      # conflicting command name.
      continue
      ;;
  esac
  if [ ! -f "$SRC/$name/SKILL.md" ]; then
    echo "skip $name: no SKILL.md" >&2
    skipped=$((skipped + 1))
    continue
  fi
  if [ -e "$WS_CLAUDE/skills/$name/SKILL.md" ]; then
    echo "skip $name: already provided by ../.claude/skills" >&2
    skipped=$((skipped + 1))
    continue
  fi
  wanted="$wanted $name"
done

for name in $wanted; do
  rm -rf "${DEST:?}/$name"
  cp -a "$SRC/$name" "$DEST/$name"
  printf 'Synced from .agents/skills/%s — edit that copy, not this one.\n' "$name" \
    > "$DEST/$name/.synced-from"
done

# --- 2. Workspace skills: symlinks, for the WSL scan that cannot walk up -----

ws_linked=0
if [ -d "$WS_CLAUDE/skills" ]; then
  for path in "$WS_CLAUDE"/skills/*/; do
    [ -e "$path" ] || continue
    name="$(basename "$path")"
    case " $wanted " in
      *" $name "*) continue ;;
    esac
    ln -sfn "../../../.claude/skills/$name" "$DEST/$name"
    ws_linked=$((ws_linked + 1))
  done
fi

# --- 3. Prune entries this script no longer owns ----------------------------

for path in "$DEST"/*/; do
  [ -e "$path" ] || continue
  name="$(basename "$path")"
  case " $wanted " in
    *" $name "*) continue ;;
  esac
  if [ -e "$WS_CLAUDE/skills/$name" ]; then
    continue
  fi
  echo "prune $name: no longer mirrored"
  rm -rf "${DEST:?}/$name"
done
# Stale links left over from an earlier run of this script.
find "$DEST" -maxdepth 1 -xtype l -delete

# --- 4. Commands and agents: same symlink rule ------------------------------

for entry in commands agents; do
  if [ -d "$WS_CLAUDE/$entry" ]; then
    ln -sfn "../../.claude/$entry" "$REPO_ROOT/.claude/$entry"
  fi
done

# The workspace settings carry the trellis hooks and status line. Claude Code
# reads settings.json only from <cwd>/.claude/, never from a parent directory.
if [ -d "$WS_CLAUDE" ]; then
  ln -sfn "../../.claude/settings.json" "$REPO_ROOT/.claude/settings.json"
  ln -sfn "../../.claude/hooks" "$REPO_ROOT/.claude/hooks"
fi

echo "Mirrored$(printf ' %s' $wanted) as copies into $DEST"
echo "Linked $ws_linked workspace skills plus commands/agents as symlinks (skipped $skipped)"
