// LitePCIeDriver.cpp - Main DriverKit driver implementation
// Based on Ubuntu LitePCIe kernel module architecture

#include <DriverKit/DriverKit.h>
#include <PCIDriverKit/PCIDriverKit.h>
#include <DriverKit/IOUserServer.h>
#include <DriverKit/IOLib.h>
#include <DriverKit/IOBufferMemoryDescriptor.h>
#include <DriverKit/IODMACommand.h>
#include <DriverKit/IOInterruptEventSource.h>

#include "LitePCIeDriver.h"
#include "LitePCIeRegisters.h"

#define kDriverName "LitePCIeDriver"
#define kClientClass "LitePCIeDriverClient"

// DMA Configuration (matching Ubuntu implementation)
#define DMA_BUFFER_COUNT 256
#define DMA_BUFFER_SIZE 8192
#define DMA_BUFFER_TOTAL (DMA_BUFFER_COUNT * DMA_BUFFER_SIZE)

struct ClassData {
    OSAction* interruptAction;
    IOInterruptEventSource* interruptEventSource;
    IOMemoryMap* registerMap;
    
    // DMA Management
    struct DMAChannel {
        IOBufferMemoryDescriptor* buffers[DMA_BUFFER_COUNT];
        IODMACommand* dmaCommands[DMA_BUFFER_COUNT];
        uint64_t physicalAddresses[DMA_BUFFER_COUNT];
        uint32_t sw_reader_idx;
        uint32_t sw_writer_idx;
        uint32_t hw_reader_idx;
        uint32_t hw_writer_idx;
        bool enabled;
    } dmaChannels[2]; // 0: Reader/RX, 1: Writer/TX
    
    // Performance counters
    uint64_t rxBytes;
    uint64_t txBytes;
    uint64_t rxPackets;
    uint64_t txPackets;
    
    // Device state
    bool isOpen;
    uint32_t generation;
};

impl LitePCIeDriver
{
    LitePCIeDriver::~LitePCIeDriver() {
        IOLog("%s: Destructor called\n", kDriverName);
    }
    
    bool LitePCIeDriver::init() {
        if (!super::init()) {
            return false;
        }
        
        ivars = new ClassData;
        if (!ivars) {
            return false;
        }
        
        memset(ivars, 0, sizeof(ClassData));
        return true;
    }
    
    void LitePCIeDriver::free() {
        IOLog("%s: free() called\n", kDriverName);
        
        if (ivars) {
            // Clean up DMA resources
            for (int ch = 0; ch < 2; ch++) {
                for (int i = 0; i < DMA_BUFFER_COUNT; i++) {
                    OSSafeReleaseNULL(ivars->dmaChannels[ch].buffers[i]);
                    OSSafeReleaseNULL(ivars->dmaChannels[ch].dmaCommands[i]);
                }
            }
            
            delete ivars;
            ivars = nullptr;
        }
        
        super::free();
    }
    
    kern_return_t LitePCIeDriver::Start(IOService* provider) {
        kern_return_t ret;
        IOPCIDevice* pciDevice;
        
        IOLog("%s: Start() called\n", kDriverName);
        
        ret = super::Start(provider, SUPERDISPATCH);
        if (ret != kIOReturnSuccess) {
            return ret;
        }
        
        pciDevice = OSDynamicCast(IOPCIDevice, provider);
        if (!pciDevice) {
            return kIOReturnBadArgument;
        }
        
        // Enable bus mastering and memory space
        ret = pciDevice->SetBusMasterEnable(true);
        if (ret != kIOReturnSuccess) {
            IOLog("%s: Failed to enable bus mastering\n", kDriverName);
            return ret;
        }
        
        ret = pciDevice->SetMemoryEnable(true);
        if (ret != kIOReturnSuccess) {
            IOLog("%s: Failed to enable memory space\n", kDriverName);
            return ret;
        }
        
        // Map BAR0 (registers)
        ret = pciDevice->MapDeviceMemoryWithIndex(0, &ivars->registerMap);
        if (ret != kIOReturnSuccess) {
            IOLog("%s: Failed to map BAR0\n", kDriverName);
            return ret;
        }
        
        // Initialize device
        ret = InitializeDevice();
        if (ret != kIOReturnSuccess) {
            return ret;
        }
        
        // Set up interrupts
        ret = SetupInterrupts(pciDevice);
        if (ret != kIOReturnSuccess) {
            return ret;
        }
        
        // Allocate DMA buffers
        ret = AllocateDMABuffers();
        if (ret != kIOReturnSuccess) {
            return ret;
        }
        
        // Register with power management
        ret = RegisterService();
        if (ret != kIOReturnSuccess) {
            return ret;
        }
        
        IOLog("%s: Started successfully\n", kDriverName);
        return kIOReturnSuccess;
    }
    
    kern_return_t LitePCIeDriver::Stop(IOService* provider) {
        IOLog("%s: Stop() called\n", kDriverName);
        
        // Disable DMA channels
        DisableDMA(0);
        DisableDMA(1);
        
        // Disable interrupts
        if (ivars->interruptEventSource) {
            ivars->interruptEventSource->Disable();
            OSSafeReleaseNULL(ivars->interruptEventSource);
        }
        
        // Unmap registers
        OSSafeReleaseNULL(ivars->registerMap);
        
        return super::Stop(provider, SUPERDISPATCH);
    }
    
    // Device initialization
    kern_return_t LitePCIeDriver::InitializeDevice() {
        IOLog("%s: Initializing device\n", kDriverName);
        
        // Read scratch register to verify access
        uint32_t scratch = ReadRegister(SCRATCH_REG);
        IOLog("%s: Scratch register: 0x%08x\n", kDriverName, scratch);
        
        // Write test pattern
        WriteRegister(SCRATCH_REG, 0xDEADBEEF);
        uint32_t readback = ReadRegister(SCRATCH_REG);
        if (readback != 0xDEADBEEF) {
            IOLog("%s: Register access test failed\n", kDriverName);
            return kIOReturnIOError;
        }
        
        // Reset DMA engines
        WriteRegister(DMA0_CONTROL_REG, DMA_RESET);
        WriteRegister(DMA1_CONTROL_REG, DMA_RESET);
        
        // Clear reset
        WriteRegister(DMA0_CONTROL_REG, 0);
        WriteRegister(DMA1_CONTROL_REG, 0);
        
        return kIOReturnSuccess;
    }
    
    // Interrupt setup
    kern_return_t LitePCIeDriver::SetupInterrupts(IOPCIDevice* pciDevice) {
        kern_return_t ret;
        
        // Create interrupt action
        ret = CreateActionInterruptOccurred(sizeof(ClassData), &ivars->interruptAction);
        if (ret != kIOReturnSuccess) {
            return ret;
        }
        
        // Create interrupt event source
        ret = IOInterruptEventSource::Create(this, 
                                           ivars->interruptAction,
                                           pciDevice,
                                           0, // interrupt index
                                           &ivars->interruptEventSource);
        if (ret != kIOReturnSuccess) {
            return ret;
        }
        
        // Enable interrupts
        ret = ivars->interruptEventSource->Enable();
        if (ret != kIOReturnSuccess) {
            return ret;
        }
        
        // Enable device interrupts
        WriteRegister(INTERRUPT_ENABLE_REG, INTERRUPT_DMA0 | INTERRUPT_DMA1);
        
        return kIOReturnSuccess;
    }
    
    // DMA buffer allocation
    kern_return_t LitePCIeDriver::AllocateDMABuffers() {
        kern_return_t ret;
        
        IOLog("%s: Allocating DMA buffers (%d x %d bytes)\n", 
              kDriverName, DMA_BUFFER_COUNT, DMA_BUFFER_SIZE);
        
        for (int ch = 0; ch < 2; ch++) {
            for (int i = 0; i < DMA_BUFFER_COUNT; i++) {
                // Create buffer memory descriptor
                ret = IOBufferMemoryDescriptor::Create(kIOMemoryDirectionInOut,
                                                     DMA_BUFFER_SIZE,
                                                     0, // alignment
                                                     &ivars->dmaChannels[ch].buffers[i]);
                if (ret != kIOReturnSuccess) {
                    IOLog("%s: Failed to allocate buffer %d for channel %d\n", 
                          kDriverName, i, ch);
                    return ret;
                }
                
                // Create DMA command
                ret = IODMACommand::Create(this,
                                         kIODMACommandSpecification64Bit,
                                         &ivars->dmaChannels[ch].dmaCommands[i]);
                if (ret != kIOReturnSuccess) {
                    return ret;
                }
                
                // Prepare DMA command with buffer
                ret = ivars->dmaChannels[ch].dmaCommands[i]->PrepareForDMA(
                    kIODMACommandPrepareForDMANoOptions,
                    ivars->dmaChannels[ch].buffers[i],
                    0, // offset
                    DMA_BUFFER_SIZE);
                if (ret != kIOReturnSuccess) {
                    return ret;
                }
                
                // Get physical address
                IODMACommandStatus status;
                IOAddressSegment segment;
                uint32_t numSegments = 1;
                
                ret = ivars->dmaChannels[ch].dmaCommands[i]->GetAddressRange(
                    &segment, &numSegments);
                if (ret != kIOReturnSuccess || numSegments != 1) {
                    return kIOReturnIOError;
                }
                
                ivars->dmaChannels[ch].physicalAddresses[i] = segment.address;
            }
        }
        
        return kIOReturnSuccess;
    }
    
    // Interrupt handler
    void LitePCIeDriver::InterruptOccurred(OSAction* action, 
                                          uint64_t timestamp) {
        uint32_t status = ReadRegister(INTERRUPT_STATUS_REG);
        
        if (status & INTERRUPT_DMA0) {
            HandleDMAInterrupt(0);
        }
        
        if (status & INTERRUPT_DMA1) {
            HandleDMAInterrupt(1);
        }
        
        // Clear interrupts
        WriteRegister(INTERRUPT_STATUS_REG, status);
    }
    
    void LitePCIeDriver::HandleDMAInterrupt(int channel) {
        auto& dma = ivars->dmaChannels[channel];
        
        // Update hardware indices
        if (channel == 0) { // Reader/RX
            dma.hw_writer_idx = ReadRegister(DMA0_WRITER_REG);
            // Process received buffers
            while (dma.sw_reader_idx != dma.hw_writer_idx) {
                // Buffer at sw_reader_idx has data
                ivars->rxPackets++;
                ivars->rxBytes += DMA_BUFFER_SIZE;
                
                // Advance reader index
                dma.sw_reader_idx = (dma.sw_reader_idx + 1) % DMA_BUFFER_COUNT;
            }
            // Update hardware reader index
            WriteRegister(DMA0_READER_REG, dma.sw_reader_idx);
        } else { // Writer/TX
            dma.hw_reader_idx = ReadRegister(DMA1_READER_REG);
            // Mark transmitted buffers as available
            while (dma.sw_writer_idx != dma.hw_reader_idx) {
                ivars->txPackets++;
                ivars->txBytes += DMA_BUFFER_SIZE;
                
                // Buffer can be reused
                dma.sw_writer_idx = (dma.sw_writer_idx + 1) % DMA_BUFFER_COUNT;
            }
        }
    }
    
    // Register access helpers
    uint32_t LitePCIeDriver::ReadRegister(uint32_t offset) {
        if (!ivars->registerMap) {
            return 0;
        }
        
        volatile uint32_t* reg = (volatile uint32_t*)
            ((uint8_t*)ivars->registerMap->GetAddress() + offset);
        return *reg;
    }
    
    void LitePCIeDriver::WriteRegister(uint32_t offset, uint32_t value) {
        if (!ivars->registerMap) {
            return;
        }
        
        volatile uint32_t* reg = (volatile uint32_t*)
            ((uint8_t*)ivars->registerMap->GetAddress() + offset);
        *reg = value;
    }
    
    // DMA control
    kern_return_t LitePCIeDriver::EnableDMA(int channel) {
        if (channel < 0 || channel > 1) {
            return kIOReturnBadArgument;
        }
        
        auto& dma = ivars->dmaChannels[channel];
        
        // Program buffer table base address
        uint64_t tableBase = /* physical address of buffer table */;
        if (channel == 0) {
            WriteRegister(DMA0_TABLE_BASE_LOW, tableBase & 0xFFFFFFFF);
            WriteRegister(DMA0_TABLE_BASE_HIGH, tableBase >> 32);
            WriteRegister(DMA0_CONTROL_REG, DMA_ENABLE | DMA_IRQ_ENABLE);
        } else {
            WriteRegister(DMA1_TABLE_BASE_LOW, tableBase & 0xFFFFFFFF);
            WriteRegister(DMA1_TABLE_BASE_HIGH, tableBase >> 32);
            WriteRegister(DMA1_CONTROL_REG, DMA_ENABLE | DMA_IRQ_ENABLE);
        }
        
        dma.enabled = true;
        return kIOReturnSuccess;
    }
    
    kern_return_t LitePCIeDriver::DisableDMA(int channel) {
        if (channel < 0 || channel > 1) {
            return kIOReturnBadArgument;
        }
        
        auto& dma = ivars->dmaChannels[channel];
        
        // Disable DMA engine
        if (channel == 0) {
            WriteRegister(DMA0_CONTROL_REG, 0);
        } else {
            WriteRegister(DMA1_CONTROL_REG, 0);
        }
        
        dma.enabled = false;
        return kIOReturnSuccess;
    }
    
    // User client support
    kern_return_t LitePCIeDriver::NewUserClient(uint32_t type,
                                               IOUserClient** userClient) {
        IOLog("%s: NewUserClient type %d\n", kDriverName, type);
        
        IOService* client;
        kern_return_t ret = Create(this, kClientClass, &client);
        if (ret != kIOReturnSuccess) {
            return ret;
        }
        
        *userClient = OSDynamicCast(IOUserClient, client);
        if (!*userClient) {
            client->release();
            return kIOReturnBadArgument;
        }
        
        return kIOReturnSuccess;
    }
}