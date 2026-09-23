# Changelog

## 0.1.1

- Track physical Intercom Viewer calls from the controller's internal MQTT RPC
  stream so `dismiss` has the request ID required to end the active call.

## 0.1.0

* Initial Home Assistant add-on wrapper.
* Supervisor MQTT service discovery with optional manual broker settings.
* Read-only `/ssl` mTLS certificate access.
* Configurable `remote_view` wake topic, Viewer, source reader and controller.
