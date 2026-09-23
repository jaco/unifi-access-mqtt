# syntax=docker/dockerfile:1

FROM golang:1.25-alpine AS builder

ARG BUILD_VERSION

RUN apk add --no-cache git
RUN git clone --depth 1 --branch "addon-v${BUILD_VERSION}" \
    https://github.com/jaco/unifi-access-mqtt.git /src
WORKDIR /src/app
RUN CGO_ENABLED=0 GOOS=linux go build -trimpath -ldflags="-s -w" -o /unifi-access-mqtt .

FROM ghcr.io/home-assistant/base:3.22

ARG BUILD_VERSION
ARG BUILD_ARCH

LABEL \
  io.hass.name="UniFi Access MQTT" \
  io.hass.description="Bridge UniFi Access to Home Assistant MQTT and wake Intercom Viewers" \
  io.hass.version="${BUILD_VERSION}" \
  io.hass.type="app" \
  io.hass.arch="${BUILD_ARCH}"

COPY --from=builder /unifi-access-mqtt /usr/bin/unifi-access-mqtt
COPY run.sh /run.sh
RUN chmod 0755 /run.sh /usr/bin/unifi-access-mqtt

CMD ["/run.sh"]
