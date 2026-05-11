package main

import (
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/mqtt-home/unifi-access-mqtt/config"
	"github.com/mqtt-home/unifi-access-mqtt/metrics"
	mqttpub "github.com/mqtt-home/unifi-access-mqtt/mqtt"
	"github.com/mqtt-home/unifi-access-mqtt/unifi"
	"github.com/philipparndt/go-logger"
	"github.com/philipparndt/mqtt-gateway/mqtt"
)

func main() {
	logger.Init("debug", logger.Logger())
	if len(os.Args) < 2 {
		logger.Error("Usage: unifi-access-mqtt <config-file>")
		os.Exit(1)
	}

	configFile := os.Args[1]

	// Load configuration
	cfg, err := config.LoadConfig(configFile)
	if err != nil {
		logger.Error("Failed to load config", "err", err)
		os.Exit(1)
	}

	// Set log level
	logger.SetLevel(cfg.LogLevel)

	logger.Info("UniFi Access MQTT Gateway starting...")
	logger.Info("Connecting to UniFi Access", "host", cfg.UniFi.Host)

	// Create UniFi Access controller
	controller := unifi.NewController(
		cfg.UniFi.Host,
		cfg.UniFi.Username,
		cfg.UniFi.Password,
		cfg.UniFi.GetVerifySSL(),
	)

	// Set doorbell configuration if present
	if cfg.UniFi.Doorbell != nil {
		controller.SetDoorbellConfig(cfg.UniFi.Doorbell.SourceReader, cfg.UniFi.Doorbell.TargetViewers)
	}

	// Connect to UniFi Access
	if err := controller.Connect(); err != nil {
		logger.Error("Failed to connect to UniFi Access", "err", err)
		os.Exit(1)
	}
	defer controller.Disconnect()

	// Connect to MQTT broker
	mqtt.Start(cfg.MQTT, "unifi_access_mqtt")

	// Create MQTT publisher
	publisher := mqttpub.NewPublisher(controller)

	// Metrics store: viewer wakes + doorbell ring/miss counters
	metricsStore := metrics.New()

	// Set up event callbacks
	controller.OnDoorUpdate = func(door *unifi.Door) {
		publisher.PublishDoorState(door)
		if door.LockStatus == "unlocked" {
			metricsStore.MarkDoorbellHandled(door.ID)
		}
	}

	controller.OnDoorbellRing = func(door *unifi.Door) {
		publisher.PublishDoorbellState(door)
		metricsStore.RecordDoorbellRing(door.ID, door.Name)
		logger.Info("Doorbell ringing", "door", door.Name)
	}

	controller.OnDoorbellCancel = func(door *unifi.Door) {
		publisher.PublishDoorbellState(door)
		metricsStore.RecordDoorbellCancel(door.ID)
		publisher.PublishMetrics(metricsStore.Snapshot())
		logger.Info("Doorbell call ended", "door", door.Name)
	}

	controller.OnDoorbellDismiss = func(door *unifi.Door) {
		metricsStore.MarkDoorbellHandled(door.ID)
	}

	// Subscribe to MQTT commands
	publisher.SubscribeToCommands()

	// Subscribe to external door-contact topics that should dismiss active calls
	if cfg.UniFi.Doorbell != nil && len(cfg.UniFi.Doorbell.DismissOnContact) > 0 {
		mqttpub.NewContactListener(controller, cfg.UniFi.Doorbell.DismissOnContact).Start()
	}

	// Connect to the controller's internal MQTT broker (mTLS) for viewer wake-up
	if cfg.UniFi.Viewer != nil && len(cfg.UniFi.Viewer.WakeOnMotion) > 0 {
		waker := unifi.NewViewerWaker(
			cfg.UniFi.Viewer.Broker,
			cfg.UniFi.Viewer.ControllerID,
			cfg.UniFi.Viewer.CA,
			cfg.UniFi.Viewer.Cert,
			cfg.UniFi.Viewer.Key,
		)
		waker.OnWake = func(viewerID string) {
			metricsStore.RecordViewerWake(viewerID)
		}
		if err := waker.Connect(); err != nil {
			logger.Error("Failed to connect viewer waker", "err", err)
		} else {
			defer waker.Disconnect()
			mqttpub.NewMotionListener(controller, waker, cfg.UniFi.Viewer.WakeOnMotion).Start()
		}
	}

	// Publish initial state for all doors
	publisher.PublishAllDoors()
	publisher.PublishMetrics(metricsStore.Snapshot())

	// Periodically refresh metrics so rolling windows decay in the broker.
	metricsTicker := time.NewTicker(time.Minute)
	defer metricsTicker.Stop()
	go func() {
		for range metricsTicker.C {
			publisher.PublishMetrics(metricsStore.Snapshot())
		}
	}()

	logger.Info("UniFi Access MQTT Gateway is running")

	// Wait for shutdown signal
	sigChan := make(chan os.Signal, 1)
	signal.Notify(sigChan, syscall.SIGINT, syscall.SIGTERM)
	<-sigChan

	logger.Info("Shutting down...")
}
