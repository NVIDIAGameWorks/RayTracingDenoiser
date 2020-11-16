#pragma once

struct HelperDataUpload
{
    HelperDataUpload(const nri::CoreInterface& NRI, nri::Device& device, const StdAllocator<uint8_t>& stdAllocator, nri::CommandQueue& commandQueue);

    nri::Result UploadData(const nri::TextureUploadDesc* textureDataDescs, uint32_t textureDataDescNum, const nri::BufferUploadDesc* bufferDataDescs, uint32_t bufferDataDescNum);

private:
    nri::Result Create();
    void Destroy();
    nri::Result UploadTextures(const nri::TextureUploadDesc* textureDataDescs, uint32_t textureDataDescNum);
    nri::Result UploadBuffers(const nri::BufferUploadDesc* bufferDataDescs, uint32_t bufferDataDescNum);
    nri::Result BeginCommandBuffers();
    nri::Result EndCommandBuffersAndSubmit();
    bool CopyTextureContent(const nri::TextureUploadDesc& textureDataDesc, uint16_t& arrayOffset, uint16_t& mipOffset, bool& isCapacityInsufficient);
    void CopyTextureSubresourceContent(const nri::TextureSubresourceUploadDesc& subresource, uint64_t alignedRowPitch, uint64_t alignedSlicePitch);
    bool CopyBufferContent(const nri::BufferUploadDesc& bufferDataDesc, uint64_t& bufferContentOffset);
    template<bool isInitialTransition>
    nri::Result DoTransition(const nri::TextureUploadDesc* textureDataDescs, uint32_t textureDataDescNum);
    template<bool isInitialTransition>
    nri::Result DoTransition(const nri::BufferUploadDesc* bufferDataDescs, uint32_t bufferDataDescNum);

    const nri::CoreInterface& NRI;
    const nri::DeviceDesc& m_DeviceDesc;
    nri::Device& m_Device;
    nri::CommandQueue& m_CommandQueue;
    nri::DeviceSemaphore* m_DeviceSemaphore = nullptr;
    Vector<nri::CommandAllocator*> m_CommandAllocators;
    Vector<nri::CommandBuffer*> m_CommandBuffers;
    nri::Buffer* m_UploadBuffer = nullptr;
    nri::Memory* m_UploadBufferMemory = nullptr;
    uint8_t* m_MappedMemory = nullptr;
    uint64_t m_UploadBufferSize = 0;
    uint64_t m_UploadBufferOffset = 0;

    static constexpr uint64_t COPY_ALIGMENT = 16;
};