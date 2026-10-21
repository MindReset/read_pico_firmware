# Contributing

Contributions to kiikoread firmware, board support, drivers, tools, and documentation are welcome. Issues and pull requests may be written in English, Simplified Chinese, or Japanese. Keep each PR focused on one problem; discuss major behavior or hardware changes in an issue first.

## Contribution licenses / 贡献许可

- Read [LICENSE](LICENSE) before contributing. Existing files and third-party material retain their licenses; patches to such material do not automatically become noncommercial. Do not replace their copyright or SPDX headers with the new license.
- New original material may use [Personal Study License 1.0](licenses/kiikoread-Personal-Study-1.0.txt) only with its rights holder's explicit consent and an accurate copyright notice. Mark new files with `SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0`. For a mixed file, identify the exact new portions and applicable license without changing the license of existing portions. If this cannot be done clearly, retain the existing license and discuss the scope before submitting.
- A PR must state the licenses of its original contributions and sources of reused material. Submission does not transfer copyright, authorize relicensing of another person's work, or withdraw any prior grant. Contributions without a clear licensing basis must be clarified before merging. Do not assume that adding this policy makes a contributor's prior work noncommercial.
- The new license permits free sharing for personal study and requires separate written permission for commercial use. Apache-2.0, MIT, ISC, LGPL and OFL rights continue to apply to their respective material, including LGPL modification, debugging and relinking rights.

- 贡献前阅读 [LICENSE](LICENSE)。既有文件和第三方材料保留其原许可，修补这些内容不会自动变成“禁止商用”；不得替换原版权或 SPDX 声明。
- 新增原创内容采用[个人学习许可 1.0](licenses/kiikoread-Personal-Study-1.0.txt)，必须取得相应权利人明确同意，保留真实版权声明，并在新文件标注 `SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0`。混合文件必须准确列出新增部分及其许可，不能改变既有部分的许可；范围无法清楚划分时，保留原许可并先讨论。
- PR 应说明原创贡献的许可及复用材料来源。提交不转让版权，不代表可以替他人重新授权，也不撤回既有授权；许可不明时先澄清再合入。不得将以往贡献自动视为“禁止商用”。
- 新许可允许个人学习用途的免费分享，商业使用须另行书面授权；Apache-2.0、MIT、ISC、LGPL、OFL 仍分别适用，包含 LGPL 的修改、调试和重新链接权利。

## Development

Start with [README.md](README.md) for hardware and ESP-IDF v6.1 build instructions, and [AGENTS.md](AGENTS.md) for component boundaries, page callbacks, bilingual comments, and frozen decisions. Reuse existing components and drawing helpers. Add demo pages through the registry rather than changing the shared event loop.

Preserve the board-specific timing in `sdkconfig.defaults`; `sdkconfig.ci` checks compilation only. VCOM remains factory-calibrated in the PMU, and the SY7636A power page stays read-only. Explain any proposed change to a frozen decision and update its block before changing the behavior.

## Pull requests

- Describe the problem, resulting behavior, affected components, and related issue.
- State checks actually performed and anything unverified. Distinguish compilation from physical-board testing; include hardware and configuration for device observations. Documentation-only changes do not require a build or device test.
- Keep README language editions aligned where practical, or identify remaining translations.
- Preserve SPDX headers and third-party license notices. Submit only material you have the right to share under the applicable license; new vendor files, fonts, waveforms, and assets need a clear source and redistribution permission.
- Remove credentials, private device identifiers, personal content, and supplier-confidential material from files, logs, and screenshots.

See [Security](SECURITY.md) for private reports, [Support](SUPPORT.md) for help, and the [Code of Conduct](CODE_OF_CONDUCT.md) for community participation.
