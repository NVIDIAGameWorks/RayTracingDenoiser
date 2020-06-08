/*
Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

#include <cstddef>

#include "NRI.h"
#include "Extensions/NRIDeviceCreation.h"
#include "Extensions/NRISwapChain.h"
#include "Extensions/NRIWrapperD3D11.h"
#include "Extensions/NRIWrapperD3D12.h"
#include "Extensions/NRIWrapperVK.h"
#include "Extensions/NRIRayTracing.h"

#include <vector>
#include <unordered_map>
#include <string>
#include <array>
#include <atomic>
#include <type_traits>
#include <limits>
#include <cassert>
#include <cstring>

#if __linux__
    #include <alloca.h>
    #define _alloca alloca
#else
    #include <malloc.h>
#endif

#include "Lock.h"

constexpr uint32_t PHYSICAL_DEVICE_GROUP_MAX_SIZE = 4;
constexpr uint32_t COMMAND_QUEUE_TYPE_NUM = (uint32_t)nri::CommandQueueType::MAX_NUM;

void CheckAndSetDefaultAllocator(nri::MemoryAllocatorInterface& memoryAllocatorInterface);
void CheckAndSetDefaultCallbacks(nri::CallbackInterface& callbackInterface);

struct Log
{
    Log(nri::GraphicsAPI graphicsAPI, const nri::CallbackInterface& callbackInterface, void* callbackInterfaceUserArg);

    void ReportMessage(nri::Message message, const char* format, ...) const;

private:
    nri::GraphicsAPI m_GraphicsAPI;
    nri::CallbackInterface m_CallbackInterface;
    void* m_CallbackInterfaceUserArg;
};

template<typename T>
struct StdAllocator
{
    typedef T value_type;
    typedef size_t size_type;
    typedef ptrdiff_t difference_type;
    typedef std::true_type propagate_on_container_move_assignment;
    typedef std::false_type is_always_equal;

    StdAllocator(const nri::MemoryAllocatorInterface& memoryAllocatorInterface, void* userArg);

    template <class U>
    StdAllocator(const StdAllocator<U>& allocator);

    StdAllocator<T>& operator= (const StdAllocator<T>& allocator);

    T* allocate(size_t n) noexcept;
    void deallocate(T* memory, size_t) noexcept;

    const nri::MemoryAllocatorInterface* GetInterface() const;
    void* GetUserArg() const;

    template<typename U>
    using other = StdAllocator<U>;

private:
    const nri::MemoryAllocatorInterface* m_Interface = nullptr;
    void* m_UserArg = nullptr;
};

template<typename T>
StdAllocator<T>::StdAllocator(const nri::MemoryAllocatorInterface& memoryAllocatorInterface, void* userArg) :
    m_Interface(&memoryAllocatorInterface),
    m_UserArg(userArg)
{
}

template<typename T>
template <class U>
StdAllocator<T>::StdAllocator(const StdAllocator<U>& allocator) :
    m_Interface(allocator.GetInterface()),
    m_UserArg(allocator.GetUserArg())
{
}

template<typename T>
StdAllocator<T>& StdAllocator<T>::operator= (const StdAllocator<T>& allocator)
{
    m_Interface = allocator.GetInterface();
    m_UserArg = allocator.GetUserArg();
    return *this;
}

template<typename T>
T* StdAllocator<T>::allocate(size_t n) noexcept
{
    return (T*)m_Interface->Allocate(m_UserArg, n * sizeof(T), alignof(T));
}

template<typename T>
void StdAllocator<T>::deallocate(T* memory, size_t n) noexcept
{
    m_Interface->Free(m_UserArg, memory);
}

template<typename T>
const nri::MemoryAllocatorInterface* StdAllocator<T>::GetInterface() const
{
    return m_Interface;
}

template<typename T>
void* StdAllocator<T>::GetUserArg() const
{
    return m_UserArg;
}

template<typename T>
bool operator== (const StdAllocator<T>& left, const StdAllocator<T>& right)
{
    return left.GetInterface() == right.GetInterface() && left.GetUserArg() == right.GetUserArg();
}

template<typename T>
bool operator!= (const StdAllocator<T>& left, const StdAllocator<T>& right)
{
    return !operator==(left, right);
}

template<typename T>
using Vector = std::vector<T, StdAllocator<T>>;

template<typename U, typename T>
using UnorderedMap = std::unordered_map<U, T, std::hash<U>, std::equal_to<U>, StdAllocator<std::pair<U, T>>>;

using String = std::basic_string<char, std::char_traits<char>, StdAllocator<char>>;
using WString = std::basic_string<wchar_t, std::char_traits<wchar_t>, StdAllocator<wchar_t>>;

template<typename T>
inline T GetAlignedSize(const T& x, uint32_t alignment)
{
    return ((x + alignment - 1) / alignment) * alignment;
}

template<typename T>
inline T* Align(T* x, size_t alignment)
{
    return (T*)(((size_t)x + alignment - 1) / alignment * alignment);
}

template <typename T, uint32_t N>
constexpr uint32_t GetCountOf(T const (&)[N])
{
    return N;
}

template<typename T, typename U>
constexpr uint32_t GetOffsetOf(U T::*member)
{
    return (uint32_t)((const char*)&((T*)nullptr->*member) - (const char*)nullptr);
}

template<typename T, typename... Args>
constexpr void Construct(T* objects, size_t number, Args&&... args)
{
    for (size_t i = 0; i < number; i++)
        new (objects + i) T(std::forward<Args>(args)...);
}

template<typename T, typename... Args>
inline T* Allocate(StdAllocator<uint8_t>& allocator, Args&&... args)
{
    const auto& lowLevelAllocator = *allocator.GetInterface();
    const auto& lowLevelAllocatorArg = allocator.GetUserArg();
    T* object = (T*)lowLevelAllocator.Allocate(lowLevelAllocatorArg, sizeof(T), alignof(T));

    new (object) T(std::forward<Args>(args)...);
    return object;
}

template<typename T, typename... Args>
inline T* AllocateArray(StdAllocator<uint8_t>& allocator, size_t arraySize, Args&&... args)
{
    const auto& lowLevelAllocator = *allocator.GetInterface();
    const auto& lowLevelAllocatorArg = allocator.GetUserArg();
    T* array = (T*)lowLevelAllocator.Allocate(lowLevelAllocatorArg, arraySize * sizeof(T), alignof(T));

    for (size_t i = 0; i < arraySize; i++)
        new (array + i) T(std::forward<Args>(args)...);

    return array;
}

template<typename T>
inline void Deallocate(StdAllocator<uint8_t>& allocator, T* object)
{
    if (object == nullptr)
        return;

    object->~T();

    const auto& lowLevelAllocator = *allocator.GetInterface();
    const auto& lowLevelAllocatorArg = allocator.GetUserArg();
    lowLevelAllocator.Free(lowLevelAllocatorArg, object);
}

template<typename T>
inline void DeallocateArray(StdAllocator<uint8_t>& allocator, T* array, size_t arraySize)
{
    if (array == nullptr)
        return;

    for (size_t i = 0; i < arraySize; i++)
        (array + i)->~T();

    const auto& lowLevelAllocator = *allocator.GetInterface();
    const auto& lowLevelAllocatorArg = allocator.GetUserArg();
    lowLevelAllocator.Free(lowLevelAllocatorArg, array);
}

//==============================================================================================================================

constexpr size_t STACK_ALLOC_MAX_SIZE = 65536;

template<typename T>
constexpr size_t CountStackAllocationSize(size_t arraySize)
{
    return arraySize * sizeof(T) + alignof(T);
}

#define ALLOCATE_SCRATCH(device, T, arraySize) \
    (CountStackAllocationSize<T>(arraySize) <= STACK_ALLOC_MAX_SIZE) ? \
    Align<T>(((arraySize) ? (T*)_alloca(CountStackAllocationSize<T>(arraySize)) : nullptr), alignof(T)) : \
    AllocateArray<T>(device.GetStdAllocator(), arraySize);

#define FREE_SCRATCH(device, array, arraySize) \
    if (array != nullptr && CountStackAllocationSize<decltype(array[0])>(arraySize) > STACK_ALLOC_MAX_SIZE) \
        DeallocateArray(device.GetStdAllocator(), array, arraySize);

#define STACK_ALLOC(T, arraySize) \
    Align<T>(((arraySize) ? (T*)_alloca(CountStackAllocationSize<T>(arraySize)) : nullptr), alignof(T))

//==============================================================================================================================

#define RETURN_ON_BAD_HRESULT(log, hresult, format, ...) \
    if ( FAILED(hresult) ) \
    { \
        (log).ReportMessage(nri::Message::TYPE_ERROR, format, ##__VA_ARGS__); \
        return GetResultFromHRESULT(hresult); \
    }

#define RETURN_ON_FAILURE(log, condition, returnCode, format, ...) \
    if ( !(condition) ) \
    { \
        (log).ReportMessage(nri::Message::TYPE_ERROR, format, ##__VA_ARGS__); \
        return returnCode; \
    }

#define REPORT_INFO(log, format, ...) \
    (log).ReportMessage(nri::Message::TYPE_INFO, format, ##__VA_ARGS__)

#define REPORT_WARNING(log, format, ...) \
    (log).ReportMessage(nri::Message::TYPE_WARNING, format, ##__VA_ARGS__)

#define REPORT_ERROR(log, format, ...) \
    (log).ReportMessage(nri::Message::TYPE_ERROR, format, ##__VA_ARGS__)

#if _DEBUG
    #define CHECK(log, condition, format, ...) \
        if ( !(condition) ) \
            (log).ReportMessage(nri::Message::TYPE_ERROR, format, ##__VA_ARGS__);
#else
    #define CHECK(log, condition, format, ...) (void*)0;
#endif

constexpr void ReturnVoid() {}

//==============================================================================================================================

void ConvertCharToWchar(const char* in, wchar_t* out, size_t outLen);
void ConvertWcharToChar(const wchar_t* in, char* out, size_t outLen);
nri::Result GetResultFromHRESULT(long result);

inline nri::Vendor GetVendorFromID(uint32_t vendorID)
{
    switch (vendorID)
    {
    case 0x10DE: return nri::Vendor::NVIDIA;
    case 0x1002: return nri::Vendor::AMD;
    case 0x8086: return nri::Vendor::INTEL;
    }

    return nri::Vendor::UNKNOWN;
}

//==============================================================================================================================

// TODO: This code is Windows/D3D specific, so it's probably better to move it into a separate header
#if _WIN32

struct IUnknown;

// This struct acts as a smart pointer for IUnknown pointers making sure to call AddRef() and Release() as needed.
template<typename T>
struct ComPtr
{
    ComPtr(T* lComPtr = nullptr) : m_ComPtr(lComPtr)
    {
        static_assert(std::is_base_of<IUnknown, T>::value, "T needs to be IUnknown based");

        if (m_ComPtr)
            m_ComPtr->AddRef();
    }

    ComPtr(const ComPtr<T>& lComPtrObj)
    {
        static_assert(std::is_base_of<IUnknown, T>::value, "T needs to be IUnknown based");

        m_ComPtr = lComPtrObj.m_ComPtr;

        if (m_ComPtr)
            m_ComPtr->AddRef();
    }

    ComPtr(ComPtr<T>&& lComPtrObj)
    {
        m_ComPtr = lComPtrObj.m_ComPtr;
        lComPtrObj.m_ComPtr = nullptr;
    }

    T* operator=(T* lComPtr)
    {
        if (m_ComPtr)
            m_ComPtr->Release();

        m_ComPtr = lComPtr;

        if (m_ComPtr)
            m_ComPtr->AddRef();

        return m_ComPtr;
    }

    T* operator=(const ComPtr<T>& lComPtrObj)
    {
        if (m_ComPtr)
            m_ComPtr->Release();

        m_ComPtr = lComPtrObj.m_ComPtr;

        if (m_ComPtr)
            m_ComPtr->AddRef();

        return m_ComPtr;
    }

    ~ComPtr()
    {
        if (m_ComPtr)
        {
            m_ComPtr->Release();
            m_ComPtr = nullptr;
        }
    }

    operator T*() const
    {
        return m_ComPtr;
    }

    T* GetInterface() const
    {
        return m_ComPtr;
    }

    T& operator*() const
    {
        return *m_ComPtr;
    }

    T** operator&()
    {
        //The assert on operator& usually indicates a bug. Could be a potential memory leak.
        // If this really what is needed, however, use GetInterface() explicitly.
        assert(m_ComPtr == nullptr);
        return &m_ComPtr;
    }

    T* operator->() const
    {
        return m_ComPtr;
    }

    bool operator!() const
    {
        return (nullptr == m_ComPtr);
    }

    bool operator<(T* lComPtr) const
    {
        return m_ComPtr < lComPtr;
    }

    bool operator!=(T* lComPtr) const
    {
        return !operator==(lComPtr);
    }

    bool operator==(T* lComPtr) const
    {
        return m_ComPtr == lComPtr;
    }

protected:
    T * m_ComPtr;
};

constexpr nri::FormatSupportBits COMMON_SUPPORT =
    nri::FormatSupportBits::TEXTURE |
    nri::FormatSupportBits::STORAGE_TEXTURE |
    nri::FormatSupportBits::BUFFER |
    nri::FormatSupportBits::STORAGE_BUFFER |
    nri::FormatSupportBits::COLOR_ATTACHMENT |
    nri::FormatSupportBits::VERTEX_BUFFER;

constexpr nri::FormatSupportBits COMMON_SUPPORT_WITHOUT_VERTEX =
    nri::FormatSupportBits::TEXTURE |
    nri::FormatSupportBits::STORAGE_TEXTURE |
    nri::FormatSupportBits::BUFFER |
    nri::FormatSupportBits::STORAGE_BUFFER |
    nri::FormatSupportBits::COLOR_ATTACHMENT;

constexpr nri::FormatSupportBits D3D_FORMAT_SUPPORT_TABLE[] = {
    nri::FormatSupportBits::UNSUPPORTED, // UNKNOWN,

    COMMON_SUPPORT_WITHOUT_VERTEX, // R8_UNORM,
    COMMON_SUPPORT_WITHOUT_VERTEX, // R8_SNORM,
    COMMON_SUPPORT_WITHOUT_VERTEX, // R8_UINT,
    COMMON_SUPPORT_WITHOUT_VERTEX, // R8_SINT,

    COMMON_SUPPORT_WITHOUT_VERTEX, // RG8_UNORM,
    COMMON_SUPPORT_WITHOUT_VERTEX, // RG8_SNORM,
    COMMON_SUPPORT_WITHOUT_VERTEX, // RG8_UINT,
    COMMON_SUPPORT_WITHOUT_VERTEX, // RG8_SINT,

    COMMON_SUPPORT_WITHOUT_VERTEX, // BGRA8_UNORM,

    COMMON_SUPPORT_WITHOUT_VERTEX, // RGBA8_UNORM,
    COMMON_SUPPORT_WITHOUT_VERTEX, // RGBA8_SNORM,
    COMMON_SUPPORT_WITHOUT_VERTEX, // RGBA8_UINT,
    COMMON_SUPPORT_WITHOUT_VERTEX, // RGBA8_SINT,
    COMMON_SUPPORT_WITHOUT_VERTEX, // RGBA8_SRGB,

    COMMON_SUPPORT, // R16_UNORM,
    COMMON_SUPPORT, // R16_SNORM,
    COMMON_SUPPORT, // R16_UINT,
    COMMON_SUPPORT, // R16_SINT,
    COMMON_SUPPORT, // R16_SFLOAT,

    COMMON_SUPPORT, // RG16_UNORM,
    COMMON_SUPPORT, // RG16_SNORM,
    COMMON_SUPPORT, // RG16_UINT,
    COMMON_SUPPORT, // RG16_SINT,
    COMMON_SUPPORT, // RG16_SFLOAT,

    COMMON_SUPPORT, // RGBA16_UNORM,
    COMMON_SUPPORT, // RGBA16_SNORM,
    COMMON_SUPPORT, // RGBA16_UINT,
    COMMON_SUPPORT, // RGBA16_SINT,
    COMMON_SUPPORT, // RGBA16_SFLOAT,

    COMMON_SUPPORT, // R32_UINT,
    COMMON_SUPPORT, // R32_SINT,
    COMMON_SUPPORT, // R32_SFLOAT,

    COMMON_SUPPORT, // RG32_UINT,
    COMMON_SUPPORT, // RG32_SINT,
    COMMON_SUPPORT, // RG32_SFLOAT,

    COMMON_SUPPORT, // RGB32_UINT,
    COMMON_SUPPORT, // RGB32_SINT,
    COMMON_SUPPORT, // RGB32_SFLOAT,

    COMMON_SUPPORT, // RGBA32_UINT,
    COMMON_SUPPORT, // RGBA32_SINT,
    COMMON_SUPPORT, // RGBA32_SFLOAT,

    COMMON_SUPPORT_WITHOUT_VERTEX, // R10_G10_B10_A2_UNORM,
    COMMON_SUPPORT_WITHOUT_VERTEX, // R10_G10_B10_A2_UINT,
    COMMON_SUPPORT_WITHOUT_VERTEX, // R11_G11_B10_UFLOAT,
    COMMON_SUPPORT_WITHOUT_VERTEX, // R9_G9_B9_E5_UFLOAT,

    nri::FormatSupportBits::TEXTURE, // BC1_RGBA_UNORM,
    nri::FormatSupportBits::TEXTURE, // BC1_RGBA_SRGB,
    nri::FormatSupportBits::TEXTURE, // BC2_RGBA_UNORM,
    nri::FormatSupportBits::TEXTURE, // BC2_RGBA_SRGB,
    nri::FormatSupportBits::TEXTURE, // BC3_RGBA_UNORM,
    nri::FormatSupportBits::TEXTURE, // BC3_RGBA_SRGB,
    nri::FormatSupportBits::TEXTURE, // BC4_R_UNORM,
    nri::FormatSupportBits::TEXTURE, // BC4_R_SNORM,
    nri::FormatSupportBits::TEXTURE, // BC5_RG_UNORM,
    nri::FormatSupportBits::TEXTURE, // BC5_RG_SNORM,
    nri::FormatSupportBits::TEXTURE, // BC6H_RGB_UFLOAT,
    nri::FormatSupportBits::TEXTURE, // BC6H_RGB_SFLOAT,
    nri::FormatSupportBits::TEXTURE, // BC7_RGBA_UNORM,
    nri::FormatSupportBits::TEXTURE, // BC7_RGBA_SRGB,

    // DEPTH_STENCIL_ATTACHMENT views
    nri::FormatSupportBits::DEPTH_STENCIL_ATTACHMENT, // D16_UNORM,
    nri::FormatSupportBits::DEPTH_STENCIL_ATTACHMENT, // D24_UNORM_S8_UINT,
    nri::FormatSupportBits::DEPTH_STENCIL_ATTACHMENT, // D32_SFLOAT,
    nri::FormatSupportBits::DEPTH_STENCIL_ATTACHMENT, // D32_SFLOAT_S8_UINT_X24,

    // Depth-stencil specific SHADER_RESOURCE views
    nri::FormatSupportBits::TEXTURE, // R24_UNORM_X8,
    nri::FormatSupportBits::TEXTURE, // X24_R8_UINT,
    nri::FormatSupportBits::TEXTURE, // X32_R8_UINT_X24,
    nri::FormatSupportBits::TEXTURE, // R32_SFLOAT_X8_X24,

    // MAX_NUM
};

static_assert(GetCountOf(D3D_FORMAT_SUPPORT_TABLE) == (size_t)nri::Format::MAX_NUM, "some format is missing");

#endif

template<typename T>
nri::Result ValidateFunctionTable(const Log& log, const T& table)
{
    const void* const* const begin = (void**)&table;
    const void* const* const end = (void**)(&table + 1);
    for (const void* const* current = begin; current != end; current++)
    {
        if (*current == nullptr)
        {
            REPORT_ERROR(log, "Invalid function table: function#%u is NULL!", uint32_t(current - begin));
            return nri::Result::FAILURE;
        }
    }
    return nri::Result::SUCCESS;
}

uint32_t GetTexelBlockWidth(nri::Format format);
uint32_t GetTexelBlockSize(nri::Format format);
nri::Format GetFormat(uint32_t dxgiFormat);

constexpr uint32_t GetPhysicalDeviceGroupMask(uint32_t mask)
{
    return mask == nri::WHOLE_DEVICE_GROUP ? 0xff : mask;
}
