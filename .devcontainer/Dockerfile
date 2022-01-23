#FROM ubuntu:18.04
# ARG VARIANT="14"
# FROM mcr.microsoft.com/vscode/devcontainers/java:0-${VARIANT}
ARG VARIANT="buster"
FROM mcr.microsoft.com/vscode/devcontainers/cpp:0-${VARIANT}
RUN rm /root/.bashrc

COPY ./usr_bin_file-for-ubuntu/*.deb /tmp/usr_bin_file-for-ubuntu/
RUN dpkg -i /tmp/usr_bin_file-for-ubuntu/libmagic-mgc_5.39-3_amd64.deb \
  && dpkg -i /tmp/usr_bin_file-for-ubuntu/libmagic1_5.39-3_amd64.deb \
  && dpkg -i /tmp/usr_bin_file-for-ubuntu/file_5.39-3_amd64.deb 


RUN curl -sS https://dl.yarnpkg.com/debian/pubkey.gpg | sudo apt-key add -
RUN wget -qO - https://adoptopenjdk.jfrog.io/adoptopenjdk/api/gpg/key/public | sudo apt-key add -

# Install jdk
RUN apt update && export DEBIAN_FRONTEND=noninteractive \
  && apt install -y software-properties-common \
  && add-apt-repository --yes https://adoptopenjdk.jfrog.io/adoptopenjdk/deb/ \
  && apt update \
  && apt install -y adoptopenjdk-16-hotspot

# Install dev tools
RUN apt update && export DEBIAN_FRONTEND=noninteractive \
  && apt install -y autoconf \
  && apt install -y build-essential \
  && apt install -y zip unzip \
  && apt install -y libcups2-dev libfontconfig1-dev libasound2-dev \
  && apt install -y libx11-dev libxext-dev libxrender-dev libxrandr-dev libxtst-dev libxt-dev

#  && apt install -y ccache
# ENV CCACHE_DIR=/workspace/jdk-rtgc/ccache