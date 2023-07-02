#include <d3dcompiler.h>

#include <sstream>

#include "aixlog.hpp"
#include "device.h"
#include "render_state.h"
#include "utils/dx_utils.h"

namespace Dx8to12 {
using ::std::endl;

constexpr char kPixelHeader[] = R"(
#include "ps_common.hlsl"
)";

static void GenerateArgValue(int stage_index, const TextureStageState &ts,
                             DWORD color_arg, std::stringstream &ss) {
  ASSERT((color_arg & D3DTA_ALPHAREPLICATE) == 0);
  ASSERT(!HasFlag(ts.transform_flags, D3DTTFF_PROJECTED));
  ss << "(";
  if (HasFlag(color_arg, D3DTA_COMPLEMENT)) ss << "1.f - ";
  switch (color_arg & D3DTA_SELECTMASK) {
    case D3DTA_DIFFUSE:
      ss << "diffuse_color";
      break;
    case D3DTA_CURRENT:
      ss << "result_color";
      break;
    case D3DTA_TEXTURE:
      if (ts.texcoord_index < 8) {
        ss << "g_texture" << stage_index << ".Sample(g_sampler" << stage_index
           << ", IN.oT" << ts.texcoord_index << ".xy)";
      } else {
        uint32_t sampler_index = ts.texcoord_index & 0xFFFF;
        uint32_t automode = ts.texcoord_index & ~0xFFFF;
        if (ts.transform_flags == D3DTTFF_COUNT2) {
          ss << "g_texture";
        } else {
          ASSERT(ts.transform_flags == D3DTTFF_COUNT3);
          ss << "g_texCube";
        }
        ss << stage_index << ".Sample(g_sampler" << sampler_index << ", ";
        switch (automode) {
          case D3DTSS_TCI_CAMERASPACENORMAL:
            ss << "IN.oViewNormal";
            break;
          case D3DTSS_TCI_CAMERASPACEPOSITION:
            ss << "IN.oViewPos";
            break;
          case D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR:
            ss << "IN.oViewReflect";
            break;
          default:
            FAIL("Unexpected auto-generated tex coord mode 0x%X", automode);
        }
        if (ts.transform_flags == D3DTTFF_COUNT2) {
          ss << ".xy";
        }
        ss << ")";
      }
      break;
    case D3DTA_TFACTOR:
      ss << "texture_factor";
      break;
    case D3DTA_SPECULAR:
      ss << "specular_color";
      break;
    default:
      FAIL("Unsupported texture stage arg 0x%X", color_arg);
  }
  ss << ")";
}

static void ApplyOperation(const PixelShaderState &s, const char *components,
                           int stage, D3DTEXTUREOP op, DWORD arg1_source,
                           DWORD arg2_source, std::stringstream &ss) {
  ss << "{" << endl;
  ss << "arg1 = ";
  GenerateArgValue(stage, s.ts[stage], arg1_source, ss);
  ss << ";\n";
  ss << "arg2 = ";
  GenerateArgValue(stage, s.ts[stage], arg2_source, ss);
  ss << ";" << endl;
  // Prepare any temporary arguments.
  switch (op) {
    case D3DTOP_BLENDTEXTUREALPHA:
      ASSERT(s.stage_has_texture(stage));
      ss << "alpha = ";
      GenerateArgValue(stage, s.ts[stage], D3DTA_TEXTURE, ss);
      ss << ".a;" << endl;
      break;
    case D3DTOP_BLENDFACTORALPHA:
      ss << "alpha = texture_factor.a;" << endl;
      break;
    case D3DTOP_BLENDCURRENTALPHA:
      ss << "alpha = result_color.a;\n";
      break;
    default:
      break;
  }
  ss << "result_color." << components << " = (";
  switch (op) {
    case D3DTOP_SELECTARG1:
      ss << "arg1";
      break;
    case D3DTOP_SELECTARG2:
      ss << "arg2";
      break;
    case D3DTOP_MODULATE:
      ss << "arg1*arg2";
      break;
    case D3DTOP_MODULATE2X:
      ss << "arg1*arg2*2.f";
      break;
    case D3DTOP_MODULATE4X:
      ss << "arg1*arg2*4.f";
      break;
    case D3DTOP_ADD:
      ss << "arg1+arg2";
      break;
    case D3DTOP_ADDSIGNED:
      ss << "arg1 + arg2  - 0.5f";
      break;
    case D3DTOP_BLENDFACTORALPHA:
      ss << "arg1*alpha + arg2*(1.f-alpha)";
      break;
    case D3DTOP_BLENDTEXTUREALPHA:
    case D3DTOP_BLENDCURRENTALPHA:
      ss << "arg1 + arg2*(1.f-alpha)";
      break;
    case D3DTOP_DOTPRODUCT3:
      ss << "saturate(dot(arg1-0.5f, arg2-0.5f)).xxxx";
      break;
    default:
      FAIL("Unsupported texture op %d", op);
  }
  ss << ")." << components << ";\n}\n";
}

ComPtr<ID3DBlob> CreatePixelShaderFromState(const PixelShaderState &s) {
  using ::std::endl;
  std::stringstream ss;
  ss << std::dec;
  ss << kPixelHeader;
  ss << "float4 PSMain(FFVertexOutput IN) : SV_Target {\n";
  ss << "float4 diffuse_color = IN.oD0;" << endl;
  ss << "float4 specular_color = IN.oD1;" << endl;

  ss << "float4 result_color = diffuse_color;" << endl;
  ss << "float4 arg1, arg2;" << endl;
  ss << "float alpha;" << endl;

  for (int i = 0; i < kMaxTexStages; ++i) {
    if (s.ts[i].color_op == D3DTOP_DISABLE) {
      // Done!
      break;
    }
    // ASSERT(s.ts[i].texcoord_index < 8);
    ASSERT(!HasFlag(s.ts[i].transform_flags, D3DTTFF_PROJECTED));
    ApplyOperation(s, "xyz", i, s.ts[i].color_op, s.ts[i].color_arg1,
                   s.ts[i].color_arg2, ss);
    if (s.ts[i].alpha_op != D3DTOP_DISABLE) {
      ApplyOperation(s, "a", i, s.ts[i].alpha_op, s.ts[i].alpha_arg1,
                     s.ts[i].alpha_arg2, ss);
    }
  }
  if (s.alpha_func() != D3DCMP_ALWAYS) {
    ss << "if (!(result_color.a ";
    switch (s.alpha_func()) {
      case D3DCMP_NEVER:
        ss << "!= result_color.a";
        break;
      case D3DCMP_LESS:
        ss << "< alpha_ref";
        break;
      case D3DCMP_LESSEQUAL:
        ss << "<= alpha_ref";
        break;
      case D3DCMP_GREATER:
        ss << "> alpha_ref";
        break;
      default:
        FAIL("Unexpected alpha func %d", s.alpha_func());
    }
    ss << ")) discard;" << endl;
  }
  ss << "return result_color;" << endl << "}" << endl;

  const std::string code = ss.str();
  auto includer = CreateShaderIncluder();

  ComPtr<ID3DBlob> result_blob;
  ID3DBlob *errorBlob = nullptr;
  HRESULT hr = D3DCompile(code.c_str(), code.size(), "ff_pixel_shader", nullptr,
                          includer.get(), "PSMain", "ps_5_0",
                          D3DCOMPILE_DEBUG | D3DCOMPILE_ENABLE_STRICTNESS |
                              D3DCOMPILE_WARNINGS_ARE_ERRORS,
                          0, result_blob.GetForInit(), &errorBlob);
  if (hr != S_OK) {
    ASSERT(errorBlob);
    ASSERT(reinterpret_cast<const char *>(
               errorBlob->GetBufferPointer())[errorBlob->GetBufferSize() - 1] ==
           0);
    LOG_ERROR() << "Error when compiling shader:" << endl
                << code << endl
                << (const char *)errorBlob->GetBufferPointer() << "\n";
    FAIL("Error when compiling shader:\r\n%s\r\n---\r\n%s", code.c_str(),
         (const char *)errorBlob->GetBufferPointer());
  }
  ASSERT(errorBlob == nullptr);
  LOG(TRACE) << "Successfully created pixel shader.\n";
  return result_blob;
}

}  // namespace Dx8to12