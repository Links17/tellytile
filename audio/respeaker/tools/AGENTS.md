# Repository tooling

This directory owns executable repository maintenance and portable application
orchestration. The public entry remains `python3 codecraft.py`; read the root
contract before changing tools. Keep installable Skills under skills/ and native
build implementations under platforms/<stack>/.

## CodeCraft tools

codecraft/ owns the public CLI implementation, Device document parsing, source
graphs, project exports and locks, evidence, generated indexes and host-test
discovery. Its resource.json declares the portable tool and workflow closure.
Context, export and evidence must use the same graph, including these tools.
Native-only projects must not load cloud contracts or package maintenance.

CI impact planning and aggregation live in codecraft/ci.py and ci_policy.py.
They reuse the declared native roots and source graph, read both committed trees,
and run through `python3 codecraft.py ci plan` / `ci check`. Keep them outside the
portable resource.json closure; exported applications report CI as not_supported.
The full Catalog graph continues to own context/export/evidence identity. Read
docs/development/ci.md in the reference repository before changing selection or
job results; that repository maintenance guide is not a portable dependency.

## Skill package maintenance

reCamera SDK reference production and development-package candidate review live
under skill_packages/recamera/. Its release.json owns the installed file
inventory; original import evidence stays in docs/recamera-skill-import.json.
Use the public skill-reference command and explicit skill-release --skill
selection documented in docs/development/recamera-sdk-maintenance.md. Keep these
maintenance tools outside the installed Skill and native application exports.

skill_packages/ owns the common client helper, reviewed package inventories,
original import provenance, upstream snapshots and candidate release tooling.
Maintain runtime helpers here and distribute package-local copies through the
reviewed release workflow. An independently installed Skill must run without
this directory, sibling Skills or the hardware Catalog.

Keep original import and upstream snapshot bytes unchanged when moving tools or
renaming a maintained package. Source paths in those records refer to the
upstream checkout. Release records use the current package identity and point
to the relocated provenance; they must retain original source facts. File
hashes and distribution versions change only through reviewed candidates.

Tool tests, fixtures and mocks belong under tests/tools/codecraft/ and
tests/tools/skill_packages/. Per-Skill conformance remains under tests/skills/.
Use the public host runner and the root validation policy. Package maintenance
checks run isolated clients with synthetic credentials and loopback services;
they do not authorize API requests, installation or publication.
