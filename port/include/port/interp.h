// Frame interpolation tags (see port/src/vi.cpp).
//
// At 120 fps the port renders an extra frame between two game frames by
// blending each matrix halfway toward the same matrix in the previous frame.
// Matrices are matched through the tag active when they are loaded, so code
// that draws objects tags them: PortInterpScope scope(this);
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void PortInterpTag(const void* tag);
const void* PortInterpCurrentTag(void);

#ifdef __cplusplus
}

// Tags the matrices loaded during the scope, then restores the outer tag.
class PortInterpScope {
public:
    explicit PortInterpScope(const void* tag) : mOuter(PortInterpCurrentTag()) { PortInterpTag(tag); }
    ~PortInterpScope() { PortInterpTag(mOuter); }

    PortInterpScope(const PortInterpScope&) = delete;
    PortInterpScope& operator=(const PortInterpScope&) = delete;

private:
    const void* mOuter;
};
#endif
