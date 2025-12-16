# Dockerfile för Raspberry Pi Pico W C-projekt
FROM ubuntu:22.04

# Installera nödvändiga paket
RUN apt-get update && apt-get install -y \
    cmake \
    gcc-arm-none-eabi \
    build-essential \
    git \
    libnewlib-arm-none-eabi \
    python3 \
    python3-pip \
    && rm -rf /var/lib/apt/lists/*

# Installera Pico SDK
RUN git clone -b master https://github.com/raspberrypi/pico-sdk.git /pico-sdk
ENV PICO_SDK_PATH=/pico-sdk

WORKDIR /project

