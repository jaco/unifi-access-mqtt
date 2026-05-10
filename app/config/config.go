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
}

// DoorbellConfig defines the devices to use for doorbell ring triggers
type DoorbellConfig struct {
	SourceReader     string           `json:"sourceReader"`               // Device ID or MAC of the reader (UA-G3, UA-G3-Pro)
	TargetViewers    []string         `json:"targetViewers"`              // Device IDs or MACs of viewers to notify
	DismissOnContact []ContactBinding `json:"dismissOnContact,omitempty"` // External MQTT contact sensors that dismiss active calls when the door opens
}

// ContactBinding subscribes to an external MQTT door-contact topic. When the
// message contains Field == OpenValue, any currently ringing doorbell call is
// dismissed (across all doors).
type ContactBinding struct {
	Topic     string `json:"topic"`     // Absolute MQTT topic, e.g. "zigbee2mqtt/eg_cont_haustuere"
	Field     string `json:"field"`     // JSON field to read, e.g. "contact"
	OpenValue any    `json:"openValue"` // Value indicating the door is open, e.g. false (Z2M) or "open"
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
