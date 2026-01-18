# This Dockerfile assumes that it is being built from the project root directory, e.g.:
# $ docker build -f docker/azahar-room/Dockerfile -t azahar-room .

# --- Builder ----------------
FROM opensauce04/azahar-build-environment:latest AS builder

COPY . /var/azahar-src

RUN mkdir builddir && cd builddir && \
    cmake /var/azahar-src -G Ninja \
                          -DENABLE_QT=OFF \
                          -DENABLE_TESTS=OFF \
                          -DENABLE_ROOM=ON \
                          -DENABLE_ROOM_STANDALONE=ON \
                          -DENABLE_OPENGL=OFF $( : "TODO: Can we disable these automatically when there's no frontend?") \
                          -DENABLE_VULKAN=OFF \
                          -DENABLE_SDL2=OFF \
                          -DENABLE_LIBUSB=OFF \
                          -DENABLE_CUBEB=OFF \
                          -DENABLE_OPENAL=OFF && \
    ninja && \
    mv bin/Release/azahar-room /usr/local/bin/ && \
    cd .. && rm -rf builddir

# --- Final ------------------
FROM debian:trixie AS final

RUN apt-get update && apt-get -y full-upgrade
RUN apt-get install -y iputils-ping net-tools

COPY --from=builder /usr/local/bin/azahar-room /usr/local/bin/azahar-room
