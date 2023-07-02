#include "vertex_shader.h"

#include <d3d12.h>
#include <d3dcommon.h>
#include <d3dcompiler.h>
#include <dxgi.h>

#include <cmrc/cmrc.hpp>
#include <cstdint>
#include <iostream>
#include <map>
#include <sstream>
#include <vector>

#include "aixlog.hpp"
#include "d3d8types.h"
#include "device_limits.h"
#include "shader_parser.h"
#include "util.h"

CMRC_DECLARE(Dx8to12_shaders);

namespace Dx8to12 {

VertexShaderDeclaration VertexShaderDeclaration::CreateFromFVFDesc(DWORD fvf) {
  ASSERT(!(fvf & D3DFVF_PSIZE));
  ASSERT(!(fvf & D3DFVF_LASTBETA_UBYTE4));

  ASSERT(!((fvf & D3DFVF_XYZ) && (fvf & D3DFVF_XYZRHW)));
  // Legacy FVF format must follow a specific ordering of vertex attributes.
  std::vector<DWORD> decl;
  decl.push_back(D3DVSD_STREAM(0));
  const DWORD position = fvf & D3DFVF_POSITION_MASK;
  switch (position) {
    case D3DFVF_XYZ:
      decl.push_back(D3DVSD_REG(D3DVSDE_POSITION, D3DVSDT_FLOAT3));
      break;
    case D3DFVF_XYZRHW:
      decl.push_back(D3DVSD_REG(D3DVSDE_POSITION, D3DVSDT_FLOAT4));
      break;
    default:
      FAIL("Unsupported position type %d", position);
  }
  if (HasFlag(fvf, D3DFVF_NORMAL))
    decl.push_back(D3DVSD_REG(D3DVSDE_NORMAL, D3DVSDT_FLOAT3));
  if (HasFlag(fvf, D3DFVF_DIFFUSE))
    decl.push_back(D3DVSD_REG(D3DVSDE_DIFFUSE, D3DVSDT_D3DCOLOR));
  if (HasFlag(fvf, D3DFVF_SPECULAR))
    decl.push_back(D3DVSD_REG(D3DVSDE_SPECULAR, D3DVSDT_D3DCOLOR));
  for (unsigned i = 0;
       i < ((fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT); ++i) {
    decl.push_back(D3DVSD_REG(D3DVSDE_TEXCOORD0 + i, D3DVSDT_FLOAT2));
  }
  decl.push_back(D3DVSD_END());
  return ParseShaderDeclaration(decl.data());
}

VertexShaderDeclaration ParseShaderDeclaration(const DWORD* declaration) {
  VertexShaderDeclaration vertex_decl;

  while (*declaration != D3DVSD_END()) {
    // Skip optional NOPs.
    while (*declaration == 0) ++declaration;

    const DWORD token_type =
        (*declaration & D3DVSD_TOKENTYPEMASK) >> D3DVSD_TOKENTYPESHIFT;
    switch (token_type) {
      case D3DVSD_TOKEN_STREAM: {
        const bool is_tessellator_stream = *declaration & D3DVSD_STREAMTESSMASK;
        ASSERT(!is_tessellator_stream);
        const int stream_index = *declaration & D3DVSD_STREAMNUMBERMASK;
        ++declaration;
        // Skip optional NOPs.
        while (*declaration == 0) ++declaration;
        // Keep parsing Stream Data Definition tokens.
        int current_offset = 0;
        for (;; ++declaration) {
          const DWORD token = *declaration;

          D3D12_INPUT_ELEMENT_DESC element = {
              .SemanticName = "POSITION",
              .SemanticIndex = static_cast<UINT>(token & D3DVSD_VERTEXREGMASK),
              .InputSlot = static_cast<UINT>(stream_index),
              .AlignedByteOffset = static_cast<UINT>(current_offset),
              .InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA};

          if ((token & D3DVSD_TOKENTYPEMASK) ==
              D3DVSD_MAKETOKENTYPE(D3DVSD_TOKEN_STREAMDATA)) {
            ASSERT(element.SemanticIndex < 16);

            vertex_decl.has_inputs.at(element.SemanticIndex) = true;
            const bool is_data_skip = token & D3DVSD_DATALOADTYPEMASK;
            ASSERT(!is_data_skip);
            const DWORD data_type =
                (token & D3DVSD_DATATYPEMASK) >> D3DVSD_DATATYPESHIFT;
            switch (data_type) {
              case D3DVSDT_FLOAT1:
                element.Format = DXGI_FORMAT_R32_FLOAT;
                current_offset += sizeof(float);
                break;
              case D3DVSDT_FLOAT2:
                element.Format = DXGI_FORMAT_R32G32_FLOAT;
                current_offset += sizeof(float) * 2;
                break;
              case D3DVSDT_FLOAT3:
                element.Format = DXGI_FORMAT_R32G32B32_FLOAT;
                current_offset += sizeof(float) * 3;
                break;
              case D3DVSDT_FLOAT4:
                element.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
                current_offset += sizeof(float) * 4;
                break;
              case D3DVSDT_D3DCOLOR:
                element.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
                current_offset += sizeof(uint32_t);
                break;
              case D3DVSDT_UBYTE4:
                element.Format = DXGI_FORMAT_R8G8B8A8_UINT;
                current_offset += sizeof(uint32_t);
                break;
              case D3DVSDT_SHORT2:
                FAIL("TODO: Make sure ZW are set to 0,1 in D3DVSDT_SHORT2");
                // element.Format = DXGI_FORMAT_R16G16_SINT;
                // current_offset += sizeof(uint16_t) * 2;
                break;
              case D3DVSDT_SHORT4:
                element.Format = DXGI_FORMAT_R16G16B16A16_SINT;
                current_offset += sizeof(uint16_t) * 4;
                break;
            }
            vertex_decl.input_elements.push_back(element);
          } else if ((token & D3DVSD_TOKENTYPEMASK) ==
                     D3DVSD_MAKETOKENTYPE(D3DVSD_TOKEN_TESSELLATOR)) {
            FAIL("TODO: Support tesselator inputs.");
          } else {
            // We're done with this buffer. Store its stride.
            ASSERT(current_offset > 0);
            vertex_decl.buffer_strides[stream_index] = current_offset;
            break;
          }
        }
      } break;  // case D3DVSD_TOKEN_STREAM
      case D3DVSD_TOKEN_CONSTMEM: {
        const DWORD token = *declaration;
        const DWORD num_consts =
            (token & D3DVSD_CONSTCOUNTMASK) >> D3DVSD_CONSTCOUNTSHIFT;
        int const_reg = token & D3DVSD_CONSTADDRESSMASK;
        for (int i = 0; i < static_cast<int>(num_consts); ++i, ++const_reg) {
          ConstantRegData data = {*++declaration, *++declaration,
                                  *++declaration, *++declaration};
          vertex_decl.constant_reg_init[const_reg] = data;
        }
      } break;  // case D3DVSD_TOKEN_CONSTMEM
      default:
        FAIL("Unexpected vertex shader declaration token 0x%X.", *declaration);
    }
  }
  return vertex_decl;
}

std::ostream& operator<<(std::ostream& os,
                         const VertexShaderDeclaration& decl) {
  using ::std::endl;
  for (const auto& element : decl.input_elements) {
    os << "Element:" << endl;
    os << "\tSemantic: " << element.SemanticName << element.SemanticIndex
       << endl;
    os << "\tFormat: " << element.Format << endl;
    os << "\tVertex Buffer Slot: " << element.InputSlot << endl;
    os << "\tByte Offset: " << element.AlignedByteOffset << endl;
  }
  return os;
}

static bool SemanticHasFormat(const VertexShaderDeclaration& decl,
                              uint32_t semantic_index, DXGI_FORMAT format) {
  for (const auto& element : decl.input_elements) {
    if (element.SemanticIndex == semantic_index) {
      return format == DXGI_FORMAT_UNKNOWN || element.Format == format;
    }
  }
  return false;
}

VertexShader CreateFixedFunctionVertexShader(
    const D3D12_VIEWPORT& viewport, const DWORD fvf_desc,
    const VertexShaderDeclaration& declaration) {
  using ::std::endl;

  // TODO: Support multimatrix blending.
  ASSERT(!HasFlag(fvf_desc, D3DFVF_XYZB1) && !HasFlag(fvf_desc, D3DFVF_XYZB2) &&
         !HasFlag(fvf_desc, D3DFVF_XYZB3) && !HasFlag(fvf_desc, D3DFVF_XYZB4) &&
         !HasFlag(fvf_desc, D3DFVF_XYZB5));

  VertexShader result = {};

  if (HasFlag(fvf_desc, D3DFVF_XYZ) && HasFlag(fvf_desc, D3DFVF_XYZRHW)) {
    LOG(ERROR) << "Can't use untransformed and transformed vertices at the "
                  "same time.";
    return result;
  }

  const bool is_untransformed = SemanticHasFormat(declaration, D3DVSDE_POSITION,
                                                  DXGI_FORMAT_R32G32B32_FLOAT);
  const bool is_lit =
      !is_untransformed || !SemanticHasFormat(declaration, D3DVSDE_NORMAL,
                                              DXGI_FORMAT_R32G32B32_FLOAT);
  LOG(TRACE) << "is_lit " << is_lit
             << " is_untransformed: " << is_untransformed;
  const bool has_diffuse =
      SemanticHasFormat(declaration, D3DVSDE_DIFFUSE, DXGI_FORMAT_UNKNOWN);
  const bool has_specular =
      SemanticHasFormat(declaration, D3DVSDE_SPECULAR, DXGI_FORMAT_UNKNOWN);
  const bool has_normal =
      SemanticHasFormat(declaration, D3DVSDE_NORMAL, DXGI_FORMAT_UNKNOWN);

  ASSERT(!(!is_untransformed && has_normal));

  if ((fvf_desc & D3DFVF_DIFFUSE) && !declaration.has_inputs[D3DVSDE_DIFFUSE]) {
    LOG(ERROR) << "FVF has diffuse, but no diffuse vertex input in stream.";
    return result;
  }
  if ((fvf_desc & D3DFVF_SPECULAR) &&
      !declaration.has_inputs[D3DVSDE_SPECULAR]) {
    LOG(ERROR) << "FVF has specular, but no specilar vertex input in stream.";
    return result;
  }

  std::vector<D3D_SHADER_MACRO> defines;
  defines.reserve(13);
  if (has_diffuse) defines.push_back({"HAS_DIFFUSE", "1"});
  if (has_specular) defines.push_back({"HAS_SPECULAR", "1"});
  if (has_normal) defines.push_back({"HAS_NORMAL", "1"});
  if (!is_untransformed) defines.push_back({"HAS_TRANSFORM", "1"});
  for (int i = 0; i < 8; ++i) {
    if (declaration.has_inputs.at(D3DVSDE_TEXCOORD0 + i)) {
      static const char* kNames[] = {"HAS_T0", "HAS_T1", "HAS_T2", "HAS_T3",
                                     "HAS_T4", "HAS_T5", "HAS_T6", "HAS_T7"};
      defines.push_back({kNames[i], "1"});
    }
  }
  defines.push_back({nullptr, nullptr});

  std::stringstream s;
  s << std::dec;

  // First, define our input vertex data.
  s << "struct VertexInput {" << endl;
  std::array<const char*, kMaxTexStages> texcoord_components;
  texcoord_components.fill(".xy");
  for (const D3D12_INPUT_ELEMENT_DESC& desc : declaration.input_elements) {
    s << "\t";
    if (desc.SemanticIndex >= D3DVSDE_TEXCOORD0)
      ASSERT(desc.Format == DXGI_FORMAT_R32G32_FLOAT);
    switch (desc.Format) {
      case DXGI_FORMAT_R32_FLOAT:
        s << "float";
        break;
      case DXGI_FORMAT_R32G32_FLOAT:
        s << "float2";
        break;
      case DXGI_FORMAT_R32G32B32_FLOAT:
        s << "float3";
        break;
      case DXGI_FORMAT_R32G32B32A32_FLOAT:
      case DXGI_FORMAT_B8G8R8A8_UNORM:
        s << "float4";
        break;
      default:
        FAIL("Unexpected input element format %d", desc.Format);
    }
    s << " input_reg" << desc.SemanticIndex;
    s << " : POSITION" << desc.SemanticIndex << ";" << endl;
  }
  s << "};" << endl << endl;
  // TODO: Put this in cbuffer? Not saving much computation cost. Also needs to
  // change whenever viewport changes!
  s << "static const float2 invView2 = {" << 2.f / viewport.Width << ", "
    << 2.f / viewport.Height << "};" << endl;
  s << "#include \"ff_vertex_shader.hlsl\"\n";

  const std::string code = s.str();
  auto includer = CreateShaderIncluder();

  ID3DBlob* errorBlob = nullptr;
  HRESULT hr = D3DCompile(code.c_str(), code.size(), nullptr, defines.data(),
                          includer.get(), "VSMain", "vs_5_0",
                          D3DCOMPILE_DEBUG | D3DCOMPILE_ENABLE_STRICTNESS |
                              D3DCOMPILE_WARNINGS_ARE_ERRORS,
                          0, result.blob.GetForInit(), &errorBlob);
  if (hr != S_OK) {
    ASSERT(errorBlob);
    ASSERT(reinterpret_cast<const char*>(
               errorBlob->GetBufferPointer())[errorBlob->GetBufferSize() - 1] ==
           0);
    LOG_ERROR() << "Error when compiling shader: "
                << (const char*)errorBlob->GetBufferPointer() << "\n";
    FAIL("Error when compiling shader:\r\n%s\r\n---\r\n%s", code.c_str(),
         (const char*)errorBlob->GetBufferPointer());
  }
  ASSERT(errorBlob == nullptr);
  LOG(TRACE) << "Successfully created shader.\n";

  // TODO: Pass declaration by value.
  result.decl = declaration;
  result.fvf_desc = fvf_desc;
  return result;
}
}  // namespace Dx8to12