<p align="right"><a href="README.md">English</a> · <strong>简体中文</strong></p>

# sensecap-indicator: button_backlight

激活声明的工具链，运行 `python3 codecraft.py lock-project .` 记录实际环境，再运行 `python3 codecraft.py build-project .`。`python3 codecraft.py verify-project .` 检查源码完整性。构建不会烧录。修改应用后，审查变更并重新运行 `python3 codecraft.py lock-project .`。`codecraft.lock` 分别保留最初导入来源和当前应用快照。

project.yaml 使用兼容 JSON 的 YAML 1.2。原生应用位于 `devices/sensecap-indicator/targets`，配置直接在原生文件中修改，未知硬件事实保持 unknown。构建成功不代表硬件验证。

所选构建变体：**gx**。原生默认值、项目配置与锁文件一致。该选择不识别实际面板或 PCB 修订；更换组件必须同时引入有来源的原生输入与元数据，审查后明确重新锁定。
