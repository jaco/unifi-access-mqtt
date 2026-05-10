package mqtt

import (
	"encoding/json"

	"github.com/mqtt-home/unifi-access-mqtt/config"
	"github.com/mqtt-home/unifi-access-mqtt/unifi"
	"github.com/philipparndt/go-logger"
	"github.com/philipparndt/mqtt-gateway/mqtt"
)

// ContactListener subscribes to external MQTT door-contact topics (e.g. Zigbee2MQTT)
// and dismisses active UniFi Access doorbell calls when the configured contact opens.
type ContactListener struct {
	controller *unifi.Controller
	bindings   []config.ContactBinding
}

// NewContactListener creates a listener for the given bindings.
func NewContactListener(controller *unifi.Controller, bindings []config.ContactBinding) *ContactListener {
	return &ContactListener{
		controller: controller,
		bindings:   bindings,
	}
}

// Start subscribes to all configured topics. Must be called after mqtt.Start.
func (l *ContactListener) Start() {
	for i := range l.bindings {
		binding := l.bindings[i]

		if binding.Topic == "" || binding.Field == "" {
			logger.Warn("dismiss-on-contact: skipping invalid binding",
				"topic", binding.Topic, "field", binding.Field)
			continue
		}

		mqtt.Subscribe(binding.Topic, func(_ string, payload []byte) {
			l.handle(binding, payload)
		})
		logger.Info("dismiss-on-contact: subscribed",
			"topic", binding.Topic, "field", binding.Field)
	}
}

func (l *ContactListener) handle(b config.ContactBinding, payload []byte) {
	var data map[string]any
	if err := json.Unmarshal(payload, &data); err != nil {
		logger.Warn("dismiss-on-contact: invalid JSON", "topic", b.Topic, "err", err)
		return
	}

	value, ok := data[b.Field]
	if !ok {
		logger.Debug("dismiss-on-contact: field not present in payload",
			"topic", b.Topic, "field", b.Field)
		return
	}

	if !jsonEqual(value, b.OpenValue) {
		logger.Trace("dismiss-on-contact: value does not match openValue",
			"topic", b.Topic, "field", b.Field, "value", value)
		return
	}

	dismissed := 0
	for _, door := range l.controller.GetDoors() {
		if door.DoorbellRequestID == "" {
			continue
		}
		logger.Info("dismiss-on-contact: dismissing call",
			"door", door.Name, "topic", b.Topic)
		if err := l.controller.DismissDoorbellCall(door); err != nil {
			logger.Warn("dismiss-on-contact: dismiss failed",
				"door", door.Name, "err", err)
			continue
		}
		dismissed++
	}
	if dismissed == 0 {
		logger.Debug("dismiss-on-contact: contact opened but no active doorbell call",
			"topic", b.Topic)
	}
}

// jsonEqual compares two arbitrary JSON-decoded values by re-marshaling and
// string-comparing. This makes bool, string, and number comparisons uniform
// regardless of how the configured OpenValue was decoded.
func jsonEqual(a, b any) bool {
	ab, err1 := json.Marshal(a)
	bb, err2 := json.Marshal(b)
	if err1 != nil || err2 != nil {
		return false
	}
	return string(ab) == string(bb)
}
