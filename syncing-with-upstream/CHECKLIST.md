# Sync Checklist

Short form of `GUIDE.md`. Tick each box in the PR description.

## Preparation
- [ ] OLD snapshot folder present **next to the AltirraSDL repo** as a sibling directory (e.g. `../Altirra-<old-ver>-src/`)
- [ ] NEW snapshot folder present next to AltirraSDL as a sibling directory
- [ ] OLD and NEW are source snapshots with non-empty `src/` trees, not binary release packages
- [ ] Clean/committed working tree
- [ ] New branch: `sync/altirra-<new-version>`

## Diff
- [ ] `./tools/sync_diff.sh --old <OLD> --new <NEW> --fork ..` ran cleanly
- [ ] Report directory does not contain `INVALID_REPORT_DO_NOT_USE.md`
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
- [ ] Mandatory constexpr and portability audits in `../AGENTS.md` and `../docs/merging-with-altirra-mainline.md` completed
- [ ] Added aggregate construction, standalone test header ordering and OS-specific path assertions reviewed
- [ ] Every upstream regression target built and run (commands in `../tests/upstream/README.md`)
- [ ] Linux normal/sanitizer and native Windows/macOS regression jobs pass on the exact PR head
- [ ] All affected platform build jobs pass; external package-service failures inspected and retried as appropriate
- [ ] `./build.sh` builds cleanly
- [ ] SDL binary launches and loads a ROM
- [ ] Kernel ROM rebuilt if `Kernel/source/Shared/version.inc` changed
- [ ] UI test-mode smoke test (`--test-mode`) ping/query_state OK

## Record
- [ ] Appended one-line entry to `HISTORY.md` only after the merge and validation are complete
- [ ] Updated documented baseline/current OLD reference only after successful validation
- [ ] PR description links to `SUMMARY.md` and lists the 3-way files touched
