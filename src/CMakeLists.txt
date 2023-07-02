target_sources(
  d3d8
  PRIVATE DirectX8/d3d8.h
          DirectX8/d3d8caps.h
          DirectX8/d3d8types.h
          device.cpp
          device.h
          direct3d8.cpp
          direct3d8.h
          dllmain.cpp
          util.h
          texture.cpp
          texture.h
          surface.cpp
          surface.h
          vertex_shader.h
          vertex_shader.cpp
          shader_parser.cpp
          buffer.cpp
          buffer.h
          ff_pixel_shader.cpp
          pool_heap.h
          pool_heap.cpp
          d3d8.def
          dynamic_ring_buffer.h
          dynamic_ring_buffer.cpp
          render_state.h
          render_state.cpp
          device_limits.h)

add_subdirectory(utils)
add_subdirectory(shaders)



target_link_libraries(d3d8 PUBLIC Dx8to12_shaders)
