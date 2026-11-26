# Portable application workflow

This guide applies to native projects and their exported workflow Skills. The
root AGENTS.md defines the current task scope. Repository contribution and release
maintenance additionally follow the reference repository's root routing table;
they are not application build prerequisites.

## Selection and context

Run `python3 codecraft.py discover`, then `context --target <stack>/<device>`.
Use `--task ideate`, develop, setup, build, debug, device-test, discover or validate
to choose guidance. Every returned source is in the same dependency graph used
by exports, locks and evidence. Read the selected Device AGENTS, Target manifest,
Platform AGENTS and cited sources. Pins, revisions, resource bounds and accessory
compatibility remain unknown without the named source or observation.

## Project intent and source locks

Use `new --target <id> --example <name> --output <new-directory>` to create a
workspace; within the reference checkout use projects/<name>. `--brief` accepts
JSON with name, purpose, target_users, scenarios and bom. BOM entries contain
item, quantity and source; unknown quantity/source use null. project.yaml owns
the brief; relocking refreshes the generated AGENTS region and preserves custom
rules outside it. Keep application work outside the reference implementations.

Project profiles and locks use schema 3. Select optional cloud clients with
`--skills <selection.json>`, containing explicit Skill IDs, configuration,
environment decisions/reference names and resource limits. No credential values
belong in metadata. Read each selected package's SKILL.md and contract.json;
selection never authorizes a service call. When executing from an exported project,
apply its reviewed Skill configuration through the documented client flags; stored
environment decisions do not establish current authorization or availability.
Native-only projects carry no cloud
registry or client. Original import provenance remains immutable.

## Build and validation scope

Activate only the exact toolchain declared by the Target. An ESP32 Arduino
project uses its Arduino core/FQBN/library pins, not ESP-IDF commands. Use
`verify-project .` to inspect source integrity. After reviewed application or
environment changes, use `lock-project .`, then `build-project .` for an
authorized build. Never refresh locks or rebuild implicitly to hide drift.

Unchanged firmware and native build inputs/behavior need static/host checks,
not firmware compilation or toolchain installation. Repository contributors use
`validate --static`; affected native changes use scoped Target/example/variant
validation. Reference validation is unavailable in exported applications;
`build-project` enforces the application lock. The no-option repository
`validate` command runs all native builds and is reserved for explicit full-build
requests or changes affecting all Targets.

## Native composition

Keep board-specific BSP logic, examples and configuration directly under
devices/<device>/targets/. Keep shared stack helpers and components under
platforms/<stack>/. Application pages, state machines and tasks belong in the
application; do not copy shared components or introduce a cross-stack ABI.

For ESP-IDF, public BSP headers belong in include/ and private code in src/.
Each example's bsp.cmake, managed dependencies and Target source/dependency
scope must agree. Preserve native component registration and required system
components. Regenerate dependency locks through the declared native tools only
when those inputs actually change. Hold LVGL locks, keep callbacks non-blocking,
and stop every task/timer/callback before freeing its screen or hardware state.

## Hardware variants and evidence

Select only sourced Target variants. Preserve the selected variant through the
profile, native configuration, lock and build cases. A default is not panel
detection or proof of PCB revision. Changing assemblies requires the corresponding
sourced native inputs, explicit review and relocking. One variant's evidence
cannot certify another. Linux overlay compilation does not establish OS/kernel
or physical behavior; Arduino board-manager profiles do not prove revision
compatibility.

## Firmware images and device operations

After an authorized successful build, use `firmware plan --project <path>
--output <new-plan.json>`, `firmware merge --plan <plan.json> --output <new-dir>`,
then `firmware verify <dir>`. Keep the matching native toolchain active. Planning
requires current successful build receipts and native layout metadata; offsets
must not be guessed. Current merging supports unsigned, unencrypted ESP-IDF
factory BIN. Other formats/platforms report not_supported. Source `package`
creates a source archive, not a firmware release.

No build, merge, package or verification command flashes or publishes. Device
writes require explicit authorization and an identified board/port. Full-chip
erase requires a specific request. Diagnose failures before retrying. Report
metadata, host tests, native compilation, file transfer and physical observations
separately; preserve original logs, revision, timestamps and source digests.

## External platform operations

SenseCraft authentication, managed devices, telemetry, Hub applications, uploads
and cloud deployment use the official external @seeed-studio/sensecraft-cli and
its bundled Skill. Install it only for an authorized task needing it; verify the
selected executable version/help. It is not required for native development.
Read the installed official Skill for setup and platform operations. CodeCraft
inference clients use their own Device-authenticated APIs; platform credentials
are not interchangeable. Installing or exporting a Skill grants no API permission.
