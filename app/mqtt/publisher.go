package mqtt

import (
	"encoding/json"
	"fmt"
	"strings"

	"github.com/mqtt-home/unifi-access-mqtt/unifi"
	"github.com/philipparndt/go-logger"
	"github.com/philipparndt/mqtt-gateway/mqtt"
)

// Note: retain is configured globally in the mqtt-gateway library through the config

// DoorState represents the state published to MQTT
type DoorState struct {
	DoorID      string `json:"door_id"`
	Name        string `json:"name"`
	LockStatus  string `json:"lock_status"`
	DoorStatus  string `json:"door_status"`
	DeviceType  string `json:"device_type"`
	IsOnline    bool   `json:"is_online"`
	HasDoorbell bool   `json:"has_doorbell"`
}

// DoorbellState represents doorbell state published to MQTT
type DoorbellState struct {
	DoorID    string `json:"door_id"`
	Name      string `json:"name"`
	Status    string `json:"status"` // "ringing" or "idle"
	RequestID string `json:"request_id,omitempty"`
}

// Command represents an incoming MQTT command
type Command struct {
	Action string `json:"action"` // "unlock", "lock"
}

// Publisher handles MQTT publishing and subscribing
type Publisher struct {
	controller *unifi.Controller
}

// NewPublisher creates a new MQTT publisher
func NewPublisher(controller *unifi.Controller) *Publisher {
	return &Publisher{
		controller: controller,
	}
}

// PublishDoorState publishes the current state of a door
func (p *Publisher) PublishDoorState(door *unifi.Door) {
	topic := p.getDoorTopic(door)

	state := DoorState{
		DoorID:      door.ID,
		Name:        door.Name,
		LockStatus:  door.LockStatus,
		DoorStatus:  door.DoorStatus,
		DeviceType:  door.Device.DeviceType,
		IsOnline:    door.IsOnline,
		HasDoorbell: door.Device.HasCapability(unifi.CapabilityDoorbell),
	}

	logger.Info("Publishing door state", "topic", topic, "lock", door.LockStatus, "door", door.DoorStatus)
	p.publish(topic, state)
}

// PublishDoorbellState publishes the doorbell state
func (p *Publisher) PublishDoorbellState(door *unifi.Door) {
	topic := fmt.Sprintf("%s/doorbell", p.getDoorTopic(door))

	status := "idle"
	if door.DoorbellRinging {
		status = "ringing"
	}

	state := DoorbellState{
		DoorID:    door.ID,
		Name:      door.Name,
		Status:    status,
		RequestID: door.DoorbellRequestID,
	}

	p.publish(topic, state)
	logger.Debug("Published doorbell state", "door", door.Name, "status", status)
}

// PublishAllDoors publishes state for all doors
func (p *Publisher) PublishAllDoors() {
	doors := p.controller.GetDoors()
	logger.Info("Publishing initial state for doors", "count", len(doors))
	for _, door := range doors {
		p.PublishDoorState(door)
		if door.Device.HasCapability(unifi.CapabilityDoorbell) {
			p.PublishDoorbellState(door)
		}
	}
}

// SubscribeToCommands subscribes to command topics for all doors
func (p *Publisher) SubscribeToCommands() {
	// Subscribe to wildcard topic for all doors (base topic is added by mqtt library)
	topic := "+/set"

	mqtt.SubscribeRelative(topic, func(topic string, payload []byte) {
		p.handleCommand(topic, payload)
	})

	logger.Info("Subscribed to command topic", "topic", topic)
}

// handleCommand processes incoming MQTT commands
func (p *Publisher) handleCommand(topic string, payload []byte) {
	// Extract door name from topic: baseTopic/{doorName}/set
	parts := strings.Split(topic, "/")
	if len(parts) < 2 {
		logger.Warn("Invalid command topic", "topic", topic)
		return
	}

	// Get the door name (second to last part before "set")
	doorName := parts[len(parts)-2]

	// Find door by sanitized name
	var matchedDoor *unifi.Door
	for _, door := range p.controller.GetDoors() {
		if unifi.SanitizeName(door.Name) == doorName {
			matchedDoor = door
			break
		}
	}

	if matchedDoor == nil {
		logger.Warn("Unknown door in command", "door", doorName)
		return
	}

	var cmd Command
	if err := json.Unmarshal(payload, &cmd); err != nil {
		logger.Warn("Invalid command payload", "payload", string(payload))
		return
	}

	logger.Info("Received command", "door", matchedDoor.Name, "action", cmd.Action)

	switch strings.ToLower(cmd.Action) {
	case "unlock":
		if err := p.controller.UnlockDoor(matchedDoor); err != nil {
			logger.Error("Failed to unlock door", "door", matchedDoor.Name, "err", err)
		}
	case "lock":
		// Note: UniFi Access doesn't support explicit lock commands via API
		// The door locks automatically after a configured timeout
		logger.Warn("Lock command not supported - doors lock automatically after unlock timeout")
	case "dismiss", "cancel", "end_call":
		if err := p.controller.DismissDoorbellCall(matchedDoor); err != nil {
			logger.Error("Failed to dismiss doorbell call", "door", matchedDoor.Name, "err", err)
		}
	case "ring":
		// Trigger a doorbell ring via the remote_call API
		logger.Debug("Triggering doorbell ring", "door", matchedDoor.Name)
		if err := p.controller.TriggerDoorbellRing(matchedDoor); err != nil {
			logger.Error("Failed to trigger doorbell ring", "door", matchedDoor.Name, "err", err)
		}
	default:
		logger.Warn("Unknown action", "action", cmd.Action)
	}
}

// getDoorTopic returns the MQTT topic suffix for a door (base topic is added by mqtt library)
func (p *Publisher) getDoorTopic(door *unifi.Door) string {
	return unifi.SanitizeName(door.Name)
}

// publish publishes a message to MQTT
func (p *Publisher) publish(topic string, payload any) {
	mqtt.PublishJSON(topic, payload)
}
