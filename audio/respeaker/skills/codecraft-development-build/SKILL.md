---
name: codecraft-development-build
description: Build native firmware and run checks without flashing or publishing.
---

# Build

First apply the [root validation policy](../../AGENTS.md).
Use this workflow for requested builds or changes affecting firmware/native build
inputs or behavior. Otherwise use static/host checks without native builds or
toolchain setup. Run `validate --static`, then `validate --target <id>` for
affected Targets; narrow by `--example` / `--variant` where applicable. Use
`--projects` when native export contents or build behavior changes. No-option
`validate` runs the complete software gate; reserve it for an explicit full-build
request or native changes affecting every Target. A missing required prerequisite
is not_run, never passed. No build command flashes or publishes. Diagnose failures
before retrying.

All commands above use `python3 codecraft.py <command>` from the repository root. Read the root AGENTS.md before acting.

In a generated application, use `python3 codecraft.py build-project .`; explicitly accept reviewed application/toolchain changes with `python3 codecraft.py lock-project .`. Do not use reference-Target validation to bypass an application's lock.

For requested factory image delivery, keep the native toolchain activated and
use `firmware plan --project <application> --output <new-plan.json>`,
`firmware merge --plan <plan.json> --output <new-release-directory>`, then
`firmware verify <release-directory>`. Reference builds select --target,
--example and, when applicable, --variant instead of --project. The plan requires
a current successful build receipt. Never relock or rebuild implicitly after
drift. Current support is unsigned, unencrypted ESP-IDF factory BIN; other
platforms/formats return not_supported. Output includes firmware.bin and its
manifest; source package behavior is unchanged. These commands do not flash or
publish. See docs/development/firmware-images.md in the reference repository.

For ESP-IDF source selection and BSP dependencies, follow [native composition rules](../../docs/development/application-workflow.md#native-composition).

For assembly selection, export and revision-specific evidence, follow [hardware variant rules](../../docs/development/application-workflow.md#hardware-variants-and-evidence).
