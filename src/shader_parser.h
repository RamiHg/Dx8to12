#pragma once

#include <d3dcommon.h>

#include "vertex_shader.h"

namespace Dx8to12 {

VertexShader ParseProgrammableVertexShader(const VertexShaderDeclaration& decl,
                                           const unsigned long* ptr);

PixelShader ParsePixelShader(const unsigned long* ptr);

// All because ID3DInclude doesn't have a virtual destructor..
struct ShaderIncluder : public ID3DInclude {
  virtual ~ShaderIncluder() = default;
  STDMETHOD(Open)
  (D3D_INCLUDE_TYPE IncludeType, LPCSTR pFileName, LPCVOID pParentData,
   LPCVOID* ppData, UINT* pBytes) PURE;
  STDMETHOD(Close)(LPCVOID pData) PURE;
};
std::unique_ptr<ShaderIncluder> CreateShaderIncluder();
}  // namespace Dx8to12