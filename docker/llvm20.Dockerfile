# LLVM 20 Builder Dockerfile for SeaHorn
# This produces a distribution package in /llvm20/
# Arguments:
#  - BUILD_TYPE: Debug, Release, RelWithDebInfo

ARG BUILD_TYPE=RelWithDebInfo

# Pull base image
FROM buildpack-deps:jammy

ARG BUILD_TYPE
RUN echo "Build type set to: $BUILD_TYPE"

# Install build dependencies
RUN apt-get update && \
  apt-get install -yqq \
    cmake \
    ninja-build \
    python3 \
    python3-pip \
    clang-14 \
    clang++-14 \
    lld-14 \
    libc++-14-dev \
    libc++abi-14-dev && \
  pip3 install lit && \
  apt-get clean && \
  rm -rf /var/lib/apt/lists/*

# Use lld for faster linking
RUN update-alternatives --install "/usr/bin/ld" "ld" "/usr/bin/ld.lld-14" 50

WORKDIR /llvm20

# Clone LLVM 20.1.4 from official repository
RUN git clone https://github.com/llvm/llvm-project.git repo -b llvmorg-20.1.4 --depth=1

# Build LLVM
WORKDIR /llvm20/build
RUN cmake -G Ninja \
    -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
    -DCMAKE_INSTALL_PREFIX=/llvm20/install \
    -DCMAKE_C_COMPILER=clang-14 \
    -DCMAKE_CXX_COMPILER=clang++-14 \
    -DLLVM_ENABLE_PROJECTS="clang;lld;polly" \
    -DLLVM_TARGETS_TO_BUILD="X86;ARM;AArch64" \
    -DLLVM_ENABLE_ASSERTIONS=ON \
    -DLLVM_ENABLE_RTTI=ON \
    -DLLVM_ENABLE_EH=ON \
    -DLLVM_INCLUDE_TESTS=OFF \
    -DLLVM_INCLUDE_BENCHMARKS=OFF \
    -DLLVM_INCLUDE_EXAMPLES=OFF \
    -DLLVM_INCLUDE_DOCS=OFF \
    -DLLVM_BINDINGS_LIST="" \
    -DLLVM_ENABLE_BINDINGS=OFF \
    -DCPACK_GENERATOR="TGZ" \
    -DCPACK_PACKAGE_FILE_NAME="llvm-seahorn-20.1.4-jammy-${BUILD_TYPE}" \
    ../repo/llvm

# Build and create package
RUN ninja && \
    ninja package && \
    mv LLVM-*.tar.gz /llvm20/ || mv llvm-seahorn-*.tar.gz /llvm20/ && \
    # Clean up build directory to save space
    cd /llvm20 && \
    rm -rf build repo

WORKDIR /llvm20

# Create copy script
RUN echo '#!/bin/sh' > copy-package.sh && \
    echo 'cp *.tar.gz /host/' >> copy-package.sh && \
    chmod +x copy-package.sh

# Default command copies package to /host mount
CMD ["./copy-package.sh"]
