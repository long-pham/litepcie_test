"""LitePCIe Test Framework for FPGA Development."""

__version__ = "0.1.0"
__author__ = "Long Pham"
__email__ = "lpham@lucidfw.com"

from loguru import logger

# Configure default logger
logger.disable("litepcie_test")  # Disable by default, let CLI enable it


def main() -> None:
    """Main entry point for the CLI."""
    print("Hello from litepcie-test!")
