# syntax=docker/dockerfile:1

FROM pharndt/unifi-access-mqtt:v0.6.1 AS upstream
FROM ghcr.io/home-assistant/base:3.22

ARG BUILD_VERSION
ARG BUILD_ARCH

LABEL \
  io.hass.name="UniFi Access MQTT" \
  io.hass.description="Bridge UniFi Access to Home Assistant MQTT and wake Intercom Viewers" \
  io.hass.version="${BUILD_VERSION}" \
  io.hass.type="app" \
  io.hass.arch="${BUILD_ARCH}"

COPY --from=upstream /unifi-access-mqtt /usr/bin/unifi-access-mqtt
COPY run.sh /run.sh
RUN chmod 0755 /run.sh /usr/bin/unifi-access-mqtt

CMD ["/run.sh"]
