# Pinned base image — never use a floating :ubuntu tag.
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# The case is C++20, which noble's default gcc 13 builds as installed. Beyond
# the basics:
#   - libstdc++-14-dev is for clang, not gcc. emacs pulls in libgccjit0 for
#     native compilation, which pulls in libgcc-14-dev. clang then selects
#     /usr/lib/gcc/x86_64-linux-gnu/14 as its gcc installation, but clang-18
#     only depends on the gcc 13 C++ headers, so without this package clang
#     finds no standard headers and cannot link -lstdc++.
#   - libclang-rt-18-dev provides the runtimes clang's -fsanitize=address
#     needs to link. gcc's come with gcc.
#   - llvm-18 provides llvm-symbolizer; without it, clang's sanitizer reports
#     show raw addresses instead of function names and source lines.
#   - clangd is the C++ language server behind the editor's diagnostics and
#     navigation. The repo's .clangd points it at build/debug.
# clang and its packages live in the universe component, which the base image
# already enables.
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        libstdc++-14-dev \
        clang \
        clang-format \
        clangd \
        libclang-rt-18-dev \
        llvm-18 \
        cmake \
        ninja-build \
        gdb \
        git \
        ca-certificates \
        vim \
        emacs \
        curl \
    && rm -rf /var/lib/apt/lists/*

# Sanitizers look for llvm-symbolizer on PATH.
RUN ln -s /usr/lib/llvm-18/bin/llvm-symbolizer /usr/local/bin/llvm-symbolizer

# Non-root user so files created in the mounted workspace aren't owned by root.
USER ubuntu
RUN curl -fsSL https://claude.ai/install.sh | bash
WORKDIR /workspaces/firmware-interview-case
