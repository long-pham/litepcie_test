# Repository Guidelines

## Project Structure & Module Organization
The runtime package lives in `src/litepcie_test`: board-specific SoC definitions such as `axau15_soc.py`, `sqrl_soc.py`, and `xem8320_soc.py`, shared helpers in `custom_usppciephy.py` and `utils.py`, and vendor board shims under `litex_platform_boards/`. Hardware build outputs land in `build/` (per-board bitstreams) and `tests/build/` (golden reference artifacts); keep these clean before committing. Utility automation sits in `scripts/`, including `mutagen_sync.py` for remote development and `run-remote.sh` for quick FPGA runs. Lightweight smoke tests and package version checks live in `tests/test_basic.py`.

## Build, Test, and Development Commands
- `uv pip install -e ".[development]"` — install pinned dependencies for local hacking.
- `python scripts/mutagen_sync.py start|status|stop` — manage Mutagen sync sessions to the FPGA host.
- `python src/litepcie_test/build.py` — trigger the default AXAU15 build; board-specific scripts expose similar `build()` helpers.
- `python -m litepcie_test ...` — invoke the CLI entry point without installing the console script.

## Coding Style & Naming Conventions
Write Python 3.10 with four-space indentation, `snake_case` names for functions and variables, and `CamelCase` for SoC classes. Ruff governs style; run `ruff format .` for formatting and `ruff check .` for lint fixes. Double quotes are preferred per `pyproject.toml`, and helper decorators such as `measure_time` from `utils.py` should wrap long-running flows consistently.

## Testing Guidelines
Use `pytest` from the project root; configuration under `pyproject.toml` already scopes discovery to `tests/`. Target passing `pytest --cov=litepcie_test` before submitting hardware-sensitive changes, and group new cases into descriptive `test_*.py` modules alongside any required data in `tests/build/<board>/`. Skip markers must be justified in code comments.

## Commit & Pull Request Guidelines
Commit summaries follow the concise, present-tense style in `git log` (e.g., `add pcie x2`, `xem8320 pcie port E working`). Reference affected boards or subsystems up front and keep bodies for detailed context, logs, or measured results. Pull requests should explain hardware setups, include command output for new flows, link related issues, and attach screenshots or timing tables when tweaking PCIe or DDR parameters. Always note whether remote sync or firmware regeneration is required.

## Hardware & Remote Workflow
Document any remote prerequisites in `mutagen-sync.yml` when adding boards. Validate PCIe links with `litepcie-test info` after flashing, and capture `run-remote.sh` output in PRs whenever DMA tuning or new PHY parameters are introduced. Coordinate large bitstreams by pushing only required diffs; avoid committing generated `.bit` files unless reviewers need them.
