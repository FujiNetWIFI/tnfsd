FROM ubuntu:latest AS build
RUN apt update && apt install -y build-essential
WORKDIR /src
COPY src/* .
RUN make clean all OS=LINUX

FROM ubuntu:latest
LABEL org.opencontainers.image.authors="thom.cherryhomes@gmail.com"
EXPOSE 16384
EXPOSE 16384/udp
VOLUME /data
COPY --from=build /bin/tnfsd /bin/tnfsd

ENTRYPOINT ["/bin/tnfsd", "/data"]
