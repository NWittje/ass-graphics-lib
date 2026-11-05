#pragma once
#include "ass_graphics/ordered_images.h"
namespace ag
{
struct OrderedImageData
{
    uint32_t width{}, height{};
    std::vector<uint32_t> pixels;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> view;
    uint64_t uploads{}, creations{}, generation{};
    uint64_t bytes() const
    {
        return sizeof(*this) + pixels.capacity() * sizeof(uint32_t);
    }
    uint64_t texture_bytes() const
    {
        return uint64_t(width) * height * 4;
    }
    void reset()
    {
        view.Reset();
        texture.Reset();
        device.Reset();
    }
    void prepare(ID3D11Device *d, uint64_t &total_creations)
    {
        if (device.Get() == d && texture && view)
            return;
        // Publish only a complete texture/view pair. A partial failure can be
        // retried without treating its surviving member as a ready image.
        ComPtr<ID3D11Texture2D> next;
        ComPtr<ID3D11ShaderResourceView> srv;
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_R32_UINT;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{pixels.data(), width * 4, 0};
        check(d->CreateTexture2D(&desc, &data, &next), L"Ordered image texture failed");
        ++uploads;
        ++creations;
        ++total_creations;
        check(d->CreateShaderResourceView(next.Get(), nullptr, &srv), L"Ordered image view failed");
        ++creations;
        ++total_creations;
        texture = std::move(next);
        view = std::move(srv);
        device = d;
        ++generation;
    }
    static uint64_t qualify(const ag_ordered_image_desc &d)
    {
        require(d.size == sizeof(d) && d.version == 1 && d.format == AG_ORDERED_IMAGE_R32_UINT && d.width &&
                    d.width <= 16384 && d.height && d.height <= 16384 && d.pixels && !d.reserved[0] &&
                    !d.reserved[1],
                L"Invalid ordered image descriptor");
        const uint64_t row = uint64_t(d.width) * 4, packed = row * d.height;
        const uint64_t extent = uint64_t(d.stride) * (d.height - 1) + row;
        require(packed <= 16ull * 1024 * 1024 && d.stride >= row && extent <= d.bytes &&
                    d.bytes <= 64ull * 1024 * 1024 && uintptr_t(d.pixels) <= UINTPTR_MAX - extent,
                L"Invalid ordered image row/byte extent");
        return packed;
    }
};
struct OrderedImage final : Object
{
    std::shared_ptr<OrderedImageData> data;
    std::weak_ptr<int> owner;
    OrderedImage(Context *c, std::shared_ptr<OrderedImageData> p, const std::shared_ptr<int> &o)
        : Object(Kind::OrderedImage, c), data(std::move(p)), owner(o)
    {
    }
};
} // namespace ag
