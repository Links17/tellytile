---
name: codecraft-development-discover
description: Discover registered devices, Targets and native interfaces with explicit unknowns.
---

# Discover

Use `discover --interface <id>` (repeatable) and optional `--platform <id>`. Show matching candidates, limitations and unknowns. No matches means no registered implementation, not proof about all hardware. Load `context --target <id>` before development; do not guess missing pins or board revisions.

All commands above use `python3 codecraft.py <command>` from the repository root. Read the root AGENTS.md before acting.

For cloud inference, select an explicit standalone Skill by its documented operation. In the reference repository, docs/skill-inventory.json is generated from package contracts and release records. Read the selected SKILL.md and contract.json. Do not assume missing network, credential references, privacy decisions or resource bounds are satisfied. Peripheral interfaces remain hardware metadata.
