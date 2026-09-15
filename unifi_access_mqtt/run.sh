#!/usr/bin/with-contenv bashio
set -euo pipefail

unifi_host="$(bashio::config 'unifi_host')"
unifi_username="$(bashio::config 'unifi_username')"
unifi_password="$(bashio::config 'unifi_password')"
unifi_verify_ssl="$(bashio::config 'unifi_verify_ssl')"
unifi_mqtt_port="$(bashio::config 'unifi_mqtt_port')"
mqtt_host="$(bashio::config 'mqtt_host')"
mqtt_port="$(bashio::config 'mqtt_port')"
mqtt_username="$(bashio::config 'mqtt_username')"
mqtt_password="$(bashio::config 'mqtt_password')"
mqtt_base_topic="$(bashio::config 'mqtt_base_topic')"
controller_id="$(bashio::config 'controller_id')"
viewer_id="$(bashio::config 'viewer_id')"
source_reader="$(bashio::config 'source_reader')"
wake_topic="$(bashio::config 'wake_topic')"
wake_value="$(bashio::config 'wake_value')"
wake_field="$(bashio::config 'wake_field')"
ca_path="$(bashio::config 'ca_path')"
client_cert_path="$(bashio::config 'client_cert_path')"
client_key_path="$(bashio::config 'client_key_path')"
log_level="$(bashio::config 'log_level')"

if [[ -z "${mqtt_host}" ]]; then
  mqtt_host="$(bashio::services mqtt 'host')"
  mqtt_port="$(bashio::services mqtt 'port')"
  mqtt_username="$(bashio::services mqtt 'username')"
  mqtt_password="$(bashio::services mqtt 'password')"
  bashio::log.info "Using the Home Assistant MQTT service"
else
  bashio::log.info "Using the configured MQTT broker ${mqtt_host}:${mqtt_port}"
fi

for cert_file in "${ca_path}" "${client_cert_path}" "${client_key_path}"; do
  if [[ ! -r "${cert_file}" ]]; then
    bashio::exit.nok "Required mTLS file is not readable: ${cert_file}"
  fi
done

gateway_host="${unifi_host#*://}"
gateway_host="${gateway_host%%/*}"
gateway_host="${gateway_host%%:*}"

mkdir -p /data/runtime
umask 077
jq -n \
  --arg mqtt_url "tcp://${mqtt_host}:${mqtt_port}" \
  --arg mqtt_username "${mqtt_username}" \
  --arg mqtt_password "${mqtt_password}" \
  --arg mqtt_topic "${mqtt_base_topic}" \
  --arg unifi_host "${unifi_host}" \
  --arg unifi_username "${unifi_username}" \
  --arg unifi_password "${unifi_password}" \
  --argjson unifi_verify_ssl "${unifi_verify_ssl}" \
  --arg viewer_broker "${gateway_host}:${unifi_mqtt_port}" \
  --arg ca "${ca_path}" \
  --arg cert "${client_cert_path}" \
  --arg key "${client_key_path}" \
  --arg controller_id "${controller_id}" \
  --arg viewer_id "${viewer_id}" \
  --arg source_reader "${source_reader}" \
  --arg wake_topic "${wake_topic}" \
  --arg wake_field "${wake_field}" \
  --arg wake_value_raw "${wake_value}" \
  --arg log_level "${log_level}" \
  'def parsed($value): try ($value | fromjson) catch $value;
  {
    mqtt: {
      url: $mqtt_url,
      topic: $mqtt_topic,
      retain: true,
      qos: 1,
      username: $mqtt_username,
      password: $mqtt_password
    },
    unifi: {
      host: $unifi_host,
      username: $unifi_username,
      password: $unifi_password,
      "verify-ssl": $unifi_verify_ssl,
      doorbell: {
        sourceReader: $source_reader,
        targetViewers: [$viewer_id]
      },
      viewer: {
        broker: $viewer_broker,
        ca: $ca,
        cert: $cert,
        key: $key,
        controllerID: $controller_id,
        wakeOnMotion: [({topic: $wake_topic, value: parsed($wake_value_raw), viewers: [$viewer_id], sourceReader: $source_reader}
          + if $wake_field == "" then {} else {field: $wake_field} end)]
      }
    },
    loglevel: $log_level
  }' > /data/runtime/config.json

bashio::log.info "Starting UniFi Access MQTT gateway"
exec /usr/bin/unifi-access-mqtt /data/runtime/config.json
