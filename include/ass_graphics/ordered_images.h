#pragma once
#include "ordered.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum ag_ordered_image_format
    {
        AG_ORDERED_IMAGE_R32_UINT = 1
    };
    /* Additive ABI1; UI thread. One uint32 per texel, top row first, x right/y
       down. No implicit channel, alpha, transfer-function or colour conversion.
       On Windows x64 the bytes 11 22 33 44 load as the integer 0x44332211.
       width/height 1..16384; packed payload <=16MiB; declared source extent
       <=64MiB. stride >=width*4; bytes covers the last row's actual pixel bytes.
       Padding is discarded. Input is copied and GPU-qualified before return.
       Immutable GPU R32_UINT, one mip, point Load only; no decoder or sampler.
       Owner display/context must outlive native handles. Use ag_destroy/release.
       All handle- or recipe-retained images count against existing CPU/texture
       budgets; <=16 live images per display. Same-handle aliases count once. */
    typedef struct ag_ordered_image_desc
    {
        uint32_t size, version, width, height, format, stride;
        uint64_t bytes;
        const void *pixels;
        uint32_t reserved[2];
    } ag_ordered_image_desc;
    /* Contract2 adds material Texture2D<uint> t1/t2 (image[0]/image[1]). Resolve
       is unchanged. Null is allowed only for slots absent from shader reflection.
       Caller records select slots/coordinates and define bounds/filtering and how
       a loaded uint modifies the two integer planes. A rectangular path supplies
       one covered quad, interleaved normally with fills/strokes/masks/clips.
       Successful admission pins data through queued GPU consumers; handles may
       then be released. Failure retains the previous complete scene. Replay and
       scene changes retain GPU images; only device reset recreates/uploads them.
       No per-frame hashing/decoding/copy/upload. Use ordered_images_contract.hlsl;
       contract1 and ag_ordered_submit remain unchanged. */
    typedef struct ag_ordered_image_bindings
    {
        uint32_t size, version;
        ag_handle image[2];
        uint32_t reserved[2];
    } ag_ordered_image_bindings;
    typedef struct ag_ordered_image_state
    {
        uint32_t size, version, width, height, format, resident;
        uint64_t packed_bytes, uploads, resource_creations, generation;
    } ag_ordered_image_state;
    AG_API ag_result ag_ordered_image_create(ag_handle display, const ag_ordered_image_desc *desc,
                                             ag_handle *out);
    AG_API ag_result ag_ordered_image_state_get(ag_handle image, ag_ordered_image_state *out);
    AG_API ag_result ag_ordered_submit_images(ag_handle display, const ag_ordered_snapshot *snapshot,
                                              const ag_ordered_image_bindings *bindings);
#ifdef __cplusplus
}
#endif
