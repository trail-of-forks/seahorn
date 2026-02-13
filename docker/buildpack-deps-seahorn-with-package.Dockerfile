#
# SeaHorn build image using pre-built LLVM 20 package
# Contains all necessary dependencies to build SeaHorn
# Expects LLVM package to be in the build context
#

ARG BASE_IMAGE=jammy-scm
FROM buildpack-deps:$BASE_IMAGE

# Install dependencies (NO LLVM packages from apt)
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && \
  apt-get install -yqq software-properties-common wget gpg && \
  apt-get update && \
  apt-get upgrade -yqq && \
  apt-get install -yqq cmake cmake-data unzip \
      zlib1g-dev libzstd-dev \
      ninja-build libgraphviz-dev \
      libgmp-dev libmpfr-dev \
      libboost1.74-dev \
      python3-pip \
      less vim \
      sudo \
      graphviz libgraphviz-dev python3-pygraphviz \
      lcov gcovr rsync \
      clang-14 clang++-14 lld-14 && \
  # Install gcc-multilib only on x86_64/amd64 architecture
  if [ "$(dpkg --print-architecture)" = "amd64" ]; then \
    apt-get install -yqq gcc-multilib; \
  fi && \
  pip3 install lit OutputCheck && \
  pip3 install networkx && \
  mkdir seahorn && \
  apt-get clean && \
  rm -rf /var/lib/apt/lists/*

# Install z3 v4.8.9
WORKDIR /tmp
RUN wget https://github.com/Z3Prover/z3/releases/download/z3-4.8.9/z3-4.8.9-x64-ubuntu-16.04.zip && \
  unzip z3-4.8.9-x64-ubuntu-16.04.zip && \
  mv z3-4.8.9-x64-ubuntu-16.04 /opt/z3-4.8.9 && \
  rm z3-4.8.9-x64-ubuntu-16.04.zip

# Install yices 2.6.1
RUN curl -sSOL https://yices.csl.sri.com/releases/2.6.1/yices-2.6.1-x86_64-pc-linux-gnu-static-gmp.tar.gz && \
  tar xf yices-2.6.1-x86_64-pc-linux-gnu-static-gmp.tar.gz && \
  cd /tmp/yices-2.6.1/ && \
  ./install-yices /opt/yices-2.6.1 && \
  rm -rf /tmp/yices-2.6.1*

# Copy and install LLVM package
# This expects llvm-seahorn-*.tar.gz to be in the build context
COPY llvm-seahorn-*.tar.gz /tmp/
RUN cd /tmp && \
  mkdir -p /opt/llvm-20 && \
  tar -xzf llvm-seahorn-*.tar.gz -C /opt/llvm-20 --strip-components=1 && \
  rm llvm-seahorn-*.tar.gz

# Set environment for LLVM
ENV PATH="/opt/llvm-20/bin:${PATH}"
ENV LD_LIBRARY_PATH="/opt/llvm-20/lib:${LD_LIBRARY_PATH}"

WORKDIR /seahorn
