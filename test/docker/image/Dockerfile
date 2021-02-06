FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive
ENV LC_ALL C.UTF-8

RUN apt-get update && \
    apt-get -y dist-upgrade

RUN apt-get update && \
  apt-get install -y curl \
                     dirmngr \
                     apt-transport-https \
                     lsb-release \
                     ca-certificates \
                     software-properties-common && \
  curl -sL https://deb.nodesource.com/setup_12.x | bash - && \
  add-apt-repository ppa:bartbes/love-stable -y && \
  apt-get update && \
  apt-get -y dist-upgrade && \
  apt-get -y install gcc-9 \
                     g++-9 \
                     ca-cacert \
                     locales \
                     language-pack-en \
                     tcl-dev \
                     tk-dev \
                     tcllib \
                     sudo \
                     python3-pip \
                     gdb && \
  apt-get -y autoremove

RUN ln -fs /usr/share/zoneinfo/Europe/London /etc/localtime && \
  dpkg-reconfigure --frontend noninteractive tzdata

RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-9 1 \
                        --slave   /usr/bin/g++ g++ /usr/bin/g++-9

## cleanup of files from setup
RUN rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/*

RUN useradd -ms /bin/bash -d /home/dev -G sudo dev && \
    echo "dev:dev" | chpasswd && \
    echo '%sudo ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers.d/sudo

RUN sudo pip3 install cmake

USER dev
WORKDIR /home/dev

ENV HOME /home/dev
