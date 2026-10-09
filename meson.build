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
  'extern/zlib',
  'extern/zstd/lib',
  'waterbox',
  'waterbox/stubs',
  # resources.S assembles upstream's data files in by name
  ds / 'data/resources',
)

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
ds_util = static_library('duckstation-util', util_sources, include_directories: inc, cpp_args: pch_util)
ds_core = static_library('duckstation-core', core_sources, include_directories: inc, cpp_args: pch_core)
ds_libs = [ds_core, ds_util, ds_common]

# What a sandbox answers differently from a desktop. Each file says what it
# refuses and why; none of them patch upstream.
driver_sources = files(
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
  driver_sources += files('waterbox/stubs/no-recompiler.cpp')
endif
if guest_gl
  driver_sources += files('waterbox/gl-osmesa.cpp')
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

  executable('core', driver_sources,
    name_suffix: 'wbx',
    include_directories: inc,
    cpp_args: pch_core,
    link_whole: ds_libs,
    link_with: [deps_lib],
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

  executable('run-native', driver_sources + files('waterbox/run-native.c'),
    include_directories: [include_directories('waterbox/native-shim'), inc],
    c_args: ['-I' + mb / 'source/guest/include', '-I' + mb / 'extern/jsmn'],
    cpp_args: pch_core + ['-I' + mb / 'source/guest/include', '-I' + mb / 'extern/jsmn'],
    link_whole: ds_libs,
    link_with: [deps_lib],
    dependencies: [threads, dl, m])

  mbhost = mb / 'build/meson-cpp/source/host'

  # The sandbox runner is not part of the machine: it drives core.wbx from
  # outside, so it wants neither the machine's includes nor its shim.
  executable('run-wbx', files('waterbox/run-wbx.c'),
    c_args: ['-I' + mb / 'source/host'],
    link_args: [mbhost / 'libminiboxhost.so', '-Wl,-rpath,' + mbhost])
endif
