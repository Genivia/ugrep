#
# Dockerfile to build and run ugrep in a container (development environment).
#
# This file creates the environment to build and run ugrep in a container. If
# you just want to use ugrep in a minimal container, use the minimized Dockerfile. 
#
# Build the ugrep image:
#   $ docker build -t ugrep .
#
# Execute ugrep in the container (The / of the host is accessible in /mnt directory in the container):
#   $ docker run -v /:/mnt -it ugrep --help
# or run bash in the container instead:
#   $ docker run -v /:/mnt --entrypoint /bin/bash -it ugrep
#   $ ugrep --help

FROM ubuntu

RUN apt-get update

RUN apt-get install -y --no-install-recommends \
    autoconf \
    automake \
    build-essential \
    ca-certificates \
    pkg-config \
    make \
    git \
    clang \
    unzip \
    libpcre2-dev \
    libz-dev \
    libbz2-dev \
    liblzma-dev \
    liblz4-dev \
    libzstd-dev \
    libbrotli-dev

WORKDIR /ugrep

# Clone ugrep from GitHub
RUN git clone --single-branch --depth=1 https://github.com/Genivia/ugrep /ugrep

# Local build of ugrep
# If you want to build ugrep from a local source, uncomment the following line:
# ADD . /ugrep

RUN autoreconf -fi
RUN ./build.sh
RUN make install
RUN ugrep --version

ENTRYPOINT [ "ugrep" ]