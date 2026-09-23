# Changelog

## 0.1.2

- Decode `access.remote_view` data when the controller wraps it as a JSON string.
- Match physical calls to the configured reader or the only known door when
  `connected_uah_id` does not identify the door directly.
- Wait briefly for a late call event when a dismiss command arrives.

## 0.1.1

- Track physical Intercom Viewer calls from the controller's internal MQTT RPC
  stream so `dismiss` has the request ID required to end the active call.

## 0.1.0

* Initial Home Assistant add-on wrapper.
* Supervisor MQTT service discovery with optional manual broker settings.
* Read-only `/ssl` mTLS certificate access.
* Configurable `remote_view` wake topic, Viewer, source reader and controller.
