---
name: codecraft-development-validate
description: Validate repository contracts, generated indexes, native builds and evidence.
---

# Validate

Select checks using the [root validation policy](../../AGENTS.md).
Run `generate --check`, focused checks and `validate --static`. If firmware and
native build inputs/behavior are unchanged, skip native builds and toolchain
setup. Otherwise validate affected Targets, examples and variants, including
native project builds when export contents or build behavior changes. No-option
`validate` runs the complete software gate without device writes; it does not
filter by Git changes and is not the default delivery check. Inspect every
status and exit code. Report checks omitted by scope as skipped. Record device
observations only through record-evidence with real revision, source digest,
toolchain, operator and retained sanitized log. A simulator or build never
establishes physical behavior.

All commands above use `python3 codecraft.py <command>` from the repository root. Read the root AGENTS.md before acting.

The documentation gate requires paired README files and rejects Chinese copies of other engineering documents. Check both repository and exported-project navigation. Preserve original third-party evidence and fail on stale Skill inventories instead of silently accepting new fingerprints.

For changed standalone packages, follow docs/development/skill-releases.md. Check the exact candidate, review its digest and versions, then explicitly apply it; application reruns isolated client conformance. A passing candidate does not publish, authorize a live API task or replace the repository checks selected by the root validation policy.

For focused host checks, use `python3 codecraft.py test --path <owner>`; use `test --list` to inspect collected IDs. Read docs/development/testing.md before moving tests. Preserve full scenario coverage; do not keep old forwarding wrappers that execute the same suite twice. Skips and fixture/collection failures must remain visible.

For ESP-IDF source selection and BSP dependencies, follow [native composition rules](../../docs/development/application-workflow.md#native-composition).

For assembly selection, export and revision-specific evidence, follow [hardware variant rules](../../docs/development/application-workflow.md#hardware-variants-and-evidence).
