#include "internal.hpp"
#include "blur_ps.h"
#include "quad_vs.h"
#include "scene_ps.h"
#include "scene_vs.h"
#include <DirectXMath.h>
#include <d3d11.h>
#include <dxgi1_2.h>
namespace ag
{
using namespace DirectX;
struct Scene final : Surface
{
    bool warp{}, wireframe{}, mesh_dirty{true}, overlay_dirty{true};
    float blur{};
    ag_camera camera{{0, 1.5f, -4}, {0, 0, 0}, {0, 1, 0}, .85f, .01f, 1000};
    std::vector<ag_vertex> vertices;
    std::vector<uint32_t> indices;
    Snapshot overlay;
    // Scratch storage makes multi-array updates transactional while reusing
    // capacity between telemetry frames.
    std::vector<ag_vertex> pending_vertices;
    std::vector<uint32_t> pending_indices;
    Snapshot pending_overlay;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> immediate;
    ComPtr<IDXGISwapChain1> swap;
    ComPtr<ID3D11RenderTargetView> back_view, scene_view;
    ComPtr<ID3D11ShaderResourceView> scene_srv;
    ComPtr<ID3D11DepthStencilView> depth;
    ComPtr<ID3D11Buffer> vertex_buffer, index_buffer, constants;
    ComPtr<ID3D11VertexShader> vertex_shader, quad_shader;
    ComPtr<ID3D11PixelShader> pixel_shader, blur_shader;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11RasterizerState> solid, wire;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11BlendState> blend;
    ComPtr<ID2D1Device> d2d_device;
    ComPtr<ID2D1DeviceContext> d2d_context;
    ComPtr<ID2D1Bitmap1> d2d_target;
    Painter painter;
    UINT width{}, height{};
    std::wstring capture_path;
    Scene(Context *c, bool w) : Surface(Kind::Scene, c), warp(w), painter(c) {}
    void targets()
    {
        ComPtr<ID3D11Texture2D> back;
        check(swap->GetBuffer(0, IID_PPV_ARGS(&back)), L"Swap buffer failed");
        check(device->CreateRenderTargetView(back.Get(), nullptr, &back_view), L"Back target failed");
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> texture;
        check(device->CreateTexture2D(&desc, nullptr, &texture), L"Scene texture failed");
        check(device->CreateRenderTargetView(texture.Get(), nullptr, &scene_view), L"Scene target failed");
        check(device->CreateShaderResourceView(texture.Get(), nullptr, &scene_srv),
              L"Scene sampling view failed");
        texture.Reset();
        desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        check(device->CreateTexture2D(&desc, nullptr, &texture), L"Depth texture failed");
        check(device->CreateDepthStencilView(texture.Get(), nullptr, &depth), L"Depth view failed");
        ComPtr<IDXGISurface> surface;
        check(back.As(&surface), L"DXGI surface failed");
        auto props =
            D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
                                    float(dpi(hwnd)), float(dpi(hwnd)));
        check(d2d_context->CreateBitmapFromDxgiSurface(surface.Get(), &props, &d2d_target),
              L"Direct2D shared target failed");
        d2d_context->SetTarget(d2d_target.Get());
        d2d_context->SetDpi(float(dpi(hwnd)), float(dpi(hwnd)));
        overlay_dirty = true;
    }
    void initialize()
    {
        RECT r{};
        GetClientRect(hwnd, &r);
        width = std::max(1L, r.right);
        height = std::max(1L, r.bottom);
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        D3D_FEATURE_LEVEL level{}, requested = D3D_FEATURE_LEVEL_11_0;
        auto create = [&]
        {
            return D3D11CreateDevice(nullptr, warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                     flags, &requested, 1, D3D11_SDK_VERSION, &device, &level, &immediate);
        };
        HRESULT hr = create();
        if (hr == DXGI_ERROR_SDK_COMPONENT_MISSING)
        {
            flags &= ~D3D11_CREATE_DEVICE_DEBUG;
            hr = create();
        }
        check(hr, L"D3D11 feature level 11 device failed");
        ComPtr<IDXGIDevice> dxgi;
        check(device.As(&dxgi), L"DXGI device failed");
        ComPtr<IDXGIAdapter> adapter;
        check(dxgi->GetAdapter(&adapter), L"Adapter failed");
        ComPtr<IDXGIFactory2> factory;
        check(adapter->GetParent(IID_PPV_ARGS(&factory)), L"DXGI factory failed");
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = width;
        desc.Height = height;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        check(factory->CreateSwapChainForHwnd(device.Get(), hwnd, &desc, nullptr, nullptr, &swap),
              L"Swap chain failed");
        factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
        check(context->d2d->CreateDevice(dxgi.Get(), &d2d_device), L"Direct2D shared device failed");
        check(d2d_device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d2d_context),
              L"Direct2D device context failed");
        targets();
        check(device->CreateVertexShader(scene_vs, sizeof(scene_vs), nullptr, &vertex_shader),
              L"Vertex shader failed");
        check(device->CreatePixelShader(scene_ps, sizeof(scene_ps), nullptr, &pixel_shader),
              L"Pixel shader failed");
        check(device->CreateVertexShader(quad_vs, sizeof(quad_vs), nullptr, &quad_shader),
              L"Quad shader failed");
        check(device->CreatePixelShader(blur_ps, sizeof(blur_ps), nullptr, &blur_shader),
              L"Blur shader failed");
        D3D11_INPUT_ELEMENT_DESC input[] = {{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
                                             offsetof(ag_vertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0},
                                            {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
                                             offsetof(ag_vertex, normal), D3D11_INPUT_PER_VERTEX_DATA, 0},
                                            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
                                             offsetof(ag_vertex, color), D3D11_INPUT_PER_VERTEX_DATA, 0}};
        check(device->CreateInputLayout(input, 3, scene_vs, sizeof(scene_vs), &layout),
              L"Vertex layout failed");
        D3D11_BUFFER_DESC cb{};
        cb.ByteWidth = 80;
        cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        check(device->CreateBuffer(&cb, nullptr, &constants), L"Constant buffer failed");
        D3D11_RASTERIZER_DESC raster{};
        raster.FillMode = D3D11_FILL_SOLID;
        raster.CullMode = D3D11_CULL_NONE;
        raster.DepthClipEnable = TRUE;
        check(device->CreateRasterizerState(&raster, &solid), L"Rasterizer failed");
        raster.FillMode = D3D11_FILL_WIREFRAME;
        check(device->CreateRasterizerState(&raster, &wire), L"Wire rasterizer failed");
        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        check(device->CreateSamplerState(&sd, &sampler), L"Sampler failed");
        D3D11_BLEND_DESC bd{};
        bd.RenderTarget[0].BlendEnable = TRUE;
        bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        check(device->CreateBlendState(&bd, &blend), L"Blend state failed");
        mesh_dirty = true;
    }
    void paint() override
    {
        try
        {
            paint_frame();
        }
        catch (...)
        {
            // A partially created device/target must never be treated as ready
            // on the next paint. CPU snapshots remain available for retry.
            reset();
            throw;
        }
    }
    void paint_frame()
    {
        {
            std::lock_guard lock(mutex);
            notified = false;
        }
        if (!device)
            initialize();
        if (mesh_dirty)
        {
            vertex_buffer.Reset();
            index_buffer.Reset();
            if (!vertices.empty() && !indices.empty())
            {
                D3D11_BUFFER_DESC desc{};
                desc.ByteWidth = UINT(vertices.size() * sizeof(ag_vertex));
                desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
                D3D11_SUBRESOURCE_DATA data{vertices.data(), 0, 0};
                check(device->CreateBuffer(&desc, &data, &vertex_buffer), L"Vertex buffer failed");
                desc.ByteWidth = UINT(indices.size() * 4);
                desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
                data.pSysMem = indices.data();
                check(device->CreateBuffer(&desc, &data, &index_buffer), L"Index buffer failed");
            }
            mesh_dirty = false;
        }
        auto c = context->theme.background;
        float clear[] = {c.r, c.g, c.b, c.a};
        immediate->ClearRenderTargetView(scene_view.Get(), clear);
        immediate->ClearDepthStencilView(depth.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1, 0);
        D3D11_VIEWPORT viewport{0, 0, float(width), float(height), 0, 1};
        immediate->RSSetViewports(1, &viewport);
        immediate->RSSetState(wireframe ? wire.Get() : solid.Get());
        immediate->OMSetRenderTargets(1, scene_view.GetAddressOf(), depth.Get());
        immediate->OMSetBlendState(blend.Get(), nullptr, 0xffffffff);
        auto load = [](ag_vec3 v) { return XMVectorSet(v.x, v.y, v.z, 0); };
        struct Constants
        {
            XMFLOAT4X4 matrix;
            XMFLOAT4 options;
        } cb{};
        XMStoreFloat4x4(&cb.matrix, XMMatrixLookAtLH(load(camera.eye), load(camera.target), load(camera.up)) *
                                        XMMatrixPerspectiveFovLH(camera.fov_y, float(width) / height,
                                                                 camera.near_z, camera.far_z));
        cb.options = {1.f / width, 1.f / height, blur, 0};
        immediate->UpdateSubresource(constants.Get(), 0, nullptr, &cb, 0, 0);
        immediate->VSSetConstantBuffers(0, 1, constants.GetAddressOf());
        immediate->PSSetConstantBuffers(0, 1, constants.GetAddressOf());
        if (vertex_buffer)
        {
            UINT stride = sizeof(ag_vertex), offset = 0;
            immediate->IASetVertexBuffers(0, 1, vertex_buffer.GetAddressOf(), &stride, &offset);
            immediate->IASetIndexBuffer(index_buffer.Get(), DXGI_FORMAT_R32_UINT, 0);
            immediate->IASetInputLayout(layout.Get());
            immediate->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            immediate->VSSetShader(vertex_shader.Get(), nullptr, 0);
            immediate->PSSetShader(pixel_shader.Get(), nullptr, 0);
            immediate->DrawIndexed(UINT(indices.size()), 0, 0);
        }
        immediate->OMSetRenderTargets(1, back_view.GetAddressOf(), nullptr);
        immediate->OMSetBlendState(nullptr, nullptr, 0xffffffff);
        immediate->RSSetState(solid.Get());
        immediate->IASetInputLayout(nullptr);
        immediate->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        immediate->VSSetShader(quad_shader.Get(), nullptr, 0);
        immediate->PSSetShader(blur_shader.Get(), nullptr, 0);
        immediate->PSSetSamplers(0, 1, sampler.GetAddressOf());
        immediate->PSSetShaderResources(0, 1, scene_srv.GetAddressOf());
        immediate->Draw(3, 0);
        ID3D11ShaderResourceView *none{};
        immediate->PSSetShaderResources(0, 1, &none);
        immediate->OMSetRenderTargets(0, nullptr, nullptr);
        if (overlay_dirty)
        {
            painter.prepare(d2d_context.Get(), overlay);
            overlay_dirty = false;
        }
        d2d_context->BeginDraw();
        painter.draw(d2d_context.Get(), overlay);
        HRESULT hr = d2d_context->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET)
        {
            reset();
            return;
        }
        check(hr, L"3D overlay failed");
        if (!capture_path.empty())
        {
            ComPtr<ID3D11Texture2D> back, staging;
            check(swap->GetBuffer(0, IID_PPV_ARGS(&back)), L"Capture buffer failed");
            D3D11_TEXTURE2D_DESC desc{};
            back->GetDesc(&desc);
            desc.Usage = D3D11_USAGE_STAGING;
            desc.BindFlags = 0;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            desc.MiscFlags = 0;
            check(device->CreateTexture2D(&desc, nullptr, &staging), L"Capture staging failed");
            immediate->CopyResource(staging.Get(), back.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};
            check(immediate->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), L"Capture map failed");
            try
            {
                save_pixels(context, capture_path.c_str(), width, height, mapped.RowPitch,
                            static_cast<const BYTE *>(mapped.pData), float(dpi(hwnd)));
            }
            catch (...)
            {
                immediate->Unmap(staging.Get(), 0);
                capture_path.clear();
                throw;
            }
            immediate->Unmap(staging.Get(), 0);
            capture_path.clear();
        }
        hr = swap->Present(1, 0);
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
        {
            reset();
            return;
        }
        check(hr, L"Present failed");
    }
    void resize() override
    {
        try
        {
            resize_targets();
        }
        catch (...)
        {
            reset();
            throw;
        }
    }
    void resize_targets()
    {
        if (!swap)
            return;
        RECT r{};
        GetClientRect(hwnd, &r);
        if (!r.right || !r.bottom)
            return;
        width = r.right;
        height = r.bottom;
        immediate->ClearState();
        painter.reset();
        d2d_context->SetTarget(nullptr);
        d2d_target.Reset();
        back_view.Reset();
        scene_view.Reset();
        scene_srv.Reset();
        depth.Reset();
        check(swap->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0), L"3D resize failed");
        targets();
    }
    void reset() override
    {
        painter.reset();
        if (d2d_context)
            d2d_context->SetTarget(nullptr);
        d2d_target.Reset();
        d2d_context.Reset();
        d2d_device.Reset();
        if (immediate)
            immediate->ClearState();
        back_view.Reset();
        scene_view.Reset();
        scene_srv.Reset();
        depth.Reset();
        vertex_buffer.Reset();
        index_buffer.Reset();
        constants.Reset();
        layout.Reset();
        vertex_shader.Reset();
        pixel_shader.Reset();
        quad_shader.Reset();
        blur_shader.Reset();
        solid.Reset();
        wire.Reset();
        sampler.Reset();
        blend.Reset();
        swap.Reset();
        immediate.Reset();
        device.Reset();
        mesh_dirty = overlay_dirty = true;
        ++metrics.device_recreations;
    }
};
} // namespace ag
using namespace ag;
ag_result ag_scene_create(ag_handle c, void *parent, ag_rect r, uint32_t warp, ag_handle *out)
{
    return api(
        [&]
        {
            output(out);
            auto ctx = get<Context>(c, Kind::Context);
            ctx->ui();
            auto p = std::make_unique<Scene>(ctx, warp != 0);
            p->create(static_cast<HWND>(parent), r);
            *out = p.release();
        });
}
ag_result ag_scene_mesh(ag_handle h, const ag_vertex *v, uint32_t n, const uint32_t *indices, uint32_t count)
{
    return api(
        [&]
        {
            auto p = get<Scene>(h, Kind::Scene);
            p->ui();
            require(n <= 1048576 && count <= 3145728 && count % 3 == 0 && (!n || v) && (!count || indices),
                    L"Invalid mesh size");
            for (uint32_t i = 0; i < n; ++i)
            {
                auto &a = v[i];
                require(finite(a.position.x) && finite(a.position.y) && finite(a.position.z) &&
                            finite(a.normal.x) && finite(a.normal.y) && finite(a.normal.z) &&
                            finite(a.color.r) && finite(a.color.g) && finite(a.color.b) && finite(a.color.a),
                        L"Nonfinite mesh vertex");
            }
            for (uint32_t i = 0; i < count; ++i)
                require(indices[i] < n, L"Mesh index out of range");
            if (n)
                p->pending_vertices.assign(v, v + n);
            else
                p->pending_vertices.clear();
            if (count)
                p->pending_indices.assign(indices, indices + count);
            else
                p->pending_indices.clear();
            p->vertices.swap(p->pending_vertices);
            p->indices.swap(p->pending_indices);
            p->mesh_dirty = true;
            std::lock_guard lock(p->mutex);
            p->schedule_locked();
        });
}
ag_result ag_scene_camera(ag_handle h, const ag_camera *c)
{
    return api(
        [&]
        {
            auto p = get<Scene>(h, Kind::Scene);
            p->ui();
            require(c, L"Null camera");
            auto v = [](ag_vec3 a) { return DirectX::XMVectorSet(a.x, a.y, a.z, 0); };
            for (auto a : {c->eye, c->target, c->up})
                require(finite(a.x) && finite(a.y) && finite(a.z), L"Nonfinite camera");
            auto direction = DirectX::XMVectorSubtract(v(c->target), v(c->eye));
            require(DirectX::XMVectorGetX(
                        DirectX::XMVector3LengthSq(DirectX::XMVector3Cross(direction, v(c->up)))) > 1e-10f &&
                        c->fov_y > 0 && c->fov_y < 3.14f && c->near_z > 0 && c->far_z > c->near_z &&
                        finite(c->far_z),
                    L"Degenerate camera");
            p->camera = *c;
            std::lock_guard lock(p->mutex);
            p->schedule_locked();
        });
}
ag_result ag_scene_options(ag_handle h, float blur, uint32_t wire)
{
    return api(
        [&]
        {
            auto p = get<Scene>(h, Kind::Scene);
            p->ui();
            require(finite(blur) && blur >= 0 && blur <= 64, L"Invalid blur radius");
            p->blur = blur;
            p->wireframe = wire != 0;
            std::lock_guard lock(p->mutex);
            p->schedule_locked();
        });
}
ag_result ag_scene_overlay(ag_handle h, const ag_draw *c, uint32_t n, const ag_point *points, uint32_t count)
{
    return api(
        [&]
        {
            auto p = get<Scene>(h, Kind::Scene);
            p->ui();
            validate_snapshot(c, n, points, count);
            if (n)
                p->pending_overlay.commands.assign(c, c + n);
            else
                p->pending_overlay.commands.clear();
            if (count)
                p->pending_overlay.points.assign(points, points + count);
            else
                p->pending_overlay.points.clear();
            std::swap(p->overlay, p->pending_overlay);
            p->overlay_dirty = true;
            std::lock_guard lock(p->mutex);
            p->schedule_locked();
        });
}
ag_result ag_scene_export_png(ag_handle h, const wchar_t *file)
{
    return api(
        [&]
        {
            auto p = get<Scene>(h, Kind::Scene);
            p->ui();
            require(file && *file, L"Missing capture filename");
            p->capture_path = file;
            try
            {
                p->paint();
                require(p->capture_path.empty(), L"Device reset interrupted capture", E_FAIL);
            }
            catch (...)
            {
                p->capture_path.clear();
                throw;
            }
        });
}
