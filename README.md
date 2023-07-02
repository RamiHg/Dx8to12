# Dx8to12 - A DirectX 8 implementation on DirectX12.

Dx8to12 implements the DirectX8 API on top of DirectX 12. The main functionality is complete. However, I haven't implemented parts of the API that weren't used in the games I was testing; for example, many of the getter functions like `GetTexture`.

## Supported Features

* The full fixed-function vertex and pixel pipeline. Vs_1_1 programmable shaders, and a subset of ps_1_3 programmable shaders (enough to get the games I was testing working).

* Cubemaps.

* Dynamic and managed buffers/textures.

## Tested Games

Battlefield 1942™ and Age of Mythology™.

## Why?

Why not?

## No, seriously. Why?

I wanted to learn DirectX 12!
