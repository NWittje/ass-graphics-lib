#pragma once
#include "ass_graphics/coverage.h"
#include "coverage_vs.h"
#include <d2d1_2.h>

namespace ag {
struct CoverageAtlas {
    struct Vertex { float x,y; uint32_t record; float u,v; };
    struct Part {
        uint32_t operation{},page{},first{};
        D2D1_RECT_F bounds{},cell{},clip{};
        D2D1_MATRIX_3X2_F matrix{};
        ComPtr<ID2D1Geometry> inner,outer;
        bool aliased{},axis_clip{};
        float tolerance{};
    };
    struct Plan {
        std::vector<Part> parts;
        std::vector<Vertex> vertices;
        std::vector<uint32_t> operation_part;
        uint32_t extent{2048}, pages{};
        uint64_t revision{};
        uint64_t bytes() const {
            return parts.capacity()*sizeof(Part)+vertices.capacity()*sizeof(Vertex)+
                   operation_part.capacity()*sizeof(uint32_t);
        }
    };
    struct Page {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11ShaderResourceView> view;
        ComPtr<ID2D1Bitmap1> bitmap;
    };
    std::vector<Page> pages;
    ComPtr<ID2D1SolidColorBrush> brush;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID2D1DeviceContext1> realization_context;
    struct Realized {
        ComPtr<ID2D1Geometry> geometry;
        ComPtr<ID2D1GeometryRealization> realization;
        float tolerance{};
        uint64_t used{};
    };
    using RealizationCache=std::array<Realized,16384>;
    std::unique_ptr<RealizationCache> realized;
    uint64_t realization_clock{};
    uint32_t extent{};
    uint64_t revision{};
    ag_coverage_metrics metrics{sizeof(ag_coverage_metrics),1};
    uint64_t texture_bytes() const { return uint64_t(pages.size())*extent*extent*4; }
    uint64_t cpu_bytes() const { return (realized?sizeof(RealizationCache):0)+pages.capacity()*sizeof(Page); }
    void reset() { pages.clear(); brush.Reset(); vertex.Reset(); layout.Reset(); realization_context.Reset();
        if(realized)for(auto &entry:*realized)entry={};realization_clock=0;extent=0;revision=0; }
    void fill(ID2D1Geometry *geometry,float tolerance) {
        auto key=uint64_t(reinterpret_cast<uintptr_t>(geometry))>>4;key^=key>>17;key*=0x9e3779b97f4a7c15ull;
        auto first=size_t((key>>32)&4095)*4;auto *victim=&(*realized)[first];++realization_clock;
        for(size_t i=first;i<first+4;++i){auto &entry=(*realized)[i];
            if(entry.geometry.Get()==geometry&&entry.tolerance==tolerance){entry.used=realization_clock;
                ++metrics.geometry_cache_hits;
                if(entry.realization)realization_context->DrawGeometryRealization(entry.realization.Get(),brush.Get());
                else realization_context->FillGeometry(geometry,brush.Get());return;}
            if(entry.used<victim->used)victim=&entry;
        }
        const auto began=std::chrono::steady_clock::now();++metrics.geometry_cache_builds;
        D2D1_RECT_F bounds{};check(geometry->GetBounds(nullptr,&bounds),L"Vector realization bounds failed");
        ComPtr<ID2D1GeometryRealization> next;
        if(bounds.left>=-524287.f&&bounds.top>=-524287.f&&bounds.right<=524287.f&&bounds.bottom<=524287.f)
            check(realization_context->CreateFilledGeometryRealization(geometry,tolerance,&next),L"Vector realization failed");
        victim->geometry=geometry;victim->realization=std::move(next);victim->tolerance=tolerance;victim->used=realization_clock;
        metrics.geometry_preparation_ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count();
        if(victim->realization)realization_context->DrawGeometryRealization(victim->realization.Get(),brush.Get());
        else realization_context->FillGeometry(geometry,brush.Get());
    }
    static Plan build(Context *context, const StencilRenderer::Scene &scene,
                      const std::vector<uint32_t> &records, const std::vector<float> &outer,
                      uint64_t revision,bool binary_fills) {
        auto start=std::chrono::steady_clock::now();
        (void)start;
        Plan plan; plan.revision=revision;
        plan.operation_part.assign(scene.operations.size(),UINT32_MAX);
        plan.parts.reserve(scene.operations.size());
        for(uint32_t i=0;i<scene.operations.size();++i) {
            const auto &o=scene.operations[i];
            if(o.kind!=AG_STENCIL_REPLAY) continue;
            Part part; part.operation=i; part.matrix=o.gpu; part.inner=o.mesh->geometry;
            part.tolerance=o.mesh->tolerance;
            part.axis_clip=o.state.clip&&o.state.clip->axis_rectangle;
            if(part.axis_clip)part.clip=o.state.clip->device_rect;
            part.aliased=binary_fills&&o.comparison==AG_REPLAY_FILL;
            require(part.inner!=nullptr,L"Vector path geometry is unavailable");
            if(outer[i]>0) {
                require(o.comparison==AG_REPLAY_DRAW && o.state.stroke &&
                        outer[i]>=o.state.stroke->width,L"Invalid vector outer width");
                if(o.mesh->coverage_outer&&o.mesh->coverage_outer_width==outer[i])part.outer=o.mesh->coverage_outer;
                else {
                const bool local=!o.state.clip||o.state.clip->axis_rectangle;
                const auto transform=o.state.matrix*scene.root;
                ComPtr<ID2D1PathGeometry> path; ComPtr<ID2D1GeometrySink> sink;
                check(context->d2d->CreatePathGeometry(&path),L"Vector outer path failed");
                check(path->Open(&sink),L"Vector outer sink failed");
                check(o.path->path->Widen(outer[i],o.state.stroke->stroke.Get(),
                        local?nullptr:&transform,part.tolerance,sink.Get()),L"Vector outer stroke failed");
                check(sink->Close(),L"Vector outer completion failed");
                part.outer=path;
                if(!local) {
                    ComPtr<ID2D1TransformedGeometry> clip;
                    check(context->d2d->CreateTransformedGeometry(o.state.clip->geometry.Get(),scene.root,&clip),L"Vector outer clip failed");
                    ComPtr<ID2D1PathGeometry> clipped; sink.Reset();
                    check(context->d2d->CreatePathGeometry(&clipped),L"Vector outer intersection failed");
                    check(clipped->Open(&sink),L"Vector outer intersection sink failed");
                    check(path->CombineWithGeometry(clip.Get(),D2D1_COMBINE_MODE_INTERSECT,nullptr,
                          scene.tolerance,sink.Get()),L"Vector outer intersection failed");
                    check(sink->Close(),L"Vector outer intersection completion failed");
                    part.outer=clipped;
                }
                o.mesh->coverage_outer=part.outer;o.mesh->coverage_outer_width=outer[i];
                }
            }
            D2D1_RECT_F bounds{};
            check((part.outer?part.outer:part.inner)->GetBounds(part.matrix,&bounds),L"Vector bounds failed");
            // Direct2D can return inverted infinite bounds for an empty clipped
            // geometry. It contributes no pixels, rather than invalidating a frame.
            if(bounds.right<=bounds.left||bounds.bottom<=bounds.top) continue;
            if(!finite(bounds.left)||!finite(bounds.top)||!finite(bounds.right)||!finite(bounds.bottom))
                require(false,L"Nonfinite vector bounds");
            bounds={std::max(0.f,std::floor(bounds.left)-1),std::max(0.f,std::floor(bounds.top)-1),
                    std::min(float(scene.w),std::ceil(bounds.right)+1),std::min(float(scene.h),std::ceil(bounds.bottom)+1)};
            if(part.axis_clip) {
                const auto r=part.clip;
                bounds={std::max(bounds.left,std::floor(r.left)),std::max(bounds.top,std::floor(r.top)),
                        std::min(bounds.right,std::ceil(r.right)),std::min(bounds.bottom,std::ceil(r.bottom))};
            }
            if(bounds.right<=bounds.left||bounds.bottom<=bounds.top) continue;
            part.bounds=bounds;
            const auto longest=uint32_t(std::max(bounds.right-bounds.left,bounds.bottom-bounds.top))+2;
            while(plan.extent<longest && plan.extent<16384)plan.extent*=2;
            require(longest<=plan.extent,L"Vector path exceeds coverage page");
            plan.operation_part[i]=uint32_t(plan.parts.size());
            plan.parts.push_back(std::move(part));
        }
        uint32_t x=0,y=0,row=0,page=0;
        plan.vertices.reserve(plan.parts.size()*6);
        for(auto &p:plan.parts) {
            auto w=uint32_t(p.bounds.right-p.bounds.left),h=uint32_t(p.bounds.bottom-p.bounds.top);
            if(x+w+2>plan.extent){x=0;y+=row;row=0;}
            if(y+h+2>plan.extent){++page;x=y=row=0;}
            p.page=page; p.cell={float(x+1),float(y+1),float(x+1+w),float(y+1+h)};
            p.first=uint32_t(plan.vertices.size());
            const auto r=records[p.operation];
            Vertex a{p.bounds.left,p.bounds.top,r,p.cell.left,p.cell.top};
            Vertex b{p.bounds.right,p.bounds.top,r,p.cell.right,p.cell.top};
            Vertex c{p.bounds.right,p.bounds.bottom,r,p.cell.right,p.cell.bottom};
            Vertex d{p.bounds.left,p.bounds.bottom,r,p.cell.left,p.cell.bottom};
            for(auto v:{a,b,c,a,c,d})plan.vertices.push_back(v);
            x+=w+2;row=std::max(row,h+2);
        }
        plan.pages=plan.parts.empty()?0:page+1;
        return plan;
    }
    void prepare(ID3D11Device *device,ID2D1DeviceContext *drawing,const Plan &plan,uint64_t budget) {
        const uint64_t needed=uint64_t(plan.pages)*plan.extent*plan.extent*4;
        require(needed<=budget,L"Vector coverage texture budget exceeded",E_OUTOFMEMORY);
        if(extent!=plan.extent || pages.size()<plan.pages) {
            const bool grow=extent==plan.extent;
            require(needed+(grow?0:texture_bytes())<=budget,L"Vector coverage replacement budget exceeded",E_OUTOFMEMORY);
            // progress() reaches preparation only after the preceding GPU lane
            // completes. Same-size pages can remain allocated during growth.
            std::vector<Page> next(plan.pages-(grow?pages.size():0));
            for(auto &p:next) {
                D3D11_TEXTURE2D_DESC t{};t.Width=t.Height=plan.extent;
                t.MipLevels=t.ArraySize=t.SampleDesc.Count=1;t.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
                t.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
                check(device->CreateTexture2D(&t,nullptr,&p.texture),L"Vector coverage texture failed");
                check(device->CreateShaderResourceView(p.texture.Get(),nullptr,&p.view),L"Vector coverage view failed");
                ComPtr<IDXGISurface> surface;check(p.texture.As(&surface),L"Vector coverage surface failed");
                auto props=D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
                    D2D1::PixelFormat(t.Format,D2D1_ALPHA_MODE_PREMULTIPLIED),96,96);
                check(drawing->CreateBitmapFromDxgiSurface(surface.Get(),&props,&p.bitmap),L"Vector coverage bitmap failed");
            }
            if(grow){pages.reserve(plan.pages);for(auto &p:next)pages.push_back(std::move(p));}
            else pages=std::move(next);
            extent=plan.extent;revision=0;
        }
        require(texture_bytes()<=budget,L"Retained vector coverage budget exceeded",E_OUTOFMEMORY);
        if(!brush)check(drawing->CreateSolidColorBrush(D2D1::ColorF(0,1,0,1),&brush),L"Vector coverage brush failed");
        if(!realization_context)check(drawing->QueryInterface(IID_PPV_ARGS(&realization_context)),L"Vector realization context unavailable");
        if(!vertex||!layout) {
            ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11InputLayout> il;
            check(device->CreateVertexShader(coverage_vs,sizeof(coverage_vs),nullptr,&vs),L"Vector vertex shader failed");
            D3D11_INPUT_ELEMENT_DESC e[]={
                {"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
                {"MATERIAL",0,DXGI_FORMAT_R32_UINT,0,8,D3D11_INPUT_PER_VERTEX_DATA,0},
                {"TEXCOORD",0,DXGI_FORMAT_R32G32_FLOAT,0,12,D3D11_INPUT_PER_VERTEX_DATA,0}};
            check(device->CreateInputLayout(e,3,coverage_vs,sizeof(coverage_vs),&il),L"Vector input layout failed");
            vertex=vs;layout=il;
        }
        metrics.paths=plan.parts.size();metrics.atlas_pages=plan.pages;metrics.atlas_bytes=texture_bytes();
        if(revision==plan.revision)return;
        const auto started=std::chrono::steady_clock::now();
        metrics.geometry_cache_hits=metrics.geometry_cache_builds=0;metrics.geometry_preparation_ms=0;
        for(uint32_t page=0;page<plan.pages;++page) {
            drawing->SetTarget(pages[page].bitmap.Get());drawing->SetDpi(96,96);
            drawing->SetTransform(D2D1::Matrix3x2F::Identity());
            drawing->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            drawing->BeginDraw();drawing->Clear(D2D1::ColorF(0,0,0,0));
            for(const auto &p:plan.parts)if(p.page==page) {
                drawing->SetAntialiasMode(p.aliased?D2D1_ANTIALIAS_MODE_ALIASED:D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                drawing->SetTransform(D2D1::Matrix3x2F::Identity());
                drawing->PushAxisAlignedClip(p.cell,D2D1_ANTIALIAS_MODE_ALIASED);
                if(p.axis_clip){const auto dx=p.cell.left-p.bounds.left,dy=p.cell.top-p.bounds.top;
                    drawing->PushAxisAlignedClip({p.clip.left+dx,p.clip.top+dy,p.clip.right+dx,p.clip.bottom+dy},
                        p.aliased?D2D1_ANTIALIAS_MODE_ALIASED:D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);}
                drawing->SetTransform(p.matrix*D2D1::Matrix3x2F::Translation(p.cell.left-p.bounds.left,p.cell.top-p.bounds.top));
                if(p.outer){brush->SetColor(D2D1::ColorF(1,0,0,1));fill(p.outer.Get(),p.tolerance);}
                brush->SetColor(D2D1::ColorF(0,1,0,1));fill(p.inner.Get(),p.tolerance);
                if(p.axis_clip)drawing->PopAxisAlignedClip();
                drawing->PopAxisAlignedClip();
            }
            const auto result=drawing->EndDraw();drawing->SetTarget(nullptr);
            check(result,L"Vector coverage rasterization failed");
        }
        revision=plan.revision;++metrics.rasterizations;
        metrics.raster_cpu_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    }
};
}
