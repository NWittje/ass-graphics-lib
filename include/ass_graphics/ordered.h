#pragma once
#include "stencil.h"
#ifdef __cplusplus
extern "C"
{
#endif
    /* ABI1 additive, trusted native caller, UI owner thread. Explicit creation uses
       an actual FL11.1 device with ROV/typed R32_UINT load/store. desc.warp selects
       software explicitly; there is no automatic fallback. Exactly two planes. */
    typedef struct ag_ordered_caps
    {
        uint32_t size, version, device_known, feature_level;
        uint32_t adapter_known, luid_low;
        int32_t luid_high;
        uint32_t vendor, device, software, options_known;
        int32_t options_result;
        uint32_t rov_supported, format_known;
        int32_t format_result;
        uint32_t r32_load, r32_store, available;
        wchar_t adapter_name[128];
    } ag_ordered_caps;
    typedef struct ag_ordered_program_desc
    {
        uint32_t size, version, contract, trusted;
        const void *material;
        uint32_t material_bytes, reserved0;
        const void *resolve;
        uint32_t resolve_bytes, constant_bytes;
        const void *constants;
        uint32_t reserved[2];
    } ag_ordered_program_desc;
    enum ag_ordered_kind
    {
        AG_ORDERED_STATE = 1,
        AG_ORDERED_FILL = 2,
        AG_ORDERED_STROKE = 3
    };
    enum ag_ordered_flags
    {
        AG_ORDERED_SEPARATE_PARTS = 1
    };
    typedef struct ag_ordered_record
    {
        uint32_t value[4];
    } ag_ordered_record;
    typedef struct ag_ordered_command
    {
        uint32_t size, version, kind, context, path, record, reserved[2];
        ag_stencil_command state;
    } ag_ordered_command;
    typedef struct ag_ordered_snapshot
    {
        uint32_t size, version, command_count, resource_count;
        const ag_ordered_command *commands;
        const ag_replay_resource *resources;
        const ag_ordered_record *records;
        uint32_t record_count, initial[2], flags;
        uint64_t sequence;
        ag_handle program;
        float flattening_tolerance;
        uint32_t mesh_vertex_limit, reserved[2];
    } ag_ordered_snapshot;
    typedef struct ag_ordered_state
    {
        uint32_t size, version;
        uint64_t accepted, submitted, completed, device_epoch;
        uint64_t submissions, material_draws, mask_draws, resolves, resource_creations;
        uint64_t cpu_bytes, vertex_bytes, record_bytes;
        uint32_t pending, available, device_lost;
        int32_t failure;
    } ag_ordered_state;
    /* Material shader ps_5_0 contract1: SV_Position + nointerpolation MATERIAL0 uint;
       ROV Texture2D<uint> u0/u1, StructuredBuffer<uint4> t0, optional constants b1.
       [earlydepthstencil] is mandatory. Resolve ps_5_0: Texture2D<uint> t0/t1,
       SV_Position -> SV_Target0 float4, optional same constants b1. See the shipped
       ordered_contract.hlsl. Reflection checks bindings/profile/requirements, not
       untrusted-code safety. Caller guarantees bounded same-pixel operations.
       Code <=1MiB per shader, constants <=4096 bytes in16-byte units. Copied cold.

       Each FILL/STROKE is one logical part coverage, with ordered overlapping parts.
       Path union/widening precedes triangulation; MOVE never invents a join. Current
       clip and selected independent mask apply. STATE permits context/transform/
       clip/stroke and mask operations only. Opaque single-sample PC coverage; no AA,
       images, tile or other colour commands. Two masks start at zero; clears/writes
       are explicit ordered state. Integer planes always start at explicit initial[]
       on each complete recipe. Records (1..65536 uint4) and arrays copy on submit.

       One GPU candidate may be pending, plus one latest CPU recipe. Completion is
       polled without a CPU wait; last completed colour stays available. Replay
       reuses accepted descriptions without hashing/preparation. Sequences strictly
       increase; coalescing may skip sequences. GPU completion is not presentation.
       Bounds: stencil64MiB counted CPU overlap,1M vertices,12MiB packed vertices,
       1MiB records, existing texture_budget including two extra4-byte planes. Driver
       memory/allocator peaks are not payload bounds. Source geometry is fixed. */
    AG_API ag_result ag_ordered_display_create(ag_handle context, void *parent, ag_rect bounds,
                                               const ag_display_desc *desc, ag_handle *out,
                                               ag_ordered_caps *observed);
    AG_API ag_result ag_ordered_capabilities(ag_handle display, ag_ordered_caps *out);
    AG_API ag_result ag_ordered_program_create(ag_handle display, const ag_ordered_program_desc *desc,
                                               ag_handle *out);
    AG_API ag_result ag_ordered_submit(ag_handle display, const ag_ordered_snapshot *snapshot);
    AG_API ag_result ag_ordered_replay(ag_handle display, uint64_t sequence);
    AG_API ag_result ag_ordered_state_get(ag_handle display, ag_ordered_state *out);
#ifdef __cplusplus
}
#endif
