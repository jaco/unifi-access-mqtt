package mqtt

import (
	"github.com/mqtt-home/unifi-access-mqtt/config"
	"github.com/mqtt-home/unifi-access-mqtt/unifi"
	"github.com/philipparndt/go-logger"
	"github.com/philipparndt/mqtt-gateway/mqtt"
)

// MotionListener subscribes to external MQTT motion-sensor topics and wakes
// the configured UniFi Access viewers when the trigger value is observed.
type MotionListener struct {
	controller *unifi.Controller
	waker      *unifi.ViewerWaker
	bindings   []config.MotionBinding
}

// NewMotionListener creates a listener for the given motion bindings.
func NewMotionListener(controller *unifi.Controller, waker *unifi.ViewerWaker, bindings []config.MotionBinding) *MotionListener {
	return &MotionListener{
		controller: controller,
		waker:      waker,
		bindings:   bindings,
	}
}

// Start subscribes to all configured topics. Must be called after mqtt.Start
// and waker.Connect.
func (l *MotionListener) Start() {
	for i := range l.bindings {
		binding := l.bindings[i]

		if binding.Topic == "" {
			logger.Warn("wake-on-motion: skipping binding with empty topic")
			continue
		}

		mqtt.Subscribe(binding.Topic, func(_ string, payload []byte) {
			l.handle(binding, payload)
		})
		logger.Info("wake-on-motion: subscribed",
			"topic", binding.Topic, "field", binding.Field)
	}
}

func (l *MotionListener) handle(b config.MotionBinding, payload []byte) {
	matched, value, ok := matchPayload(payload, b.Field, b.Value)
	if !ok {
		logger.Debug("wake-on-motion: payload could not be matched",
			"topic", b.Topic, "field", b.Field)
		return
	}
	if !matched {
		logger.Trace("wake-on-motion: value does not match",
			"topic", b.Topic, "field", b.Field, "value", value)
		return
	}

	viewers := b.Viewers
	if len(viewers) == 0 {
		viewers = l.controller.GetViewerIDs()
	}
	if len(viewers) == 0 {
		logger.Warn("wake-on-motion: no viewers to wake",
			"topic", b.Topic)
		return
	}

	source := b.SourceReader
	if source == "" {
		source = l.controller.GetDoorbellSourceReader()
	}

	logger.Info("wake-on-motion: motion detected, waking viewers",
		"topic", b.Topic, "viewers", viewers, "source", source)
	if err := l.waker.Wake(viewers, source); err != nil {
		logger.Warn("wake-on-motion: wake failed",
			"topic", b.Topic, "err", err)
	}
}
