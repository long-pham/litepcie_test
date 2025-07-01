import os
import litepcie
from litepcie.phy.usppciephy import USPPCIEPHY as BaseUSPPCIEPHY
from litepcie.phy.common import get_bar_mask


class CustomUSPPCIEPHY(BaseUSPPCIEPHY):
    """Custom USPPCIEPHY with support for multiple BARs"""

    def __init__(self, platform, pads, speed="gen3", axisten_freq=250, data_width=64, cd="sys",
        # PCIe hardblock parameters.
        ip_name         = "pcie4_uscale_plus",
        pcie_data_width = None,
        bar0_size       = 0x100000,
        bar1_size       = 0,
        bar2_size       = 0,
        bar3_size       = 0,
        bar4_size       = 0,
        bar5_size       = 0,
    ):
        self.axisten_freq = axisten_freq
        # Store all BAR sizes
        self.bar1_size = bar1_size
        self.bar2_size = bar2_size
        self.bar3_size = bar3_size
        self.bar4_size = bar4_size
        self.bar5_size = bar5_size

        # Calculate BAR masks
        self.bar1_mask = get_bar_mask(bar1_size) if bar1_size > 0 else 0
        self.bar2_mask = get_bar_mask(bar2_size) if bar2_size > 0 else 0
        self.bar3_mask = get_bar_mask(bar3_size) if bar3_size > 0 else 0
        self.bar4_mask = get_bar_mask(bar4_size) if bar4_size > 0 else 0
        self.bar5_mask = get_bar_mask(bar5_size) if bar5_size > 0 else 0

        # Call parent constructor
        super().__init__(
            platform=platform,
            pads=pads,
            speed=speed,
            data_width=data_width,
            cd=cd,
            ip_name=ip_name,
            pcie_data_width=pcie_data_width,
            bar0_size=bar0_size
        )

    def add_sources(self, platform, phy_path=None, phy_filename=None):
        """Override to add multi-BAR configuration"""
        if phy_filename is not None:
            platform.add_ip(os.path.join(phy_path, phy_filename))
        else:

            """
            set_property -dict [list \
  CONFIG.PL_LINK_CAP_MAX_LINK_SPEED {8.0_GT/s} \
  CONFIG.PL_LINK_CAP_MAX_LINK_WIDTH {X4} \
  CONFIG.axisten_freq {250} \
  CONFIG.enable_ibert {true} \
  CONFIG.enable_jtag_dbg {true} \
  CONFIG.mode_selection {Advanced} \
  CONFIG.pf0_bar0_64bit {true} \
  CONFIG.pf0_bar0_prefetchable {true} \
  CONFIG.pf0_bar0_scale {Megabytes} \
  CONFIG.pf0_bar0_size {128} \
  CONFIG.pf0_bar2_64bit {true} \
  CONFIG.pf0_bar2_enabled {true} \
  CONFIG.pf0_bar2_prefetchable {true} \
  CONFIG.pf0_bar4_64bit {true} \
  CONFIG.pf0_bar4_enabled {true} \
  CONFIG.pf0_bar4_prefetchable {true} \
  CONFIG.pf0_dsn_enabled {true} \
] [get_ips pcie4_uscale_plus_0]

set_property -dict [list \
  CONFIG.PF0_Use_Class_Code_Lookup_Assistant {true} \
  CONFIG.PL_LINK_CAP_MAX_LINK_SPEED {16.0_GT/s} \
  CONFIG.axisten_if_width {128_bit} \
  CONFIG.pcie_id_if {true} \
  CONFIG.pf0_bar0_64bit {true} \
  CONFIG.pf0_bar0_prefetchable {true} \
  CONFIG.pf0_bar0_scale {Megabytes} \
  CONFIG.pf0_bar2_64bit {true} \
  CONFIG.pf0_bar2_enabled {true} \
  CONFIG.pf0_bar2_prefetchable {true} \
  CONFIG.pf0_bar2_scale {Megabytes} \
  CONFIG.pf0_base_class_menu {Base_system_peripherals} \
] [get_ips pcie4c_uscale_plus_0]
set_property -dict [list \
  CONFIG.PL_LINK_CAP_MAX_LINK_SPEED {16.0_GT/s} \
  CONFIG.axisten_if_width {128_bit} \
  CONFIG.enable_ibert {true} \
  CONFIG.enable_jtag_dbg {true} \
  CONFIG.mode_selection {Advanced} \
] [get_ips pcie4c_uscale_plus_0]

            """
            # Base configuration
            config = {
                # Generic Config.
                # ---------------
                "Component_Name": "pcie_usp",
                "PL_LINK_CAP_MAX_LINK_WIDTH": f"X{self.nlanes}",
                "PL_LINK_CAP_MAX_LINK_SPEED": {"gen3": "8.0_GT/s", "gen4": "16.0_GT/s"}[self.speed],
                "axisten_if_width": f"{self.pcie_data_width}_bit",
                "AXISTEN_IF_RC_STRADDLE": False,
                "PF0_DEVICE_ID": {"gen3": 9030, "gen4": 9040}[self.speed] + self.nlanes,
                # "axisten_freq": 250,  # CHECKME.
                "axisten_freq": self.axisten_freq,  # CHECKME.
                "axisten_if_enable_client_tag": True,
                "aspm_support": "No_ASPM",
                "coreclk_freq": 500,  # CHECKME.
                "plltype": "QPLL0",

                # 'enable_ibert': True,
                # 'enable_jtag_dbg': True,
                # 'PF0_Use_Class_Code_Lookup_Assistant': True,
                # 'pcie_id_if': True,

                # BAR0 Config.
                # ------------
                # 'pf0_bar0_64bit': True,
                # 'pf0_bar0_prefetchable': True,
                "pf0_bar0_scale": "Megabytes",  # FIXME.
                "pf0_bar0_size": max(self.bar0_size / (1024*1024), 1),  # FIXME.
                'pf0_base_class_menu': 'Base_system_peripherals',

                # Interrupt Config.
                # -----------------
                "PF0_INTERRUPT_PIN": "NONE",

                #
                # # Generic Config.
                # "PL_LINK_CAP_MAX_LINK_WIDTH"   : f"X{self.nlanes}",
                # "PL_LINK_CAP_MAX_LINK_SPEED"   : {"gen3": "8.0_GT/s", "gen4": "16.0_GT/s"}[self.speed],
                # "axisten_if_width"             : f"{self.pcie_data_width}_bit",
                # "AXISTEN_IF_RC_STRADDLE"       : False,
                # "PF0_DEVICE_ID"                : {"gen3": 9030, "gen4": 9040}[self.speed] + self.nlanes,
                # "axisten_freq"                 : 250,#self.axisten_freq,
                # "axisten_if_enable_client_tag" : True,
                # "aspm_support"                 : "No_ASPM",
                # "plltype"                      : "QPLL0",
                #
                # # BAR0 Config (always enabled in base class)
                # "pf0_bar0_scale"               : "Megabytes",
                # "pf0_bar0_size"                : max(self.bar0_size // (1024*1024), 1),  # Convert to MB
                #
                # # Interrupt Config
                # "PF0_INTERRUPT_PIN"            : "NONE",
            }

            # Add additional BARs if enabled
            # Note: The PCIe IP core has limitations on BAR sizes
            # Valid sizes for bars other than BAR0 are: 1, 2, 4, 8, 16, 32, 64, 128, 256, 512 (in scale units)

            if self.bar2_size > 0:
                # Calculate size in MB and ensure it's a valid value
                bar2_size_mb = self.bar2_size // (1024*1024)
                # Round to nearest valid size (powers of 2)
                if bar2_size_mb > 512: bar2_size_mb = 512
                elif bar2_size_mb > 256: bar2_size_mb = 256
                elif bar2_size_mb > 128: bar2_size_mb = 128
                elif bar2_size_mb > 64: bar2_size_mb = 64
                elif bar2_size_mb > 32: bar2_size_mb = 32
                elif bar2_size_mb > 16: bar2_size_mb = 16
                elif bar2_size_mb > 8: bar2_size_mb = 8
                elif bar2_size_mb > 4: bar2_size_mb = 4
                elif bar2_size_mb > 2: bar2_size_mb = 2
                elif bar2_size_mb > 0: bar2_size_mb = 1

                config.update({
                    "pf0_bar2_enabled"         : "true",
                    "pf0_bar2_type"            : "Memory",
                    "pf0_bar2_64bit"           : "true",   # 64-bit BAR (uses BAR2+BAR3)
                    "pf0_bar2_prefetchable"    : "true",
                    "pf0_bar2_scale"           : "Megabytes",
                    "pf0_bar2_size"            : bar2_size_mb,
                })

            if self.bar4_size > 0:
                # Calculate size in MB and ensure it's a valid value
                bar4_size_mb = self.bar4_size // (1024*1024)
                # Round to nearest valid size (powers of 2)
                if bar4_size_mb > 512: bar4_size_mb = 512
                elif bar4_size_mb > 256: bar4_size_mb = 256
                elif bar4_size_mb > 128: bar4_size_mb = 128
                elif bar4_size_mb > 64: bar4_size_mb = 64
                elif bar4_size_mb > 32: bar4_size_mb = 32
                elif bar4_size_mb > 16: bar4_size_mb = 16
                elif bar4_size_mb > 8: bar4_size_mb = 8
                elif bar4_size_mb > 4: bar4_size_mb = 4
                elif bar4_size_mb > 2: bar4_size_mb = 2
                elif bar4_size_mb > 0: bar4_size_mb = 1

                config.update({
                    "pf0_bar4_enabled"         : "true",
                    "pf0_bar4_type"            : "Memory",
                    "pf0_bar4_64bit"           : "true",   # 64-bit BAR (uses BAR4+BAR5)
                    "pf0_bar4_prefetchable"    : "false",
                    "pf0_bar4_scale"           : "Megabytes",
                    "pf0_bar4_size"            : bar4_size_mb,
                })

            # Note: BAR3 and BAR5 are consumed by 64-bit BARs 2 and 4 respectively
            # If you want 32-bit BARs, you can configure them separately

            # User/Custom Config
            config.update(self.config)

            # Tcl generation
            ip_tcl  = []
            ip_tcl.append(f"create_ip -vendor xilinx.com -name {self.ip_name} -module_name pcie_usp")
            ip_tcl.append("set obj [get_ips pcie_usp]")
            ip_tcl.append("set_property -dict [list \\")
            for config_key, value in config.items():
                ip_tcl.append("CONFIG.{} {} \\".format(config_key, '{{' + str(value) + '}}'))
            ip_tcl.append(f"] $obj")
            ip_tcl.append("synth_ip $obj")
            platform.toolchain.pre_synthesis_commands += ip_tcl

        # Add verilog sources
        # Get the path to litepcie package
        litepcie_path = os.path.dirname(litepcie.__file__)
        verilog_path = os.path.join(litepcie_path, "phy", "xilinx_usp")

        platform.add_source(os.path.join(verilog_path, "axis_iff.v"))
        platform.add_source(os.path.join(verilog_path, f"s_axis_rq_adapt_{self.pcie_data_width}b.v"))
        platform.add_source(os.path.join(verilog_path, f"m_axis_rc_adapt_{self.pcie_data_width}b.v"))
        platform.add_source(os.path.join(verilog_path, f"m_axis_cq_adapt_{self.pcie_data_width}b.v"))
        platform.add_source(os.path.join(verilog_path, f"s_axis_cc_adapt_{self.pcie_data_width}b.v"))
        platform.add_source(os.path.join(verilog_path, "pcie_usp_support.v"))
