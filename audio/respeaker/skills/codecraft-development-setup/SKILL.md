---
name: codecraft-development-setup
description: Install and activate the exact toolchain declared by a hardware Target.
---

# Setup

Read target.json for the exact toolchain and its source. For ESP-IDF, activate the declared installation and verify idf.py --version. Linux source validation needs Python 3 and dtc. Arduino needs the declared FQBN and exact core/library versions. Preserve other toolchains; install missing prerequisites within authorization and report unresolved dependencies.

All commands above use `python3 codecraft.py <command>` from the repository root. Read the root AGENTS.md before acting.

For an explicit SenseCraft CLI setup request or a task needing SenseCraft platform operations, follow [external tool setup](../../docs/development/application-workflow.md). Use the official published npm package and the matching bundled Skill. Verify the actual executable version and offline help; keep authentication and API implementation upstream. Native toolchain setup does not require SenseCraft login or a cloud call.
