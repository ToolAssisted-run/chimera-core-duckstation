/* glslang's build_info.h, which its own CMake generates from build_info.h.tmpl
 * and CHANGES.md. This core compiles glslang's sources directly, and the
 * submodule is pinned (extern/glslang, the tag vulkan-sdk-1.3.290.0, which is
 * version 14.3.0), so the answers are written out here. Move the pin and
 * these three numbers move with it. */
#ifndef GLSLANG_BUILD_INFO
#define GLSLANG_BUILD_INFO

#define GLSLANG_VERSION_MAJOR 14
#define GLSLANG_VERSION_MINOR 3
#define GLSLANG_VERSION_PATCH 0
#define GLSLANG_VERSION_FLAVOR ""

#define GLSLANG_VERSION_GREATER_THAN(major, minor, patch) \
    ((GLSLANG_VERSION_MAJOR) > (major) || ((major) == GLSLANG_VERSION_MAJOR && \
    ((GLSLANG_VERSION_MINOR) > (minor) || ((minor) == GLSLANG_VERSION_MINOR && \
     (GLSLANG_VERSION_PATCH) > (patch)))))

#define GLSLANG_VERSION_GREATER_OR_EQUAL_TO(major, minor, patch) \
    ((GLSLANG_VERSION_MAJOR) > (major) || ((major) == GLSLANG_VERSION_MAJOR && \
    ((GLSLANG_VERSION_MINOR) > (minor) || ((minor) == GLSLANG_VERSION_MINOR && \
     (GLSLANG_VERSION_PATCH >= (patch))))))

#define GLSLANG_VERSION_LESS_THAN(major, minor, patch) \
    ((GLSLANG_VERSION_MAJOR) < (major) || ((major) == GLSLANG_VERSION_MAJOR && \
    ((GLSLANG_VERSION_MINOR) < (minor) || ((minor) == GLSLANG_VERSION_MINOR && \
     (GLSLANG_VERSION_PATCH) < (patch)))))

#define GLSLANG_VERSION_LESS_OR_EQUAL_TO(major, minor, patch) \
    ((GLSLANG_VERSION_MAJOR) < (major) || ((major) == GLSLANG_VERSION_MAJOR && \
    ((GLSLANG_VERSION_MINOR) < (minor) || ((minor) == GLSLANG_VERSION_MINOR && \
     (GLSLANG_VERSION_PATCH <= (patch))))))

#endif // GLSLANG_BUILD_INFO
