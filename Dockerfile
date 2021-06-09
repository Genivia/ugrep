# step 1: create a container for ugrep named "ugrep"
# docker -D build --no-cache -t ugrep .
#
# step 2: run bash in the container, e.g. to run ugrep from the command line
# docker run -it ugrep bash
#
# step 3: run ugrep in the container, for example:
# ugrep -r -n -tjava Hello ugrep/tests/

FROM ubuntu

RUN apt-get update

RUN apt-get install -y \
    make \
    vim \
    git \
    clang \
    wget \
    unzip \
    libpcre2-dev \
    libz-dev \
    libbz2-dev \
    liblzma-dev \
    liblz4-dev \
    libzstd-dev

RUN cd / &&\
    git clone https://github.com/Genivia/ugrep

RUN cd ugrep &&\
    ./build.sh &&\
    make install
