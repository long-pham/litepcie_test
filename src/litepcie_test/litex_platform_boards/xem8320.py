from litex.build.generic_platform import *
from litex.build.xilinx import XilinxUSPPlatform, VivadoProgrammer
from litex.build.openfpgaloader import OpenFPGALoader
from migen import *
from litex.soc.cores.clock import USPMMCM, USPIDELAYCTRL
from litex.gen import LiteXModule
from migen.genlib.resetsync import AsyncResetSynchronizer

# IOs ----------------------------------------------------------------------------------------------

_io = [
    # Clk / Rst
    ("clkin100", 0,
     Subsignal("p", Pins("T24"), IOStandard("LVDS")),
     Subsignal("n", Pins("U24"), IOStandard("LVDS"))
     ),

    ("clkin100_ddr", 0,  # this clk is close to DDR4
     Subsignal("p", Pins("AD20"), IOStandard("LVDS")),
     Subsignal("n", Pins("AE20"), IOStandard("LVDS"))
     ),

    ("clkin156_25", 0,  # this clk is close to DDR4
     #  Subsignal("p", Pins("Y7"), IOStandard("LVDS")),
     #  Subsignal("n", Pins("Y6"), IOStandard("LVDS"))
     Subsignal("p", Pins("Y7")),
     Subsignal("n", Pins("Y6")),
     ),
    # NO RESET, maybe use okHOST USB later
    # ("cpu_reset", 0, Pins("AN8"), IOStandard("LVCMOS18")),

    # Leds
    ("user_led", 0, Pins("G19"), IOStandard("LVCMOS12")),
    ("user_led", 1, Pins("B16"), IOStandard("LVCMOS12")),
    ("user_led", 2, Pins("F22"), IOStandard("LVCMOS12")),
    ("user_led", 3, Pins("E22"), IOStandard("LVCMOS12")),
    ("user_led", 4, Pins("M24"), IOStandard("LVCMOS12")),
    ("user_led", 5, Pins("G22"), IOStandard("LVCMOS12")),

    # Opal Kelly Host USBC interface
    ("okHost", 0,  # Uses the FrontPanel API
     Subsignal("okAA", Pins("T19")),
     Subsignal("okHU", Pins("U20 U26 T22")),
     Subsignal("okUH", Pins("V23 T23 U22 U25 U21")),
     Subsignal("okUHU", Pins(
         "P26 P25 R26 R25 R23 R22 P21 P20",
         "R21 R20 P23 N23 T25 N24 N22 V26",
         "N19 V21 N21 W20 W26 W19 Y25 Y26",
         "Y22 V22 W21 AA23 Y23 AA24 W25 AA25")),
     IOStandard("LVCMOS18"),
     Misc("SLEW=FAST"),
     ),

    # TODO: Add SMA & SFP+

    # DDR4 SDRAM
    ("ddram", 0,
     Subsignal("a", Pins(
         "AD18 AE17 AB17 AE18 AD19 AF17 Y17 AE16",
         "AA17 AC17 AC19 AC16 AF20 AD16"),
               IOStandard("SSTL12_DCI")),
     Subsignal("ba", Pins("AC18 AF18"), IOStandard("SSTL12_DCI")),
     Subsignal("bg", Pins("AB19"), IOStandard("SSTL12_DCI")),

     Subsignal("ras_n", Pins("AA18"), IOStandard("SSTL12_DCI")),
     Subsignal("cas_n", Pins("AF19"), IOStandard("SSTL12_DCI")),
     Subsignal("we_n", Pins("AA19"), IOStandard("SSTL12_DCI")),

     Subsignal("cs_n", Pins("AF22"), IOStandard("SSTL12_DCI")),

     Subsignal("act_n", Pins("Y18"), IOStandard("SSTL12_DCI")),
     Subsignal("dm", Pins("AE25 AE22"),
               IOStandard("POD12_DCI")),
     Subsignal("dq", Pins(
         "AF24 AB25 AB26 AC24 AF25 AB24 AD24 AD25",
         "AB21 AE21 AE23 AD23 AC23 AD21 AC22 AC21"),
               IOStandard("POD12_DCI"),
               Misc("PRE_EMPHASIS=RDRV_240"),
               Misc("EQUALIZATION=EQ_LEVEL2")),
     Subsignal("dqs_p", Pins("AC26 AA22"),
               IOStandard("DIFF_POD12_DCI"),
               Misc("PRE_EMPHASIS=RDRV_240"),
               Misc("EQUALIZATION=EQ_LEVEL2")),
     Subsignal("dqs_n", Pins("AD26 AB22"),
               IOStandard("DIFF_POD12_DCI"),
               Misc("PRE_EMPHASIS=RDRV_240"),
               Misc("EQUALIZATION=EQ_LEVEL2")),
     Subsignal("clk_p", Pins("Y20"), IOStandard("DIFF_SSTL12_DCI")),
     Subsignal("clk_n", Pins("Y21"), IOStandard("DIFF_SSTL12_DCI")),
     Subsignal("cke", Pins("AA20"), IOStandard("SSTL12_DCI")),
     Subsignal("odt", Pins("AB20"), IOStandard("SSTL12_DCI")),
     Subsignal("reset_n", Pins("AE26"), IOStandard("LVCMOS12")),
     Misc("SLEW=FAST"),
     ),

    # SFP
    ("sfp", 0,
     Subsignal("txp", Pins("N5")),
     Subsignal("txn", Pins("N4")),
     Subsignal("rxp", Pins("M2")),
     Subsignal("rxn", Pins("M1"))
     ),
    # ("sfp_tx", 0,
    #  Subsignal("p", Pins("N5")),
    #  Subsignal("n", Pins("N4")),
    #  ),
    # ("sfp_rx", 0,
    #  Subsignal("p", Pins("M2")),
    #  Subsignal("n", Pins("M1")),
    #  ),
    ("sfp_tx_disable_n", 0, Pins("C13"), IOStandard("LVCMOS33")),

    ("sfp", 1,
     Subsignal("txp", Pins("L5")),
     Subsignal("txn", Pins("L4")),
     Subsignal("rxp", Pins("K2")),
     Subsignal("rxn", Pins("K1"))
     ),

    ("timestampCaptureInputs", 0,
     Pins("L18 M25 K18 M26 M20 L24 M21 L25"),
     IOStandard("LVCMOS12"),
     Misc("SLEW=SLOW"),
     Misc("PULLTYPE PULLDOWN"),
     ),

    ("pcie_x4", 0,
     Subsignal("rst_n", Pins("J10"), IOStandard("LVCMOS18")),
     Subsignal("clk_p", Pins("AB7")),
     Subsignal("clk_n", Pins("AB6")),
     Subsignal("rx_p", Pins("AF2 AE4 AD2 AB2")),
     Subsignal("rx_n", Pins("AF1 AE3 AD1 AB1")),
     Subsignal("tx_p", Pins("AF7 AE9 AD7 AC5")),
     Subsignal("tx_n", Pins("AF6 AE8 AD6 AC4"))
     ),
    ("pcie_x2", 0,
     Subsignal("rst_n", Pins("J10"), IOStandard("LVCMOS18")),
     Subsignal("clk_p", Pins("AB7")),
     Subsignal("clk_n", Pins("AB6")),
     Subsignal("rx_p", Pins("AF2 AE4")),
     Subsignal("rx_n", Pins("AF1 AE3")),
     Subsignal("tx_p", Pins("AF7 AE9")),
     Subsignal("tx_n", Pins("AF6 AE8"))
     ),

]

# Connectors ---------------------------------------------------------------------------------------

# TODO: SYZYGY Connectors & SYZYGY to PMODS!
_connectors = [
    ("portA", "L18 M25 K18 M26 M20 L24 M21 L25 J19 K25 J20 K26 L22 K22 L23 K23 H24 L19 J21 M19 H23 "
              "L20 K21 K20 F24 J26 F25 J25 J23 H26 J24 G26"),

    ("portB", "A22 A24 A23 A25 E21 D24 D21 D25 E25 C23 E26 B24 F23 C21 E23 B21 D26 C26 B26 B25 D23 "
              "C24 B20 C22 B22 A20 D20 G21 G24 H21 G25 H22"),
    ("portC", "F20 C18 E20 C19 H18 H17 H19 G17 F18 A17 F19 A18 E16 B15 E17 A15 A19 B19 H16 D16 D19 "
              "E15 G20 C16 G16 F15 G15 D15 E18 C17 D18 B17"),
    ("portD", "J12 W12 H12 W13 Y13 H14 AA13 G14 J13 AF14 H13 AF15 AE13 AC13 AF13 AC14 J14 J15 W14 "
              " Y15 AB16 W15 AB15 AE15 AA15 AD15 Y16 W16 AA14 AD13 AB14 AD14"),

    ("portE", "AF2 AF7 AF1 AF6 AE4 AE9 AE3 AE8 AB7 H9 AB6 J9 J10 H11 K9 G9 K10 G10 J11 G11 AB2 "
              "AC5 AB1 AC4 AD2 AD7 AD1 AD6 E11 F10 E10 F9"),
    ("portF", "Y2 AA5 Y1 AA4 V2 W5 V1 W4 V7 B9 V6 A10 B10 A9 D9 C9 P2 R5 P1 R4 T2 U5 T1 U4 D11 "
              "C11 D10 B11")

]

# portB connects to Asgard
portB_breakout = [

    ("dbg_spi", 0,
     Subsignal("clk", Pins("portB:22")),
     Subsignal("mosi", Pins("portB:18")),
     Subsignal("miso", Pins("portB:16")),
     Subsignal("cs_n", Pins("portB:14")),
     IOStandard("LVCMOS12")
     ),

    # GPIO0 mapping follows table 1 page 8
    # 0 -> pin S0: rcs interrup tinput
    # 1 -> S3: interrupt output
    # 2 -> S6: FollowerID0
    # 3 -> S8: FollowerID1
    # 4 -> S10: imuSysClkSel0
    # 5 -> S12: imuSysClkSel1
    # 6 -> S1: imuDDRReset
    # 7 -> S2: imuDDRFull
    ("gpio", 0,
     Pins(
         "portB:0 portB:1 portB:2 portB:3 portB:4 portB:5 portB:6 portB:7 portB:8 portB:9 portB:10 "
         # "portB:12 portB:24 portB:20 portB:19 portB:21 portB:23 portB:25 portB:27 portB:11 portB:13"),
         "portB:12 portB:24 portB:20 portB:23 portB:25 portB:27 portB:11 portB:13"),
     IOStandard("LVCMOS12"),
     Misc("SLEW SLOW"), Misc("DRIVE 8"),
     # Misc("PULLTYPE PULLDOWN"),
     ),

    # drop 1..5 uses portB:30 and drop 1.9 uses portB:26 for SPMI data
    ("spmi", 0,
     # Subsignal("clk", Pins("portB:28")),
     # Subsignal("data", Pins("portB:26")),

     Subsignal("clk", Pins("portB:28")),
     Subsignal("data", Pins("portB:26")),
     #  Subsignal("data", Pins("portB:24")),
     # Subsignal("data", Pins("portB:30")),
     IOStandard("LVCMOS12"), Misc("DRIVE 8"), Misc("PULLTYPE PULLDOWN"), Misc("SLEW FAST"),
     ),

    # not using for now
    # ("spi", 0,
    #  Subsignal("clk", Pins("portB:S3")),
    #  Subsignal("cs_n", Pins("portB:S5")),
    #  Subsignal("mosi", Pins("portB:S7")),
    #  Subsignal("miso", Pins("portB:S9")),
    #  IOStandard("LVCMOS33"),
    #  ),
    # ("dbg_rst", 0, )

    ("pdm", 0,
     Subsignal("clk", Pins("portB:29")),
     Subsignal("data", Pins("portB:31")),
     IOStandard("LVCMOS12"),
     Misc("SLEW=FAST"),
     ),

    ("serial", 0,
     Subsignal("tx", Pins("portB:15")),
     Subsignal("rx", Pins("portB:17")),
     IOStandard("LVCMOS12"),
     Misc("SLEW=FAST"),
     Misc("PULLTYPE PULLUP"),
     ),

    ("serial", 1,
     Subsignal("tx", Pins("portB:19")),
     Subsignal("rx", Pins("portB:21")),
     IOStandard("LVCMOS12"),
     Misc("SLEW=FAST"),
     Misc("PULLTYPE PULLUP"),
     ),

]
portB_breakout_drop1_5 = [
    # drop 1..5 uses portB:30 and drop 1.9 uses portB:26 for SPMI data
    ("spmi", 0,
     Subsignal("clk", Pins("portB:28")),
     Subsignal("data", Pins("portB:30")),
     IOStandard("LVCMOS12"), Misc("DRIVE 8"), Misc("PULLTYPE PULLDOWN"), Misc("SLEW FAST"),
     ),

    ("dbg_spi", 0,
     Subsignal("clk", Pins("portB:22")),
     Subsignal("mosi", Pins("portB:18")),
     Subsignal("miso", Pins("portB:16")),
     Subsignal("cs_n", Pins("portB:24")),
     IOStandard("LVCMOS12")
     ),

    ("gpio", 0,
     Pins(
         "portB:0 portB:1 portB:2 portB:3 portB:4 portB:5 portB:6 portB:7 portB:8 portB:9 portB:10 portB:11 portB:12 "),
     IOStandard("LVCMOS12"),
     Misc("SLEW SLOW"), Misc("DRIVE 8"),
     # Misc("PULLTYPE PULLDOWN"),
     ),
]

# portD is for debug signals
portD_usb_breakout = [
    ('dbg_io', 0,
     Pins('portC:17 portC:19  portC:6  portC:22  portC:18  portC:16  portC:10 portC:21'),
     IOStandard("LVCMOS12"),),

    # portD usb breakout board with 1->4 usb 2.0
    # using only one pullup pin for all 4 USB ports to save FPGA IO
    # => cannot use other port if port0 is not instantiate
    ("usb", 0,
     Subsignal("d_p", Pins("portD:0")),
     Subsignal("d_n", Pins("portD:2")),
     Subsignal("pullup", Pins("portD:1")),
     IOStandard("LVCMOS33"),
     Misc("SLEW=FAST"),
     # Misc("PULLTYPE PULLDOWN"),
     # Misc("DRIVE 8"),
     ),

    # ("usb", 1,
    #  Subsignal("d_p", Pins("portD:28")),
    #  Subsignal("d_n", Pins("portD:30")),
    #  Subsignal("pullup", Pins("portD:29")),
    #  IOStandard("LVCMOS33"),
    #  Misc("SLEW=FAST")
    #  ),

    # The main Litex serial port using for loading fw
    # ("serial", 0,
    #  Subsignal("tx", Pins("portD:16")),
    #  Subsignal("rx", Pins("portD:18")),
    #  IOStandard("LVCMOS33"),
    #  Misc("SLEW=FAST"),
    #  Misc("PULLTYPE PULLUP"),
    #  ),
    #
    # # fw console
    # ("serial", 1,
    #  Subsignal("tx", Pins("portD:20")),
    #  Subsignal("rx", Pins("portD:22")),
    #  IOStandard("LVCMOS33"),
    #  Misc("SLEW=FAST"),
    #  Misc("PULLTYPE PULLUP"),
    #  ),

    # Amazon breakout board with USB3300
    # following butterstick naming: ulpi0=usb3343 and ulpi1=usb3300
    ("ulpi", 1,
     Subsignal("data", Pins("portD:27 portD:25 portD:23 portD:21 portD:19 "
                            "portD:17 portD:15 portD:13")),
     Subsignal("clk", Pins("portD:24")),
     Subsignal("dir", Pins("portD:14")),
     Subsignal("nxt", Pins("portD:12")),
     Subsignal("stp", Pins("portD:10")),
     Subsignal("rst", Pins("portD:26")),
     IOStandard("LVCMOS33"), Misc("SLEW=FAST")
     ),

    ("pdm", 1,
     Subsignal("clk", Pins("portD:4")),
     Subsignal("data", Pins("portD:6")),
     Subsignal("sel", Pins("portD:8")),
     IOStandard("LVCMOS33"),
     Misc("SLEW=FAST"),
     Misc("PULLTYPE PULLUP"),
     ),
]

portF_pcie_breakout = [
    ("pcie_x4", 0,
     Subsignal("rst_n", Pins("portF:12"), IOStandard("LVCMOS18")),
     Subsignal("clk_p", Pins("portF:8")),
     Subsignal("clk_n", Pins("portF:10")),
     Subsignal("rx_p", Pins("portF:0 portF:4 portF:24 portF:20")),
     Subsignal("rx_n", Pins("portF:2 portF:6 portF:26 portF:22")),
     Subsignal("tx_p", Pins("portF:1 portF:5 portF:25 portF:21")),
     Subsignal("tx_n", Pins("portF:3 portF:7 portF:27 portF:23"))
     ),
]

portE_pcie_breakout = [
    ("pcie_x4", 0,
     Subsignal("rst_n", Pins("portE:12"), IOStandard("LVCMOS18")),
     Subsignal("clk_p", Pins("portE:8")),
     Subsignal("clk_n", Pins("portE:10")),
     Subsignal("rx_p", Pins("portE:0 portE:4 portE:24 portE:20")),
     Subsignal("rx_n", Pins("portE:2 portE:6 portE:26 portE:22")),
     Subsignal("tx_p", Pins("portE:1 portE:5 portE:25 portE:21")),
     Subsignal("tx_n", Pins("portE:3 portE:7 portE:27 portE:23"))
     ),
]


# default clock/reset -------------------

class _CRG(LiteXModule):
    def __init__(self, clk_in_pads, clk_in_freq,
                 sys_clk_freq, margin=1e-2, timestamp_freq=None):
        if timestamp_freq is None:
            timestamp_freq = sys_clk_freq

        self.cd_sys = ClockDomain()
        self.cd_sys4x = ClockDomain()  # for ddrphy
        self.cd_pll4x = ClockDomain()
        self.cd_idelay = ClockDomain()
        self.cd_spmi24Mhz = ClockDomain()
        self.cd_spmi16Mhz = ClockDomain()
        self.cd_timestamp = ClockDomain()

        # self.cd_usb_60 = ClockDomain()
        self.cd_usb_48 = ClockDomain()
        self.cd_usb_12 = ClockDomain()
        self.cd_pll48Mhz = ClockDomain()
        # self.cd_eth = ClockDomain() # enable this if using eth

        self.reset = Signal()

        # PLL.
        self.pll = pll = USPMMCM(speedgrade=-2)
        self.comb += pll.reset.eq(self.reset)
        pll.register_clkin(clk_in_pads, clk_in_freq)

        pll.create_clkout(self.cd_pll4x, sys_clk_freq * 4, margin=margin, buf=None, with_reset=True)
        pll.create_clkout(cd=self.cd_timestamp, freq=timestamp_freq, margin=0)
        pll.create_clkout(cd=self.cd_pll48Mhz, freq=48e6, margin=0, buf=None, with_reset=True)
        pll.create_clkout(cd=self.cd_spmi16Mhz, freq=16e6, with_reset=True)

        # pll.create_clkout(cd=self.cd_usb_60, freq=60e6, margin=0.001, buf='bufgce', ce=1, with_reset=True)
        # pll.create_clkout(cd=self.cd_eth, freq=200e6)

        self.specials += [
            Instance("BUFGCE_DIV",
                     p_BUFGCE_DIVIDE=4,
                     i_CE=1, i_I=self.cd_pll4x.clk, o_O=self.cd_sys.clk),
            Instance("BUFGCE",
                     i_CE=1, i_I=self.cd_pll4x.clk, o_O=self.cd_sys4x.clk),

            # 48Mhz & 12Mhz for USB
            # 24Mhz for SPMI
            Instance("BUFGCE",
                     i_CE=1, i_I=self.cd_pll48Mhz.clk, o_O=self.cd_usb_48.clk),
            # Instance("BUFG",
            #          i_I=self.cd_pll48Mhz.clk, o_O=self.cd_usb_48.clk),
            Instance("BUFGCE_DIV",
                     p_BUFGCE_DIVIDE=2,
                     i_CE=1, i_I=self.cd_pll48Mhz.clk, o_O=self.cd_spmi24Mhz.clk),
            # Instance("BUFGCE_DIV",
            #          p_BUFGCE_DIVIDE=4,
            #          i_CE=1, i_I=self.cd_pll48Mhz.clk, o_O=self.cd_usb_12.clk),

            # Instance("BUFGCE",
            #          i_CE=1, i_I=self.cd_pll48Mhz.clk, o_O=self.cd_usb_48.clk),
        ]

        # for ddr4phy to calib idelay3
        self.idelayctrl = USPIDELAYCTRL(cd_ref=self.cd_sys4x, cd_sys=self.cd_sys)
        # ~crg_locked -> idelay_rst -> (RST)IDELAYCTRL(RDY) -> sys_rst

        config = self.pll.compute_config()
        self.sys_clk_freq = config["clkout0_freq"] / 4
        self.iodelay_clk_freq = config["clkout0_freq"]

        self.timestamp_clk_freq = config["clkout1_freq"]
        self.spmi_clk_freq = 16e6  # config["clkout2_freq"] / 2


# Platform -----------------------------------------------------------------------------------------


class Platform(XilinxUSPPlatform):
    # default_clk_name = "clk"
    # default_clk_period = 1e9 / 100e6
    revision = 0x1
    hw_platform = "xem8320"

    def __init__(self, toolchain="vivado"):
        XilinxUSPPlatform.__init__(self, "xcau25p-ffvb676-2-e", _io, _connectors,
                                   toolchain=toolchain)
        self.sys_clk_freq = None
        self.crg = None
        # USB serial consol cp2104 friend in portA
        # self.platform.add_extension(xem8320_platform.portA_usb_serial_breakout)
        # USB debug breakout board in portD
        self.add_extension(portD_usb_breakout)

        self.add_extension(portE_pcie_breakout)
        self.enable_ok_frontpanel_clk = False

    def get_default_crg(self, sys_clk_freq=150e6, clk_in_ios="clkin100", clk_in_freq=100e6,
                        margin=1e-2):
        self.crg = _CRG(clk_in_pads=self.request(clk_in_ios),
                        clk_in_freq=clk_in_freq,
                        sys_clk_freq=sys_clk_freq,
                        # timestamp_freq=
                        margin=margin)
        # Ignore sys_clk to pll.clkin path created by SoC's rst.
        self.add_false_path_constraints(self.crg.cd_sys.clk,
                                        self.crg.pll.clkin)
        self.add_false_path_constraints(self.crg.cd_spmi24Mhz.clk,
                                        self.crg.pll.clkin)
        self.add_false_path_constraints(self.crg.cd_sys.clk,
                                        self.crg.cd_spmi24Mhz.clk)

        self.add_false_path_constraints(self.crg.cd_spmi16Mhz.clk,
                                        self.crg.pll.clkin)
        self.add_false_path_constraints(self.crg.cd_sys.clk,
                                        self.crg.cd_spmi16Mhz.clk)

        self.add_false_path_constraints(self.crg.cd_sys.clk,
                                        self.crg.cd_pll48Mhz.clk)

        self.add_false_path_constraints(self.crg.cd_sys.clk,
                                        self.crg.cd_usb_48.clk)
        self.add_false_path_constraints(self.crg.cd_sys.clk,
                                        self.crg.cd_usb_12.clk)
        # self.add_false_path_constraints(self.crg.cd_sys.clk,
        #                                 self.crg.cd_eth.clk)
        # self.add_false_path_constraints(self.crg.cd_sys.clk,
        #                                 self.crg.cd_usb_60.clk)

        self.sys_clk_freq = self.crg.sys_clk_freq
        return self.crg

    def create_programmer(self, ftdi_serial=None):
        return OpenFPGALoader(fpga_part="xcau25p-ffvb676", cable="ft232", freq=15_000_000,
                              ftdi_serial=ftdi_serial)

    def do_finalize(self, fragment, *args, **kwargs):
        XilinxUSPPlatform.do_finalize(self, fragment)
        # For passively cooled boards, overheating is a significant risk if airflow isn't sufficient
        self.add_platform_command(
            "set_property BITSTREAM.CONFIG.OVERTEMPSHUTDOWN ENABLE [current_design]")
        # Reduce programming time
        self.add_platform_command("set_property BITSTREAM.GENERAL.COMPRESS True [current_design]")

        # disable clock gate in Apple IP
        self.add_platform_command("set_property VERILOG_DEFINE {{PROTO_FPGA}} [current_fileset]")

        # Configuration Bank Voltage Select 1.8V
        self.add_platform_command("set_property CFGBVS GND [current_design]")

        # DDR4 memory channel C0 Clock constraint / Internal Vref
        self.add_platform_command("set_property INTERNAL_VREF 0.84 [get_iobanks 64]")

        self.add_period_constraint(self.lookup_request("clkin100", 0, loose=True), 1e9 / 100e6)
        self.add_period_constraint(self.lookup_request("clkin100_ddr", 0, loose=True), 1e9 / 100e6)
        self.add_period_constraint(self.lookup_request("clkin125", 0, loose=True), 1e9 / 100e6)
        self.add_period_constraint(self.lookup_request("clkin156_25", 0, loose=True),
                                   1e9 / 156.25e6)

        # # spmi clock pin
        # self.add_period_constraint(clk=self.crg.cd_spmi_clk_pin.clk, period=41.667)
        # self.add_false_path_constraints(self.crg.cd_spmi_clk_pin.clk, self.crg.cd_spmi24Mhz.clk)
        xdc_lines = [
            # 'set_false_path -from [get_registers {*aplStdLibSyncRegs*}]',
            # 'set_false_path -from [get_pins -hierarchical -filter {NAME =~ "*SYNC_FF.q*"}] -to [get_cells -hierarchical -filter {NAME =~ "*SYNC_FF.q_reg*"}]',
            # 'set_false_path -through [get_pins -hierarchical -filter {REF_NAME =~ "*genblk_hard[*].genblk_present.q_hard/agent/response_queue_inst/storage_array/write/core/data_array/useGate.storage_array_clkgate/SYNC_FF.q[*]_i_*"}]',
            # 'set_false_path -through [get_pins -of_objects [get_cells -hierarchical -filter {NAME =~ "*queues/arrays/genblk_hard[*].genblk_present.q_hard/agent/response_queue_inst/storage_array/write/core/data_array/useGate.storage_array_clkgate"}]]',
        ]

        if self.enable_ok_frontpanel_clk:
            self.add_platform_command(
                "create_clock -period 9.920 -name okUH0 [get_ports {{okHost_okUH[0]}}]")
            # Manual placing OK frontpanel MMCM or PLL near the pins, otherwise Frontpanel won't work
            xdc_lines += [
                'create_pblock pblock_mmcme4_okFrontPanel',
                'add_cells_to_pblock [get_pblocks pblock_mmcme4_okFrontPanel] [get_cells -hier -filter {{NAME =~ "*okHostInst/mmcm0"}}]',
                'add_cells_to_pblock [get_pblocks pblock_mmcme4_okFrontPanel] [get_cells -hier -filter {{NAME =~ "*okHostInst/pllFrontpanelMain"}}]',
                'resize_pblock [get_pblocks pblock_mmcme4_okFrontPanel] -add {{CLOCKREGION_X0Y0:CLOCKREGION_X0Y0}}',
                'set_property CLOCK_DEDICATED_ROUTE BACKBONE [get_nets -hier -filter {{NAME =~ "*/okHostInst/okUH0_ibufg"}}]',

                # 'create_clock -period 16.667 -name ulpi0_clk [get_ports ulpi0_clk]',
                # 'set_property CLOCK_DEDICATED_ROUTE FALSE [get_nets ulpi0_clk]'

                # 'set_property BITSTREAM.CONFIG.EXTMASTERCCLK_EN disable [current_design]',
                # 'set_property CONFIG_MODE SPIx4 [current_design]',
                # 'set_property BITSTREAM.CONFIG.SPI_BUSWIDTH 4 [current_design]',
                # 'set_property BITSTREAM.CONFIG.SPI_FALL_EDGE YES [current_design]',
                # 'set_property BITSTREAM.CONFIG.CONFIGRATE 102.0 [current_design]',
                # 'set_property BITSTREAM.CONFIG.CONFIGRATE 85.0 [current_design]',

            ]

        for line in xdc_lines:
            self.add_platform_command(line)

        # self.add_false_path_constraints(self.crg.cd_sys.clk, self.crg.cd_usb.clk)

        # self.add_platform_command("set_property CLOCK_DEDICATED_ROUTE BACKBONE [get_nets basesoc_crg_clkin]")
        # self.add_platform_command("set_property CLOCK_DEDICATED_ROUTE BACKBONE [get_nets main_crg_clkin]")
