#pragma once

namespace Dx8to12 {
static constexpr int kNumBackBuffers = 2;

static constexpr int kMaxVertexStreams = 16;
static constexpr int kMaxTexStages = 8;
static constexpr int kMaxActiveLights = 8;

static constexpr int kMaxSamplerStates = 64;
static constexpr int kMaxNumSrvs = 1024 + 512;
static constexpr int kMaxNumRtvs = 32;

static constexpr int kDynamicRingBufferSize = 40 * 1024 * 1024;

static constexpr int kNumVsConstRegs = 96;
static constexpr int kNumPsConstRegs = 8;

// Helpful debug controls.

// Will implicitly disable Pso cache.
static constexpr bool kDisablePixelShaderCache = false;
static constexpr bool kDisablePsoCache = false;

// Does not bother keeping a CPU copy of managed resources. Frees up memory,
// helpful when trying to do a GPU capture.
static constexpr bool kDisableManagedResources = true;
}  // namespace Dx8to12
