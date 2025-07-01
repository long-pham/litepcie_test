import time
from loguru import logger

from litex.gen import LiteXModule, Signal, ClockDomain

# from litex_boards.platforms import alinx_axau15

from litex.soc.cores.clock import USMMCM, USIDELAYCTRL
from litex.soc.integration.soc_core import SoCCore, SoCMini

from litex.soc.cores.led import LedChaser

from litedram.modules import MT40A512M16
from litedram.phy import usddrphy

from liteeth.phy.usrgmii import LiteEthPHYRGMII

# from litepcie.phy.usppciephy import USPPCIEPHY
from litepcie_test.custom_usppciephy import CustomUSPPCIEPHY
from litepcie.software import generate_litepcie_software
import os
from litex.soc.integration.builder import Builder
from litepcie_test.utils import measure_time


# CRG ----------------------------------------------------------------------------------------------
#
class _CRG(LiteXModule):
    def __init__(self, platform, sys_clk_freq):
        self.rst = Signal()
        self.cd_sys = ClockDomain()
        self.cd_sys4x = ClockDomain()
        self.cd_idelay = ClockDomain()

        # # #

        # Clk.
        clk200 = platform.request("clk200")

        # PLL.
        self.pll = pll = USMMCM(speedgrade=-2)
        self.comb += pll.reset.eq(self.rst)
        pll.register_clkin(clk200, 200e6)
        pll.create_clkout(self.cd_sys, sys_clk_freq, with_reset=False)
        pll.create_clkout(self.cd_sys4x, 4 * sys_clk_freq)
        platform.add_false_path_constraints(self.cd_sys.clk,
                                            pll.clkin)  # Ignore sys_clk to pll.clkin path created by SoC's rst.

        # IDelayCtrl.
        self.idelayctrl = USIDELAYCTRL(cd_ref=self.cd_sys4x, cd_sys=self.cd_sys)


# BaseSoC ------------------------------------------------------------------------------------------

class BaseSoC(SoCMini):
    def __init__(self, sys_clk_freq=int(125e6),
                 with_ethernet=False,
                 with_etherbone=False,
                 eth_ip="192.168.1.50",
                 remote_ip=None,
                 with_led_chaser=True,
                 with_pcie=False,
                 pcie_speed="gen4",
                 #  pcie_speed="gen3",
                 with_sdcard=False,
                 **kwargs):
        from litex_platform_boards import axau15
        # from litex_boards.platforms import alinx_axau15 as axau15
        platform = axau15.Platform()

        # CRG --------------------------------------------------------------------------------------
        self.crg = _CRG(platform, sys_clk_freq)

        # SoCCore ----------------------------------------------------------------------------------
        SoCMini.__init__(self, platform, sys_clk_freq, ident="LiteX SoC on Alinx AXAU15", **kwargs)

        # DDR4 SDRAM -------------------------------------------------------------------------------
        if not self.integrated_main_ram_size:
            self.ddrphy = usddrphy.USPDDRPHY(platform.request("ddram"),
                                             memtype="DDR4",
                                             sys_clk_freq=sys_clk_freq,
                                             iodelay_clk_freq=500e6
                                             )
            self.add_sdram("sdram",
                           phy=self.ddrphy,
                           module=MT40A512M16(sys_clk_freq, "1:4"),
                           size=0x40000000,
                           l2_cache_size=kwargs.get("l2_size", 8192)
                           )

        # PCIe -------------------------------------------------------------------------------------
        if with_pcie:
            self.pcie_phy = CustomUSPPCIEPHY(
                platform,
                platform.request("pcie_x4"),
                # platform.request("pcie_x1"),
                speed=pcie_speed,
                data_width={"gen3": 128, "gen4": 256}[pcie_speed],
                #
                # data_width=128,
                # axisten_freq=125,

                ip_name="pcie4c_uscale_plus",
                bar0_size=0x400000,  # 8MB - Control registers (BAR0)
                # bar2_size=0x100000,    # 1MB - DMA buffers (64-bit BAR, uses BAR2+3)
                # bar4_size=0x200000,    # 2MB - Memory mapped region (64-bit BAR, uses BAR4+5)
            )
            self.add_pcie(phy=self.pcie_phy, ndmas=1)

            # Set manual locations to avoid Vivado to remap lanes to X0Y4, X0Y5, X0Y6, X0Y7.
            platform.toolchain.pre_placement_commands.append(
                "reset_property LOC [get_cells -hierarchical -filter {{NAME=~*pcie_usp_i/*GTHE4_CHANNEL_PRIM_INST}}]")
            platform.toolchain.pre_placement_commands.append(
                "set_property LOC GTHE4_CHANNEL_X0Y0 [get_cells -hierarchical -filter {{NAME=~*pcie_usp_i/*gthe4_channel_gen.gen_gthe4_channel_inst[0].GTHE4_CHANNEL_PRIM_INST}}]")
            platform.toolchain.pre_placement_commands.append(
                "set_property LOC GTHE4_CHANNEL_X0Y1 [get_cells -hierarchical -filter {{NAME=~*pcie_usp_i/*gthe4_channel_gen.gen_gthe4_channel_inst[1].GTHE4_CHANNEL_PRIM_INST}}]")
            platform.toolchain.pre_placement_commands.append(
                "set_property LOC GTHE4_CHANNEL_X0Y2 [get_cells -hierarchical -filter {{NAME=~*pcie_usp_i/*gthe4_channel_gen.gen_gthe4_channel_inst[2].GTHE4_CHANNEL_PRIM_INST}}]")
            platform.toolchain.pre_placement_commands.append(
                "set_property LOC GTHE4_CHANNEL_X0Y3 [get_cells -hierarchical -filter {{NAME=~*pcie_usp_i/*gthe4_channel_gen.gen_gthe4_channel_inst[3].GTHE4_CHANNEL_PRIM_INST}}]")

            # ICAP (For FPGA reload over PCIe).
            # from litex.soc.cores.icap import ICAP
            # self.icap = ICAP()
            # self.icap.add_reload()
            # self.icap.add_timing_constraints(platform, sys_clk_freq, self.crg.cd_sys.clk)

            # from litex.soc.cores.spi_flash import USSPIFlash
            # from litex.soc.cores.gpio import GPIOOut
            # self.flash_cs_n = GPIOOut(platform.request("flash_cs_n"))
            # self.flash = USSPIFlash(platform.request("flash"), sys_clk_freq, 25e6)

        # SD Card ----------------------------------------------------------------------------------
        if with_sdcard:
            self.add_sdcard()

        # Leds -------------------------------------------------------------------------------------
        # if with_led_chaser:
        #     self.leds = LedChaser(
        #         pads=platform.request_all("user_led"),
        #         sys_clk_freq=sys_clk_freq)
        self.add_led_chaser()

    def add_led_chaser(self, pads=None, duty=0.01, period=10240):
        if pads is None:
            pads = self.platform.request_all("user_led")
        self.leds = LedChaser(pads=pads, sys_clk_freq=self.clk_freq,
                              polarity=0)
        duty = int(duty * period)
        self.leds.add_pwm(default_width=duty, default_period=period)


@measure_time
def build():
    # argdict = {'bus_standard': 'wishbone', 'bus_data_width': 32, 'bus_address_width': 32,
    #            'bus_timeout': 1000000, 'bus_bursting': False, 'bus_interconnect': 'shared',
    #            'cpu_type': 'vexriscv', 'integrated_rom_size': 131072, 'integrated_sram_size': 8192,
    #            'csr_data_width': 32, 'csr_address_width': 14, 'csr_paging': 2048,
    #            'csr_ordering': 'big', 'ident_version': True, 'with_uart': True,
    #            'uart_name': 'serial', 'uart_baudrate': 115200, 'uart_fifo_depth': 16,
    #            'with_timer': True, 'timer_uptime': False, 'with_ctrl': True, 'with_jtagbone': False,
    #            'jtagbone_chain': 1, 'with_uartbone': False, 'with_watchdog': False,
    #            'watchdog_width': 32, 'l2_size': 8192}
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
        compile_software=True,
        compile_gateware=True,
        generate_doc=True,

    )

    builder.build(run=True)
    generate_litepcie_software(soc, os.path.join(builder.output_dir, "driver"))


if __name__ == '__main__':
    build()
