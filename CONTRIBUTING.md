# Contributing

Contributions to Read Pico firmware, board support, drivers, tools, and documentation are welcome. Issues and pull requests may be written in English, Simplified Chinese, or Japanese. Keep each PR focused on one problem; discuss major behavior or hardware changes in an issue first.

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
