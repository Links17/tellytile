---
name: codecraft-development-ideate
description: Clarify a hardware application brief and select sourced Device and Target candidates before generating a private project.
---

# Ideate

For a new application, read the root contract and [project workspace guide](../../docs/development/application-workflow.md). Establish the purpose, target users, concrete scenarios, BOM, intended output path and software stack. Clarify budget, power, connectivity and privacy constraints when they affect selection. Record missing information as unknown; do not invent quantities, physical revisions, compatible accessories or source claims.

Run `python3 codecraft.py discover` for actual candidates, then `python3 codecraft.py context --target <platform>/<device> --task ideate` for each selected candidate. Read the returned device facts, references and native bindings before relying on them. A brief records user intent; build or physical support comes only from current source-bound evidence. Use an actual registered standalone Skill contract for cloud requirements; no credential or real API operation is implied by ideation.

Write the confirmed brief as JSON using only name, purpose, target_users, scenarios and bom. Each BOM entry has item, quantity and source; unknown quantity/source use null. Put the brief in ignored local storage such as `projects/<name>.brief.json`. Generate a new workspace with `python3 codecraft.py new --target <platform>/<device> --example <example> --output projects/<name> --brief projects/<name>.brief.json`. Explicit external output directories also work. Do not overwrite an existing project or edit a generated AGENTS region by hand.

Hand the selected Target, unresolved constraints and generated project to codecraft-development-setup and codecraft-development-develop. Use codecraft-development-build and codecraft-development-validate for actual checks, codecraft-development-debug for failed operations, and codecraft-development-device-test only within explicit device-write authorization. Ideation itself does not install a toolchain, flash a device, call a cloud API or publish anything.
