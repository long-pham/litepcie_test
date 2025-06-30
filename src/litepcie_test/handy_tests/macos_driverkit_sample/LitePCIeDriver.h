// LitePCIeDriver.h - DriverKit driver header
#ifndef LITEPCIE_DRIVER_H
#define LITEPCIE_DRIVER_H

#include <DriverKit/IOService.iig>
#include <DriverKit/IOUserClient.iig>

class LitePCIeDriver : public IOService
{
public:
    virtual bool init() override;
    virtual void free() override;
    
    virtual kern_return_t Start(IOService* provider) override;
    virtual kern_return_t Stop(IOService* provider) override;
    
    // Interrupt handling
    virtual void InterruptOccurred(OSAction* action, 
                                 uint64_t timestamp) TYPE(IOInterruptEventSource::Action);
    
    // User client support
    virtual kern_return_t NewUserClient(uint32_t type,
                                      IOUserClient** userClient) override;
    
    // DMA control methods
    virtual kern_return_t EnableDMA(int channel);
    virtual kern_return_t DisableDMA(int channel);
    virtual kern_return_t ConfigureDMA(int channel, uint32_t config);
    virtual kern_return_t GetDMAStatus(int channel, uint64_t* status);
    
    // Register access
    virtual uint32_t ReadRegister(uint32_t offset);
    virtual void WriteRegister(uint32_t offset, uint32_t value);
    
private:
    // Internal methods
    kern_return_t InitializeDevice();
    kern_return_t SetupInterrupts(IOPCIDevice* pciDevice);
    kern_return_t AllocateDMABuffers();
    void HandleDMAInterrupt(int channel);
    
    // Private data
    struct ClassData* ivars;
};

#endif // LITEPCIE_DRIVER_H