# step 1: create a container for ugrep named "ugrep"
# docker -D build --no-cache -t ugrep .
#
# step 2: run bash in the container to execute ugrep from the command line
# docker run -it ugrep bash
#
# step 3: run ugrep, for example:
# ugrep -r -tjava Hello tests/

FROM ubuntu

RUN apt-get update

RUN apt-get install -y \
    make \
    vim \
    git \
    g\+\+ \
    wget \
    unzip \
    libboost-all-dev \
    libz-dev \
    libbz2-dev \
    liblzma-dev

RUN cd / &&\
    git clone https://github.com/Genivia/ugrep &&\
    cd ugrep &&\
    touch config.h.in configure &&\
    ./configure --enable-color &&\
    make -j &&\
    make install
