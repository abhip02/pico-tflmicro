FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       build-essential ca-certificates cmake git ninja-build python3 \
    && rm -rf /var/lib/apt/lists/*

# Cross compiler and SDK for producing RP2040 firmware as well as host demos.
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib \
    && rm -rf /var/lib/apt/lists/*

ARG PICO_SDK_VERSION=2.2.0
RUN git clone --branch "$PICO_SDK_VERSION" --depth 1 \
      https://github.com/raspberrypi/pico-sdk.git /opt/pico-sdk \
    && git -C /opt/pico-sdk submodule update --init --depth 1 lib/tinyusb
ENV PICO_SDK_PATH=/opt/pico-sdk

# Install the UF2 conversion tool once, so firmware builds need no downloads.
RUN git clone --branch "$PICO_SDK_VERSION" --depth 1 \
      https://github.com/raspberrypi/picotool.git /opt/picotool \
    && cmake -S /opt/picotool -B /tmp/picotool-build -G Ninja \
       -DPICOTOOL_NO_LIBUSB=1 -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /tmp/picotool-build --parallel 4 \
    && cmake --install /tmp/picotool-build \
    && rm -rf /tmp/picotool-build
