#!/bin/sh
# The curated DuckStation source set: what the Chimera core compiles of
# upstream, and nothing else. Both flavors of the build read it from here -
# the guest (core.wbx) and the native reference the equivalence gate compares
# against - so there is one answer to "which sources are this core".
#
# It is a script rather than a list because the set is described by SHAPE:
# "the machine, its renderers, and nothing that talks to a person". A pin can
# never move (docs/PLAN.md, "The pin"), so the shape matters less here than in
# other cores - but a list of a hundred and fifty names is still harder to
# read than the dozen that are left out.
#
# What is deliberately absent, and why:
#   core/achievements, util/http_downloader*      a network service
#   core/fullscreen_ui, imgui_overlays, hotkeys   a frontend already exists
#   core/game_list                                a library of somebody's files
#   core/gdb_server, pine_server, util/sockets    a sandbox has no sockets
#   core/texture_replacements                     pictures read from a host
#                                                 folder behind a movie's back
#   core/cpu_recompiler_*, cpu_newrec_*           the code generators: built
#                                                 only with -Drecompiler=true
#   util/imgui_manager, imgui_fullscreen          the on-screen display
#   util/audio_stream, cubeb_*, sdl_*             a sound device; the core's
#                                                 own stream is
#                                                 waterbox/audio-stream.cpp
#   util/input_source, sdl_input_source           host game controllers
#   util/media_capture                            an encoder; the frontend
#                                                 records
#   util/image                                    PNG, JPEG and WebP: three
#                                                 libraries to write a
#                                                 screenshot nobody takes
#   util/page_fault_handler                       faults as a fast path
#   util/postprocessing*                          host-side filters
#   util/cd_image_device                          a real drive
#   util/platform_misc_*, common/crash_handler    the desktop
#   util/{d3d11,d3d12,metal}_*                    no such thing in a sandbox
#   util/opengl_*, util/vulkan_*                  built only with a guest Mesa
#                                                 (the hardware renderer)
#
# TWO TREES. The console is extern/duckstation. The arcade boards built out of
# a PlayStation are extern/arcadeduck, which began as the same commit and is
# its own emulator now (docs/PLAN.md, "ArcadeDuck"): the second argument names
# the tree, the shape is the same, and what the arcade tree adds is added at
# the end - its boards, the two processors their sound runs on, and the zip
# reader a MAME set needs. Left out of it:
#   core/arcade/arcade_output_win32, _network     a cabinet's lamps and
#                                                 solenoids, told to a window
#                                                 message or a socket
#
# Prints paths relative to the repository root, one per line.
#
# Usage: sources.sh [common|util|core|deps] [console|arcade]
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
tree="${2:-console}"
case "$tree" in
	console) p="extern/duckstation" ;;
	arcade) p="extern/arcadeduck" ;;
	*) echo "sources.sh: the tree is console or arcade" >&2; exit 2 ;;
esac
cd "$root"

common_srcs() {
	find "$p/src/common" -maxdepth 1 -name '*.cpp' | grep -v \
		-e "/crash_handler.cpp$" \
		-e "/windows_headers" \
		| sort
	echo "$p/src/common/thirdparty/SmallVector.cpp"
}

util_srcs() {
	find "$p/src/util" -maxdepth 1 -name '*.cpp' | grep -v \
		-e "/audio_stream.cpp$" -e "/cubeb_audio_stream.cpp$" \
		-e "/sdl_" -e "/xaudio2_" -e "/xinput_" -e "/dinput_" -e "/win32_" \
		-e "/input_source.cpp$" \
		-e "/image.cpp$" \
		-e "/http_downloader" \
		-e "/imgui_manager.cpp$" -e "/imgui_fullscreen.cpp$" \
		-e "/media_capture.cpp$" \
		-e "/page_fault_handler.cpp$" \
		-e "/postprocessing" \
		-e "/sockets.cpp$" \
		-e "/cd_image_device.cpp$" \
		-e "/platform_misc_" \
		-e "/vulkan_" -e "/d3d11_" -e "/d3d12_" -e "/d3d_common" -e "/metal_" \
		-e "/opengl_" \
		| sort
	if [ -n "${CHIMERA_GUEST_MESA:-}" ]; then
		find "$p/src/util" -maxdepth 1 -name 'opengl_*.cpp' | grep -v \
			-e "_egl.cpp$" -e "_wgl.cpp$" -e "_agl" -e "_glx" -e "_x11" -e "_wayland" \
			| sort
		# The Vulkan device, with the hardware renderer: it draws through
		# Chimera's Vulkan bridge and nowhere else. The swap chain is
		# compiled because the device names it, and never made: a core has
		# no window, and the device is asked for one without a surface.
		find "$p/src/util" -maxdepth 1 -name 'vulkan_*.cpp' | sort
	fi
}

# glslang, which turns the GLSL DuckStation writes for Vulkan into SPIR-V.
# Upstream reaches it through shaderc, loaded as a shared library at run time;
# a sandbox loads nothing, so the compiler itself is compiled in - the front
# end and the SPIR-V writer, without the optimiser (SPIRV-Tools) and without
# HLSL. One copy for both emulators.
glslang_srcs() {
	g="extern/glslang"
	find "$g/glslang/MachineIndependent" "$g/glslang/GenericCodeGen" -name '*.cpp' | sort
	echo "$g/glslang/OSDependent/Unix/ossource.cpp"
	echo "$g/glslang/ResourceLimits/ResourceLimits.cpp"
	find "$g/SPIRV" -maxdepth 1 -name '*.cpp' | grep -v -e "/SpvTools.cpp$" | sort
}

core_srcs() {
	find "$p/src/core" -maxdepth 1 -name '*.cpp' | grep -v \
		-e "/achievements.cpp$" \
		-e "/fullscreen_ui.cpp$" -e "/imgui_overlays.cpp$" -e "/hotkeys.cpp$" \
		-e "/game_list.cpp$" \
		-e "/gdb_server.cpp$" -e "/pine_server.cpp$" \
		-e "/texture_replacements.cpp$" \
		-e "/cpu_recompiler_" -e "/cpu_newrec_" \
		| sort
	if [ -n "${CHIMERA_RECOMPILER:-}" ]; then
		for f in cpu_recompiler_code_generator cpu_recompiler_code_generator_generic \
			cpu_recompiler_register_cache cpu_recompiler_code_generator_x64 \
			cpu_newrec_compiler cpu_newrec_compiler_x64; do
			echo "$p/src/core/$f.cpp"
		done
	fi
	if [ "$tree" = arcade ]; then
		find "$p/src/core/arcade" "$p/src/core/m37710" "$p/src/core/musashi" \
			\( -name '*.cpp' -o -name '*.c' \) | grep -v \
			-e "/arcade_output_win32.cpp$" -e "/arcade_output_network.cpp$" \
			| sort
	fi
}

deps_srcs() {
	d="$p/dep"
	echo "$d/fmt/src/format.cc"
	echo "$d/fmt/src/os.cc"
	echo "$d/xxhash/src/xxhash.c"
	find "$d/rapidyaml/src" -name '*.cpp' | sort
	find "$d/libchdr/src" -name '*.c' | sort
	find "$d/lzma/src" -name '*.c' | grep -v -e "Mt" -e "Threads" | sort
	echo "$d/simpleini/src/ConvertUTF.c"
	for f in imgui imgui_draw imgui_tables imgui_widgets imgui_stdlib; do
		echo "$d/imgui/src/$f.cpp"
	done
	# the GL loader, when there is a GL to load
	if [ -n "${CHIMERA_GUEST_MESA:-}" ]; then
		echo "$d/glad/src/gl.c"
	fi
	# zlib and zstd: upstream takes them from the system, a core cannot
	for f in adler32 compress crc32 deflate infback inffast inflate inftrees trees uncompr zutil; do
		echo "extern/zlib/$f.c"
	done
	find extern/zstd/lib/common extern/zstd/lib/compress extern/zstd/lib/decompress -name '*.c' | sort
	# a MAME set is a zip
	if [ "$tree" = arcade ]; then
		find "$d/minizip/src" -name '*.c' | grep -v -e "iowin32" -e "/minizip.c$" -e "/miniunz.c$" | sort
	fi
}

# Three groups and not one, because upstream compiles each of its libraries
# against a precompiled header of its own and its sources lean on that:
# meson.build force-includes util/pch.h and core/pch.h for the group they
# belong to.
case "${1:-}" in
	common) common_srcs ;;
	util) util_srcs ;;
	core) core_srcs ;;
	deps) deps_srcs ;;
	glslang) glslang_srcs ;;
	*) echo "usage: sources.sh [common|util|core|deps|glslang] [console|arcade]" >&2; exit 2 ;;
esac
