# SeaHorn builder image using pre-built LLVM package
# Arguments:
#  - BUILD_TYPE: Debug, RelWithDebInfo, Coverage
ARG BASE_IMAGE=jammy-llvm20-package
FROM seahorn/buildpack-deps-seahorn:$BASE_IMAGE

# Assume that docker-build is ran in the top-level SeaHorn directory
COPY . /seahorn
# Re-create the build directory that might have been present in the source tree
RUN rm -rf /seahorn/build /seahorn/debug /seahorn/release && \
  mkdir /seahorn/build && \
# Remove any third-party dependencies that build process clones
  rm -rf /seahorn/clam /seahorn/sea-dsa /seahorn/llvm-seahorn
WORKDIR /seahorn/build

ARG BUILD_TYPE=RelWithDebInfo

# Build configuration
RUN cmake .. -GNinja \
  -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
  -DZ3_ROOT=/opt/z3-4.8.9 \
  -DYICES2_HOME=/opt/yices-2.6.1 \
  -DCMAKE_INSTALL_PREFIX=run \
  -DCMAKE_CXX_COMPILER=/opt/llvm-20/bin/clang++ \
  -DCMAKE_C_COMPILER=/opt/llvm-20/bin/clang \
  -DSEA_ENABLE_LLD=ON \
  -DLLVM_DIR=/opt/llvm-20/lib/cmake/llvm \
  -DCPACK_GENERATOR="TGZ" \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON && \
  cmake --build . --target extra  && cmake .. && \
  cmake --build . --target crab  && cmake .. && \
  cmake --build . --target install && \
  cmake --build . --target units_z3 && \
  cmake --build . --target units_yices2 && \
  cmake --build . --target test_type_checker && \
  cmake --build . --target test_hex_dump && \
  cmake --build . --target package && \
  units/units_z3 && \
  units/units_yices2 && \
  units/units_type_checker

ENV PATH="/seahorn/build/run/bin:${PATH}"
WORKDIR /seahorn
