# This Dockerfile assumes that it is being built from the project root directory, e.g.:
# $ docker build -f docker/azahar-room/Dockerfile -t azahar-room .

# --- Builder ----------------
FROM opensauce04/azahar-build-environment:latest AS builder

COPY . /var/azahar-src

RUN mkdir builddir && cd builddir && \
    cmake /var/azahar-src -DENABLE_QT=OFF -DENABLE_TESTS=OFF -DENABLE_ROOM=ON -DENABLE_ROOM_STANDALONE=ON && \
    cmake --build . -j$(nproc) && \
    mv bin/Release/azahar-room /usr/local/bin/ && \
    cd .. && rm -rf builddir

# --- Final ------------------
FROM debian:trixie

RUN apt-get update && apt-get -y full-upgrade
RUN apt-get install -y iputils-ping net-tools

COPY --from=builder /usr/local/bin/azahar-room /usr/local/bin/azahar-room
