# Gateway Firmware

STM32 firmware for the UART-to-nRF24 gateway node in the temperature logger system.

This firmware runs on an STM32 Blue Pill class board and acts as a bridge between:

- the host-side UART CLI tool
- the 2.4 GHz nRF24L01+ radio link
- the remote temperature measurement node

## Role in the system

The gateway node is responsible for:

- receiving request/response commands from the host over UART
- translating supported UART commands into RF transactions
- sending requests to the remote node over nRF24
- receiving replies from the remote node
- returning formatted responses back to the host over UART

Example supported command flow:

- Host sends `TEMP?`
- Gateway sends RF request `GTMP`
- Remote node replies with `TMP!` payload
- Gateway formats and returns a single UART response line

## Repository context

This repository contains the **actual STM32CubeIDE firmware project** for the gateway node.

The broader system documentation is maintained separately in:

- **stm32-nrf24-temperature-logger**: [System-level documentation](https://github.com/honkajan/stm32-nrf24-temperature-logger)

Related repositories:

- **uartctl** (host-side Python CLI tool): [https://github.com/honkajan/uartctl](https://github.com/honkajan/uartctl)
- **remote-fw** (remote node firmware): [https://github.com/honkajan/remote-fw](https://github.com/honkajan/remote-fw)

## Project layout

This repository contains the original STM32CubeIDE project.

The CubeIDE project currently retains its historical working name:

- `bluepill_uart_pingpong`

That name reflects the project's earlier bring-up phase and ping/pong testing history.

## Development environment

- STM32CubeIDE project
- STM32F103C8T6 target
- STM32 HAL / CMSIS drivers included in the repository
- nRF24L01+ radio module
- UART-based host interface

## Opening the project

Open the repository as an STM32CubeIDE project.

Key project files include:

- `.project`
- `.cproject`
- `.mxproject`
- `bluepill_uart_pingpong.ioc`

The `.ioc` file can be opened with STM32CubeMX integration inside STM32CubeIDE if regeneration or inspection of peripheral configuration is needed.

## Notes on build artifacts

Build output directories and local IDE/debug launch files are intentionally not tracked in Git.

Examples of excluded local/generated content:

- `Debug/`
- `.settings/`
- `*.launch`

## Current focus

The firmware has evolved from early UART ping/pong experiments into a more structured wireless gateway with support for:

- RF request/response handling
- gateway-to-remote ping transactions
- temperature fetch transactions
- radio diagnostics and transaction status reporting

## Status

This is an actively developed embedded firmware project and may continue to evolve as the overall system is refined.

For system-level architecture, protocol documentation, and diagrams, see the main system repository.

## License

See the repository license file.
