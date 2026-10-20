#!/usr/bin/env bash

# Rebuild the platform-native SDL_GPU fragment shader headers. This follows
# SDL's own shader build process and requires the SDL_shadercross CLI and xxd.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
shader="$script_dir/sdlgpu_screenfx.frag.hlsl"
generated_dir="$script_dir/generated"
temp_dir=$(mktemp -d)

cleanup() {
    rm -rf -- "$temp_dir"
}
trap cleanup EXIT HUP INT TERM

# shadercross is the preferred all-in-one compiler. Its upstream DXC +
# SPIRV-Cross tools can also produce the identical resource layout.
if command -v shadercross >/dev/null 2>&1; then
    use_shadercross=true
elif command -v dxc >/dev/null 2>&1 && command -v spirv-cross >/dev/null 2>&1; then
    use_shadercross=false
else
    echo "shadercross (or dxc and spirv-cross) is required" >&2
    exit 1
fi
command -v xxd >/dev/null 2>&1 || {
    echo "xxd is required" >&2
    exit 1
}

make_header() {
    input=$1
    symbol=$2
    output=$3

    xxd -i -n "$symbol" "$input" \
        | sed -e 's/^unsigned char /static const unsigned char /' \
            -e 's/^unsigned int /static constexpr unsigned int /' \
        > "$output"
}

mkdir -p "$generated_dir"

if "$use_shadercross"; then
    shadercross "$shader" -o "$temp_dir/screenfx.spv"
    shadercross "$shader" -o "$temp_dir/screenfx.msl"
    shadercross "$shader" -o "$temp_dir/screenfx.dxil"
else
    dxc -T ps_6_0 -E main -spirv "$shader" -Fo "$temp_dir/screenfx.spv"
    dxc -T ps_6_0 -E main "$shader" -Fo "$temp_dir/screenfx.dxil"
    spirv-cross "$temp_dir/screenfx.spv" --msl --msl-version 20100 \
        --output "$temp_dir/screenfx.msl"
fi

make_header "$temp_dir/screenfx.spv" kATSDLGPU_ScreenFX_SPIRV \
    "$generated_dir/sdlgpu_screenfx_frag_spv.h"
make_header "$temp_dir/screenfx.msl" kATSDLGPU_ScreenFX_MSL \
    "$generated_dir/sdlgpu_screenfx_frag_msl.h"
make_header "$temp_dir/screenfx.dxil" kATSDLGPU_ScreenFX_DXIL \
    "$generated_dir/sdlgpu_screenfx_frag_dxil.h"

# The offscreen GPU regression exercises the production fragment shader with
# a full-screen triangle. Keep its tiny platform vertex shaders reproducible.
test_shader="$script_dir/../../../../../tests/upstream/display_sampling.vert.hlsl"
test_generated="$script_dir/../../../../../tests/upstream/generated"
if [ -f "$test_shader" ]; then
    mkdir -p "$test_generated"
    if "$use_shadercross"; then
        shadercross "$test_shader" -o "$temp_dir/sampling.spv"
        shadercross "$test_shader" -o "$temp_dir/sampling.msl"
        shadercross "$test_shader" -o "$temp_dir/sampling.dxil"
    else
        dxc -T vs_6_0 -E main -spirv "$test_shader" -Fo "$temp_dir/sampling.spv"
        dxc -T vs_6_0 -E main "$test_shader" -Fo "$temp_dir/sampling.dxil"
        spirv-cross "$temp_dir/sampling.spv" --msl --msl-version 20100 \
            --output "$temp_dir/sampling.msl"
    fi
    for format in spv msl dxil; do
        make_header "$temp_dir/sampling.$format" kATSamplingVertex \
            "$test_generated/display_sampling_vert_$format.h"
    done
fi
