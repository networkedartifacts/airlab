# Air Lab

**Portable and playful air quality monitor.**

![Air Lab device](https://cdn.networkedartifacts.com/al1-gh__header__medium.webp)

_Using its high-quality sensors, Air Lab measures CO2, temperature, relative humidity, air pollutants (VOC, NOx), and atmospheric pressure. The device can record measurements for multiple days, which you can analyze directly on-device — no computer or smartphone required._ [Read more about the product on our website.](https://networkedartifacts.com/airlab)

**Welcome to the Air Lab open-source repository!** At Networked Artifacts, we firmly believe that open-source software is essential to delivering a product that remains accessible and customizable forever. We encourage you to join the development, report bugs, or fork the repository and customize it to your needs. And when you do, make sure to share your project in [discussions](https://github.com/networkedartifacts/airlab/discussions)!

This repository is meant to be cloned or forked by anyone wanting to collaborate on the firmware, plugins, or tools. All components live in a single tree, so they can be built and tested together.

## Structure

- [`firmware`](firmware) — The official device firmware and board support library.
- [`plugins`](plugins) — WASM-based plugins that run on-device, compiled with Zig.
- [`tools`](tools) — The `airlab` CLI and supporting Go packages.

## Prerequisites

- **Firmware:** [NAOS](https://github.com/256dpi/naos) framework (manages ESP-IDF and the toolchain).
- **Plugins:** [Zig](https://ziglang.org) compiler.
- **Tools:** [Go](https://go.dev) 1.23+.

## Quick Start

- **Build the firmware:** See [`firmware/README.md`](firmware/README.md) for setup, build, and flash instructions.
- **Write a plugin:** See [`plugins/README.md`](plugins/README.md) for the plugin API, examples, and build workflow.
- **Install the CLI:** See [`tools/README.md`](tools/README.md) for installation and command reference.

## Contributing

- **Discussions** — Have a question, idea, or want to share a project? Start a thread in [GitHub Discussions](https://github.com/networkedartifacts/airlab/discussions). This is the best place to start.
- **Issues** — Bug reports and feature requests for the official firmware and tools are tracked in [GitHub Issues](https://github.com/networkedartifacts/airlab/issues).
- **Pull Requests** — PRs for the official firmware are accepted from invited contributors. If you'd like to contribute, please open a discussion or issue first.

## Resources

- Website: https://networkedartifacts.com/airlab
- Manual: https://networkedartifacts.com/airlab/manual
- Studio: https://studio.networkedartifacts.com

## Licenses

The source code and materials in this repository are open source and licensed under the Apache License 2.0. This license allows you to use, modify, and redistribute the code freely, including for commercial purposes, provided you comply with the terms of the license. See [LICENSE.md](LICENSE.md) for the full license text.

Please note that certain graphics, names, and logos included in this repository are Networked Artifacts Inc. trademarks or copyrighted materials. These assets are not covered by the Apache 2.0 license and may not be used in derivative products without prior written permission. See [ASSETS-EULA.md](ASSETS-EULA.md) for terms governing the use of included graphics and branding assets.

In case of uncertainty about the license, please contact us directly at https://networkedartifacts.com/contact.
