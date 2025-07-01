#!/usr/bin/env python3

#
# This file is part of LiteX-Boards.
#
# Copyright (c) 2022 Andrew Elbert Wilson <Andrew.E.Wilson@ieee.org>
# SPDX-License-Identifier: BSD-2-Clause

# import os
#
# from migen import *
# from migen.genlib.resetsync import AsyncResetSynchronizer
#
# from litex.gen import *

# from litex_boards.platforms import opalkelly_xem8320

# from litex.soc.cores.clock import *
from litex.soc.integration.soc_core import *
from litex.soc.integration.builder import *
from litex.soc.cores.led import LedChaser
# from litex.soc.cores.video import VideoDVIPHY
#
# from litedram.modules import MT40A512M16
# from litedram.phy import usddrphy

from litepcie.software import generate_litepcie_software

from litepcie_test.utils import measure_time
from litex_platform_boards import xem8320
from litepcie_test.custom_usppciephy import CustomUSPPCIEPHY


# BaseSoC ------------------------------------------------------------------------------------------

class BaseSoC(SoCMini):
    def __init__(self, sys_clk_freq=int(125e6),
                 with_video_framebuffer=False,
                 **kwargs):
        self.platform = platform = xem8320.Platform()
        self.crg = platform.get_default_crg(sys_clk_freq=sys_clk_freq)

        # SoCCore ----------------------------------------------------------------------------------
        # kwargs["uart_name"] = "jtag_uart"
        SoCMini.__init__(self, platform, sys_clk_freq, ident="LiteX SoC on XEM8320", **kwargs)

        # self._add_pcie_x4(pcie_speed="gen3")
        self._add_pcie_x2(pcie_speed="gen3")
        self.add_led_chaser()

    def add_led_chaser(self, pads=None, duty=0.01, period=10240):
        if pads is None:
            pads = self.platform.request_all("user_led")
        self.leds = LedChaser(pads=pads, sys_clk_freq=self.clk_freq,
                              polarity=0)
        duty = int(duty * period)
        self.leds.add_pwm(default_width=duty, default_period=period)

    def _add_pcie_x2(self, pcie_speed="gen3", ):
        platform = self.platform
        self.pcie_phy = CustomUSPPCIEPHY(
            platform,
            platform.request("pcie_x2"),
            speed=pcie_speed,
            data_width={"gen3": 128, "gen4": 256}[pcie_speed],
            axisten_freq=125,
            ip_name="pcie4_uscale_plus",
            bar0_size=0x800000,  # 8MB - Control registers (BAR0)
            bar2_size=0x800000,  # 1MB - DMA buffers (64-bit BAR, uses BAR2+3)
            # bar4_size=0x200000,    # 2MB - Memory mapped region (64-bit BAR, uses BAR4+5)
        )
        self.add_pcie(phy=self.pcie_phy, ndmas=1, with_dma_monitor=True,
                      with_dma_synchronizer=True,
                      with_dma_status=True, address_width=32, )

    def _add_pcie_x4(self, pcie_speed="gen4", ):
        platform = self.platform
        self.pcie_phy = CustomUSPPCIEPHY(
            platform,
            platform.request("pcie_x4"),
            # platform.request("pcie_x1"),
            speed=pcie_speed,
            data_width={"gen3": 128, "gen4": 256}[pcie_speed],

            ip_name="pcie4_uscale_plus",
            bar0_size=0x800000,  # 8MB - Control registers (BAR0)
            bar2_size=0x800000,  # 1MB - DMA buffers (64-bit BAR, uses BAR2+3)
            # bar4_size=0x200000,    # 2MB - Memory mapped region (64-bit BAR, uses BAR4+5)
        )
        self.add_pcie(phy=self.pcie_phy, ndmas=1, with_dma_monitor=True,
                      with_dma_synchronizer=True,
                      with_dma_status=True, address_width=32, )


# Build --------------------------------------------------------------------------------------------
@measure_time
def build():
    argdict = {
        'integrated_rom_size': 131072,
        'with_ctrl': True,
        # 'with_sdcard': True,
        'with_pcie': True,
        'sys_clk_freq': 100e6,
        'integrated_main_ram_size': 8 * 1024,
    }

    soc = BaseSoC(**argdict)

    builder = Builder(
        soc=soc,
        # compile_software=True,
        compile_gateware=True,
        generate_doc=True,
    )

    builder.build(run=True)
    generate_litepcie_software(soc, os.path.join(builder.output_dir, "driver"))

if __name__ == "__main__":
    build()
