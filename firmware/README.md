<p align="right"><strong>English</strong> · <a href="README.zh_CN.md">简体中文</a></p>

# sensecap-indicator: button_backlight

Activate the declared toolchain, run `python3 codecraft.py lock-project .` to record the actual environment, then `python3 codecraft.py build-project .`. `python3 codecraft.py verify-project .` checks source integrity. Builds never flash. After deliberate application edits, review the changes and run `python3 codecraft.py lock-project .` again. `codecraft.lock` retains the original import provenance and a separate current application snapshot.

project.yaml uses JSON-compatible YAML 1.2. The native application lives under `devices/sensecap-indicator/targets`. Edit native configuration there; unknown hardware facts remain unknown. A successful build does not verify hardware.

Selected build variant: **gx**. The native default, project profile and lock agree. This choice does not identify the physical panel or PCB revision. Changing assemblies requires the corresponding sourced native inputs and metadata, review and explicit relocking.
