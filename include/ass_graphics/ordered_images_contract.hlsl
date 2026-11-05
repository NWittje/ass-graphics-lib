// Contract2: same trusted same-pixel ordered planes/records and resolve as
// contract1, plus two immutable uint image inputs. Caller controls coordinates,
// bounds and interpretation; Texture2D.Load(int3(texel,0)) performs no filtering.
#ifndef AG_ORDERED_RESOLVE
Texture2D<uint> ag_image0 : register(t1);
Texture2D<uint> ag_image1 : register(t2);
#endif
#include "ordered_contract.hlsl"
