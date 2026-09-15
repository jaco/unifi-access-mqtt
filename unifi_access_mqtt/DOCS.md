# Configuration

## Credentials

`unifi_username` and `unifi_password` must identify a local UniFi OS account
that can read the Access topology. Secrets are stored only in the add-on's
Supervisor-managed options and are written at runtime to `/data` with mode
`0600`; they are never logged or committed.

Leave `mqtt_host`, `mqtt_username`, and `mqtt_password` empty to use the Home
Assistant MQTT service discovered through Supervisor.

## UniFi private MQTT certificates

The current upstream gateway (`v0.6.1`) requires exactly these files from
`/data/unifi-access/ws/certs` on the UniFi Cloud Gateway:

* `ca-cert.pem`
* `mqtt-client-cert.pem`
* `mqtt-client-priv.pem`

Copy them once over SSH into `/ssl/unifi-access-mqtt/` on Home Assistant. The
add-on mounts HA's `/ssl` read-only. Keep the private key readable only by root
on the host and never paste it into add-on options or logs.

The upstream helper is `scripts/copy-certs.sh`. A manual equivalent is:

```bash
mkdir -p /ssl/unifi-access-mqtt
scp root@CLOUD_GATEWAY:/data/unifi-access/ws/certs/{ca-cert.pem,mqtt-client-cert.pem,mqtt-client-priv.pem} /ssl/unifi-access-mqtt/
chmod 0600 /ssl/unifi-access-mqtt/mqtt-client-priv.pem
```

## Identifiers

Use identifiers returned by UniFi Access, never display names guessed from the
UI. `controller_id` is the Cloud Gateway MAC without separators. `viewer_id`
is the `unique_id` of a device whose type is `UA-Int-Viewer`. `source_reader`
is the Access device ID associated with the door/camera to show.

The official Access developer endpoint can verify device model and ID:

```text
GET https://CLOUD_GATEWAY:12445/api/v1/developer/devices?refresh=true
Authorization: Bearer ACCESS_API_TOKEN
```

## Wake trigger

The add-on subscribes to `wake_topic`. With an empty `wake_field`, the complete
MQTT payload is compared with `wake_value`. For a Home Assistant automation,
publish the configured string (default `true`) when the motion entity changes
to `on`.
