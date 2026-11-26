---
name: codecraft-development-device-test
description: Perform explicitly authorized device writes and record actual physical observations.
---

# Device Test

Require explicit session authorization before any device write, and verify board/port or UF2 drive identity. Existing authorization remains valid within scope. Do not require flash readback and never erase the full chip without an explicit request. Use the native deployment workflow, then observe the claimed physical behavior and recovery. ESP32 flashing does not update separate co-processors. File copy alone does not prove boot success. Record only actual observations with record-evidence. Store sanitized logs and evidence under tests/devices/<device>/; read tests/AGENTS.md. Acceptance procedures remain in the Device AGENTS contract.

All commands above use `python3 codecraft.py <command>` from the repository root. Read the root AGENTS.md before acting.
