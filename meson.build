# chimera-core-duckstation: DuckStation in miniBox's sandbox, as a Chimera core.
#
#   guest (core.wbx)   sh waterbox/setup-guest.sh
#                      ninja -C build/meson-guest core.wbx
#   native reference   meson setup build/meson-native
#                      ninja -C build/meson-native
#
# Both flavors compile the SAME curated source set (waterbox/sources.sh) with
# the SAME defines, and the equivalence gate compares them frame for frame.
project('chimera-core-duckstation', 'c', 'cpp',
  default_options: ['c_std=gnu11', 'cpp_std=c++20',
                    'optimization=2', 'debug=false', 'warning_level=0'])

ds = 'extern/duckstation'
ds_abs = meson.current_source_dir() / ds

# the submodule pin is pristine upstream; overlay the chimera patch set
# (idempotent - waterbox/apply-patches.sh says how it decides)
run_command(files('waterbox/apply-patches.sh'), check: true)

mb = get_option('minibox_dir')
if mb == ''
  mb_candidate = meson.current_source_dir() / '../../chimera/extern/chimera-common-minibox'
  if import('fs').is_dir(mb_candidate)
    mb = mb_candidate
  else
    error('pass -Dminibox_dir=<miniBox checkout>')
  endif
endif

# CHIMERA_WATERBOX marks the places upstream asks the host for something a
# sandbox does not have. Every patch in patches/ is behind it or behind a
# define named for the one thing it turns off.
defs = [
  '-DNDEBUG',
  '-D_FILE_OFFSET_BITS=64',
  '-DCHIMERA_WATERBOX',
  '-DCHIMERA_NO_SHADER_TRANSPILER',
  '-DCHIMERA_NO_SHARED_MEMORY',
  '-DCHIMERA_FLAT_FILESYSTEM',
  '-DCHIMERA_LAG_DETECTION',
  # what upstream's build sets for its bundled assembler
  '-DXBYAK_NO_EXCEPTION=1',
  # Upstream logs when it was compiled. A package is its commit, and two
  # builds of one commit must be the same bytes.
  '-Wno-builtin-macro-redefined', '-D__DATE__="Sep  1 2024"', '-D__TIME__="13:02:58"',
  # zlib's gz* sources include <unistd.h> only when this is set; ./configure
  # would have set it, and nobody configured this vendored copy.
  '-DHAVE_UNISTD_H',
  # zstd ships hand-written amd64 assembly for one decode loop. The C path is
  # the same bytes out and one source language fewer in a cross-built guest.
  '-DZSTD_DISABLE_ASM',
  # lzma's multithreaded match finder is not compiled (waterbox/sources.sh)
  '-D_7ZIP_ST',
  # DuckStation refuses to compile below SSE 4.1, and GSVector uses those
  # instructions: both flavors get the same ones, so both compute the same way.
  '-msse4.1',
  '-Wno-deprecated-declarations',
  # What both flavors agree on, whichever C library is underneath.
  '-include', 'chimera-shim.h',
  '-Dgettid=chimera_gettid',
]

# Time is the machine's (patches, waterbox/cinterface.cpp's
# ChimeraClockNanoseconds). -Dhost_clock=true is for the native reference
# only, and only so a gate leg can be seen to fail: it puts upstream's timers
# back on the host's clock.
if not get_option('host_clock')
  defs += ['-DCHIMERA_VIRTUAL_CLOCK']
endif

recompiler = get_option('recompiler')
sources_env = environment()
if recompiler
  # What upstream's build sets on x86-64. ENABLE_MMAP_FASTMEM is NOT set:
  # fastmem by page fault is the one thing a sandbox cannot give a code
  # generator, and the lookup-table mode needs no fault.
  defs += ['-DENABLE_RECOMPILER=1', '-DENABLE_NEWREC=1']
  sources_env.set('CHIMERA_RECOMPILER', '1')
endif

# A guest Mesa turns on DuckStation's OpenGL device and with it the hardware
# renderer: waterbox/gl-osmesa.cpp says what it is and why it is not a GPU.
mesa = get_option('mesa_guest_dir')
guest_gl = mesa != ''
if guest_gl
  sources_env.set('CHIMERA_GUEST_MESA', mesa)
  defs += ['-DENABLE_OPENGL=1', '-DCHIMERA_GUEST_GL']
  # The Vulkan device comes with the hardware renderer too. It has no
  # software fallback of its own: it draws through Chimera's Vulkan bridge on
  # the computer's graphics card, or it is not used (the Renderer setting's
  # "vulkan-hw").
  defs += ['-DENABLE_VULKAN=1', '-DCHIMERA_GUEST_VK']
endif

if meson.is_cross_build()
  # musl's world has no room for thread-local storage: a sandboxed guest has
  # one thread and its whole state must be visible to a savestate.
  defs += ['-DCHIMERA_NO_THREAD_LOCAL']
endif

add_project_arguments(defs, language: ['c', 'cpp'])
# As upstream builds it - and -fpermissive because upstream builds with clang
# and nothing else: GCC's SSE headers take a non-const pointer where clang's
# take a const one, and the difference is an error only by default.
add_project_arguments(['-fno-exceptions', '-fno-rtti', '-fpermissive'], language: ['cpp'])
# The static variable of an inline function is, by GCC's default, a symbol of
# a binding all its own (STB_GNU_UNIQUE): one definition for the whole process,
# whatever anybody says. There are two emulators in this process and each
# keeps its own of everything (waterbox/link-machine.sh), so they are ordinary
# weak symbols here, which that step can make local like the rest.
add_project_arguments(['-fno-gnu-unique'], language: ['cpp'])

inc = include_directories(
  ds / 'src',
  ds / 'dep/fmt/include',
  ds / 'dep/fast_float/include',
  ds / 'dep/simpleini/include',
  ds / 'dep/imgui/include',
  ds / 'dep/rapidyaml/include',
  ds / 'dep/xxhash/include',
  ds / 'dep/libchdr/include',
  ds / 'dep/lzma/include',
  ds / 'dep/xbyak/xbyak',
  ds / 'dep/glad/include',
  ds / 'dep/ffmpeg/include',
  # the Vulkan headers and the allocator upstream builds against
  ds / 'dep/vulkan/include',
  'extern/zlib',
  'extern/zstd/lib',
  'waterbox',
  'waterbox/stubs',
  # resources.S assembles upstream's data files in by name
  ds / 'data/resources',
)
# glslang, for the one function that asks it to compile a shader
# (util/gpu_device.cpp). Its own headers and the build_info.h its CMake would
# have generated.
inc_glslang = include_directories('extern/glslang', 'waterbox/glslang-include')

add_project_arguments(['-I' + mb / 'source/gl', '-I' + meson.current_source_dir() / 'waterbox'], language: ['c', 'cpp'])

sources_sh = files('waterbox/sources.sh')
common_sources = files(run_command(sources_sh, 'common', env: sources_env, check: true).stdout().strip().split('\n'))
util_sources = files(run_command(sources_sh, 'util', env: sources_env, check: true).stdout().strip().split('\n'))
core_sources = files(run_command(sources_sh, 'core', env: sources_env, check: true).stdout().strip().split('\n'))
dep_sources = files(run_command(sources_sh, 'deps', env: sources_env, check: true).stdout().strip().split('\n'))

deps_lib = static_library('duckstation-deps', dep_sources, include_directories: inc)

# Upstream's three libraries, each against the precompiled header upstream
# gives it (src/common has none). A source here may use a name its own
# includes never declare, because the header was always there first.
pch_util = ['-include', 'util/pch.h']
pch_core = ['-include', 'core/pch.h']
native_inc = meson.is_cross_build() ? [] : [include_directories('waterbox/native-shim')]
ds_common = static_library('duckstation-common', common_sources, include_directories: inc)
ds_util = static_library('duckstation-util', util_sources, include_directories: [inc, inc_glslang], cpp_args: pch_util)
ds_core = static_library('duckstation-core', core_sources, include_directories: inc, cpp_args: pch_core)
ds_libs = [ds_core, ds_util, ds_common]

# ---- the arcade boards: a second tree ---------------------------------------
#
# ArcadeDuck began as the console's pinned commit and is its own emulator now:
# it took the console's pads and memory cards out and changed the bus, the
# GPU and the system underneath its boards. So it is not patched into the
# console - it is compiled WHOLE, from its own submodule, with the same flags
# and the same source shape, and joined to the console at the end as one
# object whose every name but one is local (see "one binary, two machines").
arcade = get_option('arcade')
if arcade
  ad = 'extern/arcadeduck'
  ad_abs = meson.current_source_dir() / ad
  inc_ad = include_directories(
    ad / 'src',
    ad / 'dep/fmt/include',
    ad / 'dep/fast_float/include',
    ad / 'dep/simpleini/include',
    ad / 'dep/imgui/include',
    ad / 'dep/rapidyaml/include',
    ad / 'dep/xxhash/include',
    ad / 'dep/libchdr/include',
    ad / 'dep/lzma/include',
    ad / 'dep/xbyak/xbyak',
    ad / 'dep/glad/include',
    ad / 'dep/ffmpeg/include',
    ad / 'dep/vulkan/include',
    ad / 'dep/minizip/include',
    'extern/zlib',
    'extern/zstd/lib',
    'waterbox',
    'waterbox/stubs',
    ad / 'data/resources',
  )
  ad_args = ['-DCHIMERA_ARCADE']
  ad_common_sources = files(run_command(sources_sh, 'common', 'arcade', env: sources_env, check: true).stdout().strip().split('\n'))
  ad_util_sources = files(run_command(sources_sh, 'util', 'arcade', env: sources_env, check: true).stdout().strip().split('\n'))
  ad_core_sources = files(run_command(sources_sh, 'core', 'arcade', env: sources_env, check: true).stdout().strip().split('\n'))
  ad_dep_sources = files(run_command(sources_sh, 'deps', 'arcade', env: sources_env, check: true).stdout().strip().split('\n'))
  ad_deps_lib = static_library('arcadeduck-deps', ad_dep_sources, include_directories: inc_ad,
    c_args: ad_args, cpp_args: ad_args)
  ad_common = static_library('arcadeduck-common', ad_common_sources, include_directories: inc_ad,
    c_args: ad_args, cpp_args: ad_args)
  ad_util = static_library('arcadeduck-util', ad_util_sources, include_directories: [inc_ad, inc_glslang],
    c_args: ad_args, cpp_args: ad_args + pch_util)
  ad_core = static_library('arcadeduck-core', ad_core_sources, include_directories: inc_ad,
    c_args: ad_args, cpp_args: ad_args + pch_core)
endif

# What a sandbox answers differently from a desktop. Each file says what it
# refuses and why; none of them patch upstream.
#
# They are compiled once for each emulator (machine_sources), against that
# emulator's headers; waterbox/dispatch.c is compiled once, and is the guest
# ABI itself.
machine_sources = files(
  'waterbox/cinterface.cpp',
  'waterbox/host.cpp',
  'waterbox/audio-stream.cpp',
  'waterbox/memory-device.cpp',
  'waterbox/resources.cpp',
  'waterbox/resources.S',
  'waterbox/stubs/frontend.cpp',
  'waterbox/stubs/posix-stubs.cpp',
)
if not recompiler
  machine_sources += files('waterbox/stubs/no-recompiler.cpp')
endif
dispatch_sources = files('waterbox/dispatch.c')
dispatch_args = arcade ? ['-DCHIMERA_HAS_ARCADE'] : []
if guest_gl
  # The GPU bridge's guest half: a wrapper per GL entry point that hands the
  # call to the host, and the context object upstream asks for. Compiled in
  # whenever the GL device is, because it costs nothing until a host offers a
  # callback - chimera_gl_bridged() is false and the softpipe draws.
  # Generated from the SHARED table in miniBox, not from a list of our own:
  # opcodes are indices into that master list, so what this core means by an
  # opcode is what every other core and the frontend's host half mean.
  gl_bridge_guest = custom_target('gl-bridge-guest',
    output: ['gl-bridge-guest.cpp', 'gl-bridge-ops.h', 'gl-bridge-host.inc'],
    input: files('waterbox/gl-entry-points.txt'),
    command: [find_program('python3'), mb / 'source/gl/gen-gl-bridge.py',
      ds_abs / 'dep/glad/include/glad/gl.h',
      mb / 'source/gl/gl-entry-points.txt',
      '@OUTDIR@', '--only', '@INPUT@'])
  machine_sources += [files('waterbox/gl-osmesa.cpp', 'waterbox/gl-bridged.cpp'), gl_bridge_guest[0]]
endif

# ---- Vulkan: what both machines share -------------------------------------------
#
# glslang and the guest half of miniBox's Vulkan bridge are compiled once and
# linked into the binary beside the two machines, which call them by name.
# The bridge's guest half is the real vk* functions: a wrapper per command
# that hands the call to the host, generated from the SHARED list in miniBox,
# so an opcode means here what it means to the frontend's host half.
shared_libs = []
if guest_gl
  glslang_sources = files(run_command(sources_sh, 'glslang', env: sources_env, check: true).stdout().strip().split('\n'))
  # "-Dthread_local=": glslang keeps its allocator per thread. The guest has
  # one thread and keeps no thread-local storage at all (its whole state has
  # to be where a savestate finds it), so the two variables are ordinary ones.
  shared_libs += static_library('glslang', glslang_sources,
    include_directories: inc_glslang,
    cpp_args: ['-DENABLE_OPT=0', '-Dthread_local=', '-w'])
  vk_bridge = custom_target('vk-bridge',
    output: ['vk-bridge-guest.c', 'vk-bridge-ops.h', 'vk-bridge-host.inc'],
    command: [find_program('python3'), mb / 'source/vk/gen-vk-bridge.py',
      mb / 'extern/vulkan-headers/registry/vk.xml',
      mb / 'source/vk/vk-entry-points.txt', '@OUTDIR@'])
  vk_bridge_args = ['-I' + mb / 'source/vk', '-I' + mb / 'extern/vulkan-headers/include',
    '-I' + meson.current_build_dir()]
  shared_libs += static_library('vk-bridge-guest', [files(mb / 'source/vk/vk-guest.c'), vk_bridge[0], vk_bridge[1]],
    c_args: vk_bridge_args)
endif

# ---- one binary, two machines -------------------------------------------------
#
# Each emulator - its three libraries, its dependencies and its copy of the
# adapter - is joined into one object whose only global name is its table of
# the guest ABI (waterbox/machine.h, waterbox/link-machine.sh). The console's
# was always linked the plain way and still could be; it goes through the same
# step so that the two are built the same way and neither can lean on a name
# of the other's by accident.
shim_args = meson.is_cross_build() ? [] : ['-I' + mb / 'source/guest/include', '-I' + mb / 'extern/jsmn']
link_machine = files('waterbox/link-machine.sh')
cc_cmd = meson.get_compiler('c').cmd_array()

console_adapter = static_library('console-adapter', machine_sources,
  include_directories: [native_inc, inc],
  c_args: shim_args, cpp_args: pch_core + shim_args)
machines = [custom_target('console-machine',
  output: 'console-machine.o',
  input: [console_adapter, ds_core, ds_util, ds_common, deps_lib],
  command: [link_machine, '@OUTPUT@', 'chimera_machine_console', cc_cmd, '--',
    '@INPUT0@', '@INPUT1@', '@INPUT2@', '@INPUT3@', '--', '@INPUT4@'])]
if arcade
  arcade_adapter = static_library('arcade-adapter', machine_sources + files('waterbox/stubs/arcade.cpp'),
    include_directories: [native_inc, inc_ad],
    c_args: ad_args + shim_args, cpp_args: ad_args + pch_core + shim_args)
  machines += custom_target('arcade-machine',
    output: 'arcade-machine.o',
    input: [arcade_adapter, ad_core, ad_util, ad_common, ad_deps_lib],
    command: [link_machine, '@OUTPUT@', 'chimera_machine_arcade', cc_cmd, '--',
      '@INPUT0@', '@INPUT1@', '@INPUT2@', '@INPUT3@', '--', '@INPUT4@'])
endif

if meson.is_cross_build()
  # ---- the guest: core.wbx --------------------------------------------------
  mbdir = meson.get_external_property('minibox_dir')
  mbuild = meson.get_external_property('minibox_build')
  sr = meson.get_external_property('guest_sysroot')

  # Mesa, for the guest. Its static archives are linked in a group because they
  # refer to each other both ways, and the osmesa target's own object comes
  # separately: the shared library it belongs to cannot be built for a guest
  # (-fno-pic, -mcmodel=large), but osmesa_create_screen lives in it.
  mesa_link = []
  if guest_gl
    mesa_archives = run_command('find', mesa / 'build-guest2', '-name', '*.a',
      check: true).stdout().strip().split('\n')
    mesa_target = run_command('find', mesa / 'build-guest2/src/gallium/targets/osmesa',
      '-name', 'target.c.o', check: true).stdout().strip().split('\n')
    # Mesa declares pthread_mutexattr_* WEAK as a build workaround. Statically
    # linked they would resolve to address zero, and Mesa calls null the first
    # time it makes a recursive mutex.
    mesa_link = mesa_target + ['-Wl,--start-group'] + mesa_archives + ['-Wl,--end-group',
      '-Wl,-u,pthread_mutexattr_init', '-Wl,-u,pthread_mutexattr_settype',
      '-Wl,-u,pthread_mutexattr_destroy']
  endif

  executable('core', dispatch_sources, machines,
    name_suffix: 'wbx',
    link_with: shared_libs,
    c_args: dispatch_args,
    link_args: mesa_link + [
      '-static', '-no-pie',
      '-Wl,--eh-frame-hdr', '-Wl,-O2', '-Wl,--no-relax',
      '-Wl,-z,stack-size=8388608',
      '-T', mbdir / 'source/guest/linkscript.T',
      '-Wl,-u,pthread_once', '-Wl,-u,pthread_cond_wait',
      '-Wl,-u,pthread_cond_broadcast', '-Wl,-u,pthread_key_create',
      mbuild / 'source/guest/cxxglue.c.o',
      mbuild / 'source/guest/emulibc.c.o',
      '-L' + sr / 'lib', '-lstdc++', '-lgcc', '-lgcc_eh', '-lc'])
else
  # ---- the native reference and the sandbox driver --------------------------
  threads = dependency('threads')
  dl = meson.get_compiler('c').find_library('dl', required: false)
  m = meson.get_compiler('c').find_library('m', required: false)

  executable('run-native', dispatch_sources + files('waterbox/run-native.c'), machines,
    include_directories: [include_directories('waterbox/native-shim')],
    c_args: dispatch_args + shim_args,
    link_language: 'cpp',
    dependencies: [threads, dl, m])

  mbhost = mb / 'build/meson-cpp/source/host'

  # The GPU bridge's HOST half (-Dgl_bridge=true): a real GL context outside the
  # sandbox, and the dispatcher a guest reaches it through. It is the only part
  # of this repository that depends on the machine it runs on, so it is opt-in
  # and it lives in the runner rather than in the core package - a .chimeraCore
  # is a guest binary, and this is host code by definition. The frontend has
  # its own (the engine's); this one exists so the gate can drive a GPU.
  bridge_sources = []
  bridge_args = []
  bridge_deps = []
  if get_option('gl_bridge')
    gl_bridge_host = custom_target('gl-bridge-host',
      output: ['gl-bridge-host.inc', 'gl-bridge-ops.h', 'gl-bridge-guest.cpp'],
      command: [find_program('python3'), mb / 'source/gl/gen-gl-bridge.py',
        ds_abs / 'dep/glad/include/glad/gl.h',
        mb / 'source/gl/gl-entry-points.txt', '@OUTDIR@'])

    bridge_sources = [files('waterbox/gl-host.c', ds / 'dep/glad/src/gl.c'), gl_bridge_host[0]]
    bridge_args = ['-DCHIMERA_GL_BRIDGE',
      '-I' + meson.current_source_dir() / 'waterbox',
      '-I' + mb / 'source/gl',
      '-I' + ds_abs / 'dep/glad/include']
    # the generated ops header lands in the build dir
    bridge_args += ['-I' + meson.current_build_dir()]
    bridge_deps = [meson.get_compiler('c').find_library('EGL', required: true)]

    # The Vulkan bridge's host half, the same way: miniBox's one file, with
    # its dispatch generated from the shared list. It links against nothing -
    # the computer's Vulkan loader is looked for when a core asks - so a
    # machine without Vulkan loses the Vulkan renderer's checks and nothing
    # else.
    vk_bridge_host = custom_target('vk-bridge-host',
      output: ['vk-bridge-host.inc', 'vk-bridge-ops.h', 'vk-bridge-guest.c'],
      command: [find_program('python3'), mb / 'source/vk/gen-vk-bridge.py',
        mb / 'extern/vulkan-headers/registry/vk.xml',
        mb / 'source/vk/vk-entry-points.txt', '@OUTDIR@'])
    bridge_sources += [files(mb / 'source/vk/vk-host.c'), vk_bridge_host[0], vk_bridge_host[1]]
    bridge_args += ['-DCHIMERA_VK_BRIDGE', '-I' + mb / 'source/vk', '-I' + mb / 'extern/vulkan-headers/include']
    bridge_deps += [dl]
  endif

  # The sandbox runner is not part of the machine: it drives core.wbx from
  # outside, so it wants neither the machine's includes nor its shim.
  executable('run-wbx', files('waterbox/run-wbx.c') + bridge_sources,
    c_args: ['-I' + mb / 'source/host'] + bridge_args,
    dependencies: bridge_deps,
    link_args: [mbhost / 'libminiboxhost.so', '-Wl,-rpath,' + mbhost])
endif
