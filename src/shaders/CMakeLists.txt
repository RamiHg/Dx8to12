include(${CMAKE_SOURCE_DIR}/CMakeRC.cmake)

cmrc_add_resource_library(
  Dx8to12_shaders common.hlsl lighting.hlsl ff_vertex_shader.hlsl
  programmable_vs.hlsl ps_common.hlsl programmable_ps.hlsl)
