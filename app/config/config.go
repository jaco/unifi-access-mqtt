package config

import (
	"encoding/json"
	"os"

	"github.com/philipparndt/go-logger"
	"github.com/philipparndt/mqtt-gateway/config"
)

var cfg Config

type Config struct {
	MQTT     config.MQTTConfig `json:"mqtt"`
	UniFi    UniFiConfig       `json:"unifi"`
	LogLevel string            `json:"loglevel,omitempty"`
}

type UniFiConfig struct {
	Host      string          `json:"host"`
	Username  string          `json:"username"`
	Password  string          `json:"password"`
	VerifySSL *bool           `json:"verify-ssl,omitempty"`
	Doorbell  *DoorbellConfig `json:"doorbell,omitempty"`
	Viewer    *ViewerConfig   `json:"viewer,omitempty"`
}

// DoorbellConfig defines the devices to use for doorbell ring triggers
type DoorbellConfig struct {
	SourceReader     string           `json:"sourceReader"`               // Device ID or MAC of the reader (UA-G3, UA-G3-Pro)
	TargetViewers    []string         `json:"targetViewers"`              // Device IDs or MACs of viewers to notify
	DismissOnContact []ContactBinding `json:"dismissOnContact,omitempty"` // External MQTT contact sensors that dismiss active calls when the door opens
}

// ContactBinding subscribes to an external MQTT door-contact topic. When the
// message contains Field == OpenValue (or the entire payload equals OpenValue
// when Field is empty), any currently ringing doorbell call is dismissed.
type ContactBinding struct {
	Topic     string `json:"topic"`           // Absolute MQTT topic, e.g. "zigbee2mqtt/eg_cont_haustuere"
	Field     string `json:"field,omitempty"` // Optional JSON field to read; empty = compare whole payload
	OpenValue any    `json:"openValue"`       // Value indicating the door is open, e.g. false (Z2M) or "open"
}

// ViewerConfig configures the connection to the UniFi Cloud Gateway's internal
// MQTT broker (mTLS) used to send remote_view RPC commands that wake viewer
// displays without ringing the doorbell reader.
type ViewerConfig struct {
	Broker        string          `json:"broker"`                 // host[:port] of the controller's MQTT broker (default port 12812)
	CA            string          `json:"ca"`                     // path to the CA certificate file
	Cert          string          `json:"cert"`                   // path to the client certificate file
	Key           string          `json:"key"`                    // path to the client private key file
	ControllerID  string          `json:"controllerID"`           // controller MAC without colons, e.g. "aabbccddeeff"
	WakeOnMotion  []MotionBinding `json:"wakeOnMotion,omitempty"` // External MQTT motion sensors that wake viewers
}

// MotionBinding subscribes to an external MQTT motion sensor topic and wakes
// the configured viewers when the trigger condition is met.
type MotionBinding struct {
	Topic        string   `json:"topic"`                  // Absolute MQTT topic, e.g. "haus/frontdoor/motion"
	Field        string   `json:"field,omitempty"`        // Optional JSON field; empty = compare whole payload (e.g. raw "true")
	Value        any      `json:"value"`                  // Trigger value, e.g. true
	Viewers      []string `json:"viewers,omitempty"`      // Viewer device IDs to wake; empty = all known viewers
	SourceReader string   `json:"sourceReader,omitempty"` // Optional source reader device ID for the remote_view (defaults to doorbell.sourceReader)
}

func (u *UniFiConfig) GetVerifySSL() bool {
	if u.VerifySSL == nil {
		return false // Default to false for self-signed certificates
	}
	return *u.VerifySSL
}

func LoadConfig(file string) (Config, error) {
	data, err := os.ReadFile(file)
	if err != nil {
		logger.Error("Error reading config file", "err", err)
		return Config{}, err
	}

	data = config.ReplaceEnvVariables(data)

	// Unmarshal the JSON data into the Config object
	err = json.Unmarshal(data, &cfg)
	if err != nil {
		logger.Error("Unmarshaling JSON", "err", err)
		return Config{}, err
	}

	// Set default values
	if cfg.LogLevel == "" {
		cfg.LogLevel = "info"
	}

	return cfg, nil
}

func Get() Config {
	return cfg
}
