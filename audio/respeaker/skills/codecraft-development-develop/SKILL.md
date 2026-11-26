---
name: codecraft-development-develop
description: Develop hardware applications and repository contributions with native Target implementations.
---

# Develop

Read the root AGENTS.md and obtain context for the selected Target.
Start from the selected Device's authored AGENTS.md. Use the
[portable application workflow](../../docs/development/application-workflow.md)
for project work. Repository contributions that edit Device knowledge, specifications,
sources or acceptance must additionally read docs/development/device-documents.md
under the root routing rules. Follow the matching
platforms/<stack>/AGENTS.md for native work. Use codecraft-development-ideate when the
application brief needs clarification. Generate private projects with
`new --target <id> --example <name> --output projects/<name>`; explicit external
directories remain supported. Use --brief <file.json> for a confirmed brief.
Keep generated application AGENTS content sourced from project.yaml and preserve
custom rules outside its marked region. See [project workspaces](../../docs/development/application-workflow.md#project-intent-and-source-locks).
Preserve application/BSP boundaries and user changes; finish with the required
validation report.

All commands above use `python3 codecraft.py <command>` from the repository root. Read the root AGENTS.md before acting.

For SenseCraft platform management or CodeCraft Hub tasks, use the official external sensecraft-cli Skill; see [external platform routing](../../docs/development/application-workflow.md#external-platform-operations). Install/select the required external tool version when needed and follow its task-specific reference.

For CodeCraft inference functionality, read the selected standalone Skill and contract.json. Use new --skills with explicit reviewed selections; exports carry the selected package and its release identity. Run python3 codecraft.py lock-project . after reviewed application/environment updates; initial import provenance remains intact.

Maintain README.md and README.zh_CN.md together. All other engineering documents, including AGENTS.md, SKILL.md, API references, plans and reports, use one English source. Update generators and local references with documentation changes; never generate translated Agent rules or duplicate hardware facts.

For repository-maintained standalone package changes, use skill-release prepare/check/apply, following docs/development/skill-releases.md. Keep helper maintenance under tools/skill_packages and runtime imports package-local. Review candidate bytes, separate versions and the exact digest before applying; never automatically accept hash drift or rewrite original provenance. Exported applications use the same public CLI and source lock.

For ESP-IDF source selection and BSP dependencies, follow [native composition rules](../../docs/development/application-workflow.md#native-composition).

For assembly selection, export and revision-specific evidence, follow [hardware variant rules](../../docs/development/application-workflow.md#hardware-variants-and-evidence).
