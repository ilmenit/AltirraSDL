"""Install the AltirraBridge Claude Code skill into a project.

Claude Code discovers user-defined skills under ``.claude/skills/``
in the user's project directory (or ``~/.claude/skills/`` for
user-wide installation). This module finds the bundled skill
inside the installed ``altirra_bridge`` package and copies it
into one of those locations.

CLI usage::

    python -m altirra_bridge.install_skills              # → ./.claude/skills/
    python -m altirra_bridge.install_skills --user       # → ~/.claude/skills/
    python -m altirra_bridge.install_skills --dest /path # → /path/altirra-bridge/

Programmatic usage::

    from altirra_bridge.install_skills import install
    install(dest="./.claude/skills")

The installer copies the entire ``skills/altirra-bridge/`` tree
(``SKILL.md`` + ``references/``) so progressive disclosure works.
It does not modify any other files in the destination.
"""

from __future__ import annotations

import argparse
import os
import shutil
import sys
from pathlib import Path
from typing import Optional


# ---------------------------------------------------------------------------
# Locating the bundled skill
# ---------------------------------------------------------------------------

def _find_bundled_skill() -> Optional[Path]:
    """Locate the ``skills/altirra-bridge/`` directory shipped with
    the package.

    Search order, in decreasing preference:

    1. ``$ALTIRRA_BRIDGE_SKILLS`` environment override (for tests
       and unusual install layouts).
    2. ``../../skills/altirra-bridge/`` relative to this file —
       the layout when running from the source tree (the
       ``AltirraBridge/`` repo subfolder ships ``sdk/python/`` and
       ``skills/`` as siblings).
    3. ``<sys.prefix>/share/altirra_bridge/skills/altirra-bridge/``
       — the layout for ``pip install`` with a data-files entry.

    Returns the resolved path or ``None`` if no candidate exists.
    """
    env = os.environ.get("ALTIRRA_BRIDGE_SKILLS")
    if env and Path(env).is_dir():
        return Path(env).resolve()

    here = Path(__file__).resolve()
    # altirra_bridge/install_skills.py → altirra_bridge/ → python/
    # → sdk/ → AltirraBridge/ → skills/altirra-bridge/
    candidate = here.parent.parent.parent.parent / "skills" / "altirra-bridge"
    if candidate.is_dir():
        return candidate

    candidate = Path(sys.prefix) / "share" / "altirra_bridge" / "skills" / "altirra-bridge"
    if candidate.is_dir():
        return candidate

    return None


# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------

def install(
    dest:    Optional[str] = None,
    user:    bool          = False,
    force:   bool          = False,
) -> Path:
    """Copy the bundled ``altirra-bridge`` skill into ``dest``.

    Exactly one of ``dest`` and ``user`` should be set:

    - ``dest=PATH`` — copy into ``PATH/altirra-bridge/``.
    - ``user=True`` — copy into ``~/.claude/skills/altirra-bridge/``.
    - neither — copy into ``./.claude/skills/altirra-bridge/``
      (the project-local default).

    If the destination already contains a previous installation,
    raises ``FileExistsError`` unless ``force=True``.

    Returns the absolute path to the installed skill.
    """
    src = _find_bundled_skill()
    if src is None:
        raise FileNotFoundError(
            "altirra-bridge skill not found. Set ALTIRRA_BRIDGE_SKILLS to its "
            "directory, or reinstall altirra_bridge with the skills/ tree "
            "available alongside sdk/python/.")

    if dest is None:
        if user:
            base = Path.home() / ".claude" / "skills"
        else:
            base = Path.cwd() / ".claude" / "skills"
    else:
        base = Path(dest)

    target = base / "altirra-bridge"

    if target.exists():
        if not force:
            raise FileExistsError(
                f"{target} already exists; pass force=True or --force to "
                f"overwrite.")
        shutil.rmtree(target)

    base.mkdir(parents=True, exist_ok=True)
    shutil.copytree(src, target)
    return target.resolve()


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def _build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="python -m altirra_bridge.install_skills",
        description="Install the AltirraBridge Claude Code skill.",
        epilog="With no arguments, installs into ./.claude/skills/altirra-bridge/.",
    )
    p.add_argument("--dest", metavar="PATH",
                   help="install into PATH/altirra-bridge/")
    p.add_argument("--user", action="store_true",
                   help="install into ~/.claude/skills/altirra-bridge/")
    p.add_argument("--force", action="store_true",
                   help="overwrite an existing installation")
    return p


def main(argv: Optional[list] = None) -> int:
    args = _build_parser().parse_args(argv)
    if args.dest and args.user:
        print("error: --dest and --user are mutually exclusive",
              file=sys.stderr)
        return 2
    try:
        path = install(dest=args.dest, user=args.user, force=args.force)
    except FileExistsError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    except FileNotFoundError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    print(f"installed altirra-bridge skill to: {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
