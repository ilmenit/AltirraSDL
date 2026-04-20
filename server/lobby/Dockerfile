# Build from the AltirraSDL repo root so the two shared protocol
# headers in src/AltirraSDL/source/netplay/ are reachable:
#
#   docker build -f server/lobby/Dockerfile -t altirra-lobby .
#
# The standalone deploy repo (github.com/ilmenit/altirra-sdl-lobby)
# clones AltirraSDL and invokes docker with -f pointing here.

FROM alpine:3.20 AS build
RUN apk add --no-cache build-base cmake linux-headers
WORKDIR /src
COPY server/lobby           /src/server/lobby
COPY src/AltirraSDL/source/netplay/lobby_protocol.h \
     src/AltirraSDL/source/netplay/json_cursor.h    \
     /src/src/AltirraSDL/source/netplay/
# Tiny top-level CMake shim so the lobby subdirectory builds without
# the full AltirraSDL tree.  The CMakeLists in server/lobby/ resolves
# the shared headers via ${CMAKE_SOURCE_DIR}/src/AltirraSDL/source/netplay.
RUN printf 'cmake_minimum_required(VERSION 3.24)\n'                       \
           'project(altirra-lobby LANGUAGES CXX)\n'                       \
           'set(CMAKE_CXX_STANDARD 23)\n'                                 \
           'add_subdirectory(server/lobby)\n' > /src/CMakeLists.txt \
    && cmake -B /build -DCMAKE_BUILD_TYPE=Release /src                    \
    && cmake --build /build --target altirra-lobby -j                     \
    && strip /build/server/lobby/altirra-lobby

FROM alpine:3.20
RUN apk add --no-cache libstdc++
COPY --from=build /build/server/lobby/altirra-lobby /altirra-lobby
EXPOSE 8080
USER 65532:65532
ENTRYPOINT ["/altirra-lobby"]
