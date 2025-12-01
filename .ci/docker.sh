mkdir build

docker build -f docker/azahar-room/Dockerfile -t azahar-room .
docker save azahar-room:latest > build/azahar-room.dockerimage