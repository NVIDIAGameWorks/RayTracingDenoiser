#pragma once

template<typename U, typename T>
using Map = std::map<U, T, std::less<U>, StdAllocator<std::pair<U, T>>>;

struct HelperDeviceMemoryAllocator
{
    HelperDeviceMemoryAllocator(const nri::CoreInterface& NRI, nri::Device& device, const StdAllocator<uint8_t>& stdAllocator);

    uint32_t CalculateAllocationNumber(const nri::ResourceGroupDesc& resourceGroupDesc);
    nri::Result AllocateAndBindMemory(const nri::ResourceGroupDesc& resourceGroupDesc, nri::Memory** allocations);

private:
    struct MemoryTypeGroup;

    nri::Result TryToAllocateAndBindMemory(const nri::ResourceGroupDesc& resourceGroupDesc, nri::Memory** allocations, size_t& allocationNum);
    nri::Result ProcessMemoryTypeGroup(nri::MemoryType memoryType, MemoryTypeGroup& group, nri::Memory** allocations, size_t& allocationNum);
    nri::Result ProcessDedicatedResources(nri::MemoryLocation memoryLocation, nri::Memory** allocations, size_t& allocationNum);
    void GroupByMemoryType(nri::MemoryLocation memoryLocation, nri::Buffer* const* buffers, uint32_t bufferNum);
    void GroupByMemoryType(nri::MemoryLocation memoryLocation, nri::Texture* const* textures, uint32_t textureNum);
    void FillMemoryBindingDescs(nri::Buffer* const* buffers, const uint64_t* bufferOffsets, uint32_t bufferNum, nri::Memory& memory);
    void FillMemoryBindingDescs(nri::Texture* const* texture, const uint64_t* textureOffsets, uint32_t textureNum, nri::Memory& memory);

    struct MemoryTypeGroup
    {
        MemoryTypeGroup(const StdAllocator<uint8_t>& stdAllocator);

        Vector<nri::Buffer*> buffers;
        Vector<uint64_t> bufferOffsets;
        Vector<nri::Texture*> textures;
        Vector<uint64_t> textureOffsets;
        uint64_t memoryOffset;
    };

    const nri::CoreInterface& m_NRI;
    nri::Device& m_Device;
    const StdAllocator<uint8_t>& m_StdAllocator;

    Map<nri::MemoryType, MemoryTypeGroup> m_Map;
    Vector<nri::Buffer*> m_DedicatedBuffers;
    Vector<nri::Texture*> m_DedicatedTextures;
    Vector<nri::BufferMemoryBindingDesc> m_BufferBindingDescs;
    Vector<nri::TextureMemoryBindingDesc> m_TextureBindingDescs;
};

