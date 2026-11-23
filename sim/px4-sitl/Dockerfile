# PX4 v1.17.0 headless SITL (SIH quadrotor), built from source.
#
# No px4io/px4-sitl image is published for the v1.17.0 release, so this builds
# the release tag itself. The first build takes a while; later ones reuse the
# Docker layer cache.

ARG PX4_VERSION=v1.17.0

# Official PX4 build environment for the same release (digest of the tag when I
# pinned it: sha256:82e3ddcf4bf3fa4dbc0be6489c6de4158d0efbc8530182c691184302027335ce).
FROM px4io/px4-dev:v1.17.0 AS build
ARG PX4_VERSION
ARG JOBS=8
WORKDIR /src
RUN git clone --depth 1 --branch "${PX4_VERSION}" https://github.com/PX4/PX4-Autopilot.git px4 \
    && cd px4 \
    && git submodule update --init --recursive --depth 1 --jobs 8
WORKDIR /src/px4
RUN make px4_sitl_default -j"${JOBS}" \
    && mkdir -p /out/bin /out/etc \
    && cp -a build/px4_sitl_default/bin/. /out/bin/ \
    && cp -aL build/px4_sitl_default/etc/. /out/etc/

FROM ubuntu:24.04
ARG PX4_VERSION
LABEL org.opencontainers.image.title="px4-sitl (SIH)" \
      org.opencontainers.image.version="${PX4_VERSION}"
COPY --from=build /out/ /opt/px4/
COPY entrypoint.sh /opt/px4/bin/skyrelay-entrypoint.sh
# PX4 sends the offboard link to 127.0.0.1 by default. Make that one link use
# the partner address in $SKYRELAY_HOST_IP, which the entrypoint sets at run
# time. The grep fails the build if PX4 ever changes the line I patch.
RUN sed -i 's/-m onboard -o \$udp_offboard_port_remote/-m onboard -t $SKYRELAY_HOST_IP -o $udp_offboard_port_remote/' \
        /opt/px4/etc/init.d-posix/px4-rc.mavlink \
    && grep -q -- '-m onboard -t \$SKYRELAY_HOST_IP -o \$udp_offboard_port_remote' \
        /opt/px4/etc/init.d-posix/px4-rc.mavlink
ENV PX4_SIM_MODEL=sihsim_quadx \
    PX4_SIMULATOR=sihsim \
    INSTANCE=0
WORKDIR /var/lib/px4
ENTRYPOINT ["/opt/px4/bin/skyrelay-entrypoint.sh"]
