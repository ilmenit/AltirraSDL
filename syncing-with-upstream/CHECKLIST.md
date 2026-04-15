# Sync Checklist

Short form of `GUIDE.md`. Tick each box in the PR description.

## Preparation
- [ ] OLD snapshot folder present **next to the AltirraSDL repo** as a sibling directory (e.g. `../Altirra-<old-ver>-src/`)
- [ ] NEW snapshot folder present next to AltirraSDL as a sibling directory
- [ ] Clean/committed working tree
- [ ] New branch: `sync/altirra-<new-version>`

## Diff
- [ ] `./tools/sync_diff.sh --old <OLD> --new <NEW> --fork ..` ran cleanly
- [ ] Skimmed `reports/<OLD>__to__<NEW>/SUMMARY.md`
- [ ] Skimmed `reports/<OLD>__to__<NEW>/00_changelog.txt`
- [ ] Inspected unexpected entries in `02_fork_changed.txt`

## Apply
- [ ] `./tools/apply_trivial.py reports/<OLD>__to__<NEW>` succeeded
- [ ] Committed trivial copies on their own (`sync(upstream): copy …`)
- [ ] Three-way files resolved module by module via `07_classified.md`
       (optionally driven by `prompts/PROMPT.md` + LLM agent)
- [ ] Cross-platform core changes ported to FORK
- [ ] UI changes (cmd/ui files) reflected in `src/AltirraSDL/source/ui/`
- [ ] Gaming Mode (`mobile/`) updated only for gameplay-relevant changes

## Verify
- [ ] `./build.sh` builds cleanly
- [ ] SDL binary launches and loads a ROM
- [ ] Kernel ROM rebuilt if `Kernel/source/Shared/version.inc` changed
- [ ] UI test-mode smoke test (`--test-mode`) ping/query_state OK

## Record
- [ ] Appended one-line entry to `HISTORY.md`
- [ ] PR description links to `SUMMARY.md` and lists the 3-way files touched
