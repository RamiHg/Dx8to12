#include "shader_parser.h"

#include <cmrc/cmrc.hpp>
#include <sstream>
// #include <d3dcommon.h>
#include <d3dcompiler.h>

#include "d3d8.h"
#include "util.h"
#include "vertex_shader.h"

CMRC_DECLARE(Dx8to12_shaders);

namespace Dx8to12 {
constexpr int kMaxNumTempRegs = 12;
constexpr int kMaxNumConstRegs = 96;

constexpr char kTempRegName[] = "temp_reg";
constexpr char kInputRegName[] = "input_reg";

struct DestParamToken {
  int reg_number;
  uint32_t write_mask;
  bool saturate;
};

struct SourceParamToken {
  uint32_t modification;
};

static DestParamToken ParseGenericDestinationParamToken(bool is_tex_dest,
                                                        const uint32_t token,
                                                        std::ostream& os) {
  const int reg_number = static_cast<int>(token & D3DSP_REGNUM_MASK);
  const uint32_t reg_type = token & D3DSP_REGTYPE_MASK;

  DestParamToken result;
  result.reg_number = reg_number;

  ASSERT((token & D3DVS_ADDRESSMODE_MASK) == D3DVS_ADDRMODE_ABSOLUTE);

  switch (reg_type) {
    case D3DSPR_TEMP:
      ASSERT(reg_number < kMaxNumTempRegs);
      os << kTempRegName << "[" << reg_number << "]";
      break;
    case D3DSPR_ADDR:  // Same as D3DSPR_TEXTURE
      if (is_tex_dest) {
        os << "t" << reg_number;
      } else {
        ASSERT(reg_number == 0);
        os << "addr_reg";
      }
      break;
    case D3DSPR_RASTOUT:
      switch (reg_number) {
        case D3DSRO_POSITION:
          os << "OUT.oPos";
          break;
        case D3DSRO_FOG:
          os << "OUT.oFog";
          break;
        case D3DSRO_POINT_SIZE:
          FAIL("TODO: Support point size in vertex shader.");
          break;
        default:
          FAIL("Unexpected rast output offset %d", reg_number);
          break;
      }
      break;
    case D3DSPR_ATTROUT:
      ASSERT(reg_number < 2);
      os << "OUT.oD" << reg_number;
      break;
    case D3DSPR_TEXCRDOUT:
      ASSERT(reg_number < 8);
      os << "OUT.oT" << reg_number;
      break;
    default:
      FAIL("Unexpected reg type for destination param %d", reg_type);
      break;
  }

  const uint32_t write_mask = token & D3DSP_WRITEMASK_ALL;
  result.write_mask = write_mask;
  if (write_mask) {
    os << ".";
    if (write_mask & D3DSP_WRITEMASK_0) os << "x";
    if (write_mask & D3DSP_WRITEMASK_1) os << "y";
    if (write_mask & D3DSP_WRITEMASK_2) os << "z";
    if (write_mask & D3DSP_WRITEMASK_3) os << "w";
  }

  result.saturate = (token & D3DSP_DSTMOD_MASK) >> D3DSP_DSTMOD_SHIFT;
  ASSERT(token & 0x80000000);
  return result;
}

static DestParamToken ParseDestinationParamToken(const uint32_t token,
                                                 std::ostream& os) {
  return ParseGenericDestinationParamToken(false, token, os);
}

static DestParamToken ParseTexDestParamToken(const uint32_t token,
                                             std::ostream& os) {
  return ParseGenericDestinationParamToken(true, token, os);
}

static SourceParamToken ParseSourceParamToken(const DWORD** ptr,
                                              uint32_t write_mask,
                                              std::ostream& os) {
  const uint32_t token = *(*ptr)++;
  const int reg_number = static_cast<int>(token & D3DSP_REGNUM_MASK);
  const uint32_t reg_type = token & D3DSP_REGTYPE_MASK;
  SourceParamToken result;

  bool is_relative_mode = HasFlag(token, D3DVS_ADDRMODE_RELATIVE);

#ifdef SUPPORT_SM_2_IN_THE_FUTURE
  // Shader model 2+ specifies the relative register in an extra dword.
  if (is_relative_mode) {
    ASSERT(reg_type == D3DSPR_CONST);
    const uint32_t rel_token = *(*ptr)++;
    ASSERT(HasFlag(rel_token, 1U << 31));
    const int rel_reg_number = static_cast<int>(rel_token & D3DSP_REGNUM_MASK);
    ASSERT(rel_reg_number == 0);
    ASSERT((rel_token & D3DSP_REGTYPE_MASK) == D3DSPR_ADDR);
  }
#endif

  os << "(";
  switch (reg_type) {
    case D3DSPR_TEMP:
      ASSERT(reg_number < kMaxNumTempRegs);
      os << kTempRegName << "[" << reg_number << "]";
      break;
    case D3DSPR_INPUT:
      ASSERT(reg_number < 16);
      os << "IN." << kInputRegName << reg_number;
      break;
    case D3DSPR_CONST:
      ASSERT(reg_number < kMaxNumConstRegs);
      if (is_relative_mode) {
        os << "c[addr_reg.x + " << reg_number << "]";
      } else {
        os << "c[" << reg_number << "]";
      }
      break;
    case D3DSPR_RASTOUT:
      os << "OUT.";
      switch (reg_number) {
        case D3DSRO_POSITION:
          os << "oPos";
          break;
        case D3DSRO_FOG:
          printf("TODO: Support fog output.\n");
          break;
        case D3DSRO_POINT_SIZE:
          FAIL("TODO: Support point size in vertex shader.");
          break;
        default:
          FAIL("Unexpected rast output offset %d", reg_number);
          break;
      }
      break;
    case D3DSPR_ATTROUT:
      ASSERT(reg_number < 2);
      os << "OUT.oD" << reg_number;
      break;
    case D3DSPR_TEXCRDOUT:
      ASSERT(reg_number < 8);
      os << "OUT.oT" << reg_number;
      break;
    case D3DSPR_ADDR:  // Same as D3DSPR_TEXTURE
      // Differentiate between tex and address based on relative bit?
      if (is_relative_mode) {
        FAIL("TODO: Use address reg as source?");
      } else {
        os << "t" << reg_number;
      }
      break;
    default:
      FAIL("Unexpected reg type for destination param %d", reg_type);
      break;
  }

  // Parse the swizzle.
  uint32_t swizzle = (token & D3DSP_SWIZZLE_MASK) >> D3DSP_SWIZZLE_SHIFT;
  os << ".";
  for (int i = 0; i < 4; ++i, swizzle >>= 2) {
    switch (swizzle & 0x3) {
      case 0:
        os << "x";
        break;
      case 1:
        os << "y";
        break;
      case 2:
        os << "z";
        break;
      case 3:
        os << "w";
        break;
    }
  }
  result.modification = token & D3DSP_SRCMOD_MASK;
  switch (result.modification) {
    case D3DSPSM_NONE:
      break;
    case D3DSPSM_NEG:
      os << "*-1";
      break;
    default:
      FAIL("Unexpected modification %d",
           result.modification >> D3DSP_SRCMOD_SHIFT);
  }
  os << ")";

  return result;
}

static void EmitWriteMask(const uint32_t write_mask, std::ostream& os) {
  // Match the destination write mask (if any).
  if (write_mask != D3DSP_WRITEMASK_ALL) {
    os << ".";
    if (write_mask & D3DSP_WRITEMASK_0) os << "x";
    if (write_mask & D3DSP_WRITEMASK_1) os << "y";
    if (write_mask & D3DSP_WRITEMASK_2) os << "z";
    if (write_mask & D3DSP_WRITEMASK_3) os << "w";
  }
}

static const char* GetUnaryOpStr(D3DSHADER_INSTRUCTION_OPCODE_TYPE opcode) {
  switch (opcode) {
    case D3DSIO_MOV:
      return "";
    case D3DSIO_RCP:
      return "rcp";
    case D3DSIO_RSQ:
      return "rsqrt";
    default:
      FAIL("Unexpected unary op %d", opcode);
  }
}

static const char* GetBinaryOpStr(D3DSHADER_INSTRUCTION_OPCODE_TYPE opcode) {
  switch (opcode) {
    case D3DSIO_ADD:
      return "+";
    case D3DSIO_SUB:
      return "-";
    case D3DSIO_MUL:
      return "*";
    case D3DSIO_SGE:
      return ">=";
    default:
      FAIL("Unexpected binary op %d", opcode);
  }
}

static const char* GetFuncStr(D3DSHADER_INSTRUCTION_OPCODE_TYPE opcode) {
  switch (opcode) {
    case D3DSIO_DP4:
      return "mydot4";
    case D3DSIO_MIN:
      return "min";
    case D3DSIO_MAX:
      return "max";
    default:
      FAIL("Unexpected func op %d", opcode);
  }
}

static void ParseShader(bool is_pixel_shader, const DWORD* ptr,
                        std::stringstream& code) {
  ASSERT(ptr != nullptr);
  // First token is always the version token.
  const int version_major = D3DSHADER_VERSION_MAJOR(*ptr);
  const int version_minor = D3DSHADER_VERSION_MINOR(*ptr);
  ASSERT(version_major == 1);
  if (is_pixel_shader) {
    ASSERT_TODO(version_minor <= 3, "Pixel shader 1_4.");
  }
  ++ptr;

  D3DSHADER_INSTRUCTION_OPCODE_TYPE opcode = D3DSIO_NOP;

  for (; opcode != D3DSIO_END;) {
    const DWORD token = *(ptr++);
    opcode = static_cast<D3DSHADER_INSTRUCTION_OPCODE_TYPE>(token &
                                                            D3DSI_OPCODE_MASK);
    DestParamToken dest = {};
    SourceParamToken src, lhs, rhs;
    switch (opcode) {
      case D3DSIO_NOP:
        break;
      case D3DSIO_MOV:
      case D3DSIO_RCP:
      case D3DSIO_RSQ:
        dest = ParseDestinationParamToken(*(ptr++), code);
        code << " = (" << GetUnaryOpStr(opcode) << "(";
        src = ParseSourceParamToken(&ptr, dest.write_mask, code);
        code << "))";
        break;
      case D3DSIO_ADD:
      case D3DSIO_SUB:
      case D3DSIO_MUL:
      case D3DSIO_SGE:
        dest = ParseDestinationParamToken(*(ptr++), code);
        code << " = (";
        lhs = ParseSourceParamToken(&ptr, dest.write_mask, code);
        code << GetBinaryOpStr(opcode);
        rhs = ParseSourceParamToken(&ptr, dest.write_mask, code);
        code << ")";
        break;
      case D3DSIO_MAD:
        dest = ParseDestinationParamToken(*(ptr++), code);
        code << " = (";
        ParseSourceParamToken(&ptr, dest.write_mask, code);
        code << " * ";
        ParseSourceParamToken(&ptr, dest.write_mask, code);
        code << " + ";
        ParseSourceParamToken(&ptr, dest.write_mask, code);
        code << ")";
        break;
      case D3DSIO_DP3:
        dest = ParseDestinationParamToken(*(ptr++), code);
        code << " = mydot3(";
        lhs = ParseSourceParamToken(&ptr, dest.write_mask, code);
        code << ".xyz, ";
        rhs = ParseSourceParamToken(&ptr, dest.write_mask, code);
        code << ".xyz)";
        break;
      case D3DSIO_DP4:
      case D3DSIO_MIN:
      case D3DSIO_MAX:
        dest = ParseDestinationParamToken(*(ptr++), code);
        code << " = " << GetFuncStr(opcode) << "(";
        lhs = ParseSourceParamToken(&ptr, dest.write_mask, code);
        code << ", ";
        rhs = ParseSourceParamToken(&ptr, dest.write_mask, code);
        code << ")";
        break;
        // ------------------
        // Pixel-shader-only instructions from now.
      case D3DSIO_LRP:
        dest = ParseDestinationParamToken(*(ptr++), code);
        code << " = mylerp(";
        src = ParseSourceParamToken(&ptr, dest.write_mask, code);
        code << ", ";
        lhs = ParseSourceParamToken(&ptr, dest.write_mask, code);
        code << ", ";
        rhs = ParseSourceParamToken(&ptr, dest.write_mask, code);
        code << ")";
        break;
      case D3DSIO_TEX:
        dest = ParseTexDestParamToken(*(ptr++), code);
        code << " = g_texture" << dest.reg_number << ".Sample(g_sampler"
             << dest.reg_number << ", IN.oT" << dest.reg_number << ".xy)";
        break;
      case D3DSIO_COMMENT: {
        int comment_num_dwords =
            (token & D3DSI_COMMENTSIZE_MASK) >> D3DSI_COMMENTSIZE_SHIFT;
        for (int i = 0; i < comment_num_dwords; ++i, ++ptr) {
          DWORD value = *ptr;
          for (int j = 0; j < 4; ++j) {
            DWORD letter = value & 0xFF;
            if (letter == 0) break;
            // code << (char)letter;
            value >>= 8;
          }
        }
        // code << "\n";
        break;
      }
      case D3DSIO_END:
        // Done!
        break;
      default: {
        std::string sofar = code.str();
        FAIL("TODO: Implement instruction %d. So far:\r\n%s", opcode,
             sofar.c_str());
        break;
      }
    }
    if (opcode != D3DSIO_END && opcode != D3DSIO_COMMENT) {
      // Emit the write mask after we've written the result expression.
      EmitWriteMask(dest.write_mask, code);
      code << ";\n";
    }
  }
}

VertexShader ParseProgrammableVertexShader(const VertexShaderDeclaration& decl,
                                           const unsigned long* ptr) {
  // First, define our input vertex data.
  std::stringstream s;
  s << std::dec;

  s << "struct VertexInput {\n";
  for (const D3D12_INPUT_ELEMENT_DESC& desc : decl.input_elements) {
    s << "\t";
    // switch (desc.Format) {
    //   case DXGI_FORMAT_R32_FLOAT:
    //     s << "float";
    //     break;
    //   case DXGI_FORMAT_R32G32_FLOAT:
    //     s << "float2";
    //     break;
    //   case DXGI_FORMAT_R32G32B32_FLOAT:
    //     s << "float3";
    //     break;
    //   case DXGI_FORMAT_R32G32B32A32_FLOAT:
    //   case DXGI_FORMAT_B8G8R8A8_UNORM:
    //     s << "float4";
    //     break;
    //   default:
    //     FAIL("Unexpected input element format %d", desc.Format);
    // }
    s << "float4";
    s << " input_reg" << desc.SemanticIndex;
    s << " : POSITION" << desc.SemanticIndex << ";\n";
  }
  s << "};\n\n";

  auto fs = cmrc::Dx8to12_shaders::get_filesystem();
  auto prologue = fs.open("programmable_vs.hlsl");
  s << prologue.begin();
  ParseShader(false, ptr, s);
  s << "return OUT;\n}\n";

  const std::string code = s.str();
  auto includer = CreateShaderIncluder();

  VertexShader result = {};
  ID3DBlob* errorBlob = nullptr;
  HRESULT hr = D3DCompile(code.c_str(), code.size(), nullptr, nullptr,
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
                << static_cast<const char*>(errorBlob->GetBufferPointer())
                << "\n";
    FAIL("Error when compiling shader:\r\n%s\r\n---\r\n%s", code.c_str(),
         static_cast<const char*>(errorBlob->GetBufferPointer()));
  }
  ASSERT(errorBlob == nullptr);

  result.decl = decl;
  return result;
}

PixelShader ParsePixelShader(const unsigned long* ptr) {
  std::stringstream ss;
  ss << std::dec;
  ss << "#include \"programmable_ps.hlsl\"\n";
  ParseShader(true, ptr, ss);
  ss << "return temp_reg[0];\n}\n";
  const std::string code = ss.str();

  std::unique_ptr<ShaderIncluder> includer = CreateShaderIncluder();

  PixelShader result = {};
  ID3DBlob* errorBlob = nullptr;
  HRESULT hr = D3DCompile(code.c_str(), code.size(), "programmable_ps", nullptr,
                          includer.get(), "PSMain", "ps_5_0",
                          D3DCOMPILE_DEBUG | D3DCOMPILE_ENABLE_STRICTNESS |
                              D3DCOMPILE_WARNINGS_ARE_ERRORS,
                          0, result.blob.GetForInit(), &errorBlob);
  if (hr != S_OK) {
    ASSERT(errorBlob);
    ASSERT(reinterpret_cast<const char*>(
               errorBlob->GetBufferPointer())[errorBlob->GetBufferSize() - 1] ==
           0);
    LOG_ERROR() << "Error when compiling shader: "
                << static_cast<const char*>(errorBlob->GetBufferPointer())
                << "\n";
    FAIL("Error when compiling shader:\r\n%s\r\n---\r\n%s", code.c_str(),
         static_cast<const char*>(errorBlob->GetBufferPointer()));
  }
  ASSERT(errorBlob == nullptr);

  return result;
}

std::unique_ptr<ShaderIncluder> CreateShaderIncluder() {
  struct ShaderIncluderImpl : public ShaderIncluder {
    cmrc::embedded_filesystem fs = cmrc::Dx8to12_shaders::get_filesystem();
    cmrc::file opened;

    HRESULT __nothrow STDMETHODCALLTYPE Open(D3D_INCLUDE_TYPE IncludeType,
                                             LPCSTR pFileName,
                                             LPCVOID pParentData,
                                             LPCVOID* ppData,
                                             UINT* pBytes) override {
      std::string path(pFileName);
      if (!fs.exists(path)) return ERROR_FILE_NOT_FOUND;
      opened = fs.open(path);
      *ppData = opened.begin();
      *pBytes = safe_cast<UINT>(opened.size());
      return S_OK;
    }

    HRESULT __nothrow STDMETHODCALLTYPE Close(LPCVOID pData) override {
      opened = cmrc::file();
      return S_OK;
    }
  };
  return std::unique_ptr<ShaderIncluder>(new ShaderIncluderImpl());
}
}  // namespace Dx8to12