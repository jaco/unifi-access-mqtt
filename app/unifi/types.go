package unifi

import (
	"encoding/json"
	"strings"
	"time"
)

// TopologyResponse represents the raw response from topology4 API
type TopologyResponse struct {
	Code  int               `json:"code"`
	CodeS string            `json:"codeS"`
	Data  []BuildingConfig  `json:"data"`
}

// BuildingConfig represents a building in the topology
type BuildingConfig struct {
	UniqueID     string           `json:"unique_id"`
	Name         string           `json:"name"`
	Floors       []FloorConfig    `json:"floors"`
	DeviceGroups [][]DeviceConfig `json:"device_groups"` // Building-level devices (Viewers)
}

// BootstrapResponse represents the parsed bootstrap data
type BootstrapResponse struct {
	Version string           `json:"version"`
	Host    ControllerHost   `json:"host"`
	Devices []DeviceConfig   `json:"devices"`
	Doors   []DoorConfig     `json:"full_doors"`
	Viewers []DeviceConfig   `json:"viewers"` // Intercom Viewer devices
}

// ControllerHost represents the UniFi Access controller information
type ControllerHost struct {
	MAC             string `json:"mac"`
	Name            string `json:"name"`
	FirmwareVersion string `json:"firmware_version"`
}

// DeviceConfig represents a UniFi Access device configuration
type DeviceConfig struct {
	UniqueID       string         `json:"unique_id"`
	ConnectedUAHID string         `json:"connected_uah_id"` // Used by Viewers instead of unique_id
	Name           string         `json:"name"`
	DeviceType     string         `json:"device_type"` // UAH, UGT, UA-ULTRA, UA-Hub-Door-Mini, UA-Int-Viewer
	DisplayModel   string         `json:"display_model"`
	MAC            string         `json:"mac"`
	IP             string         `json:"ip,omitempty"`
	IsOnline       bool           `json:"is_online"`
	IsManaged      bool           `json:"is_managed"`
	Capabilities   []string       `json:"capabilities"`
	Configs        []ConfigEntry  `json:"configs,omitempty"`
	Extensions     []Extension    `json:"extensions,omitempty"`
	Door           *DoorReference `json:"door,omitempty"`
}

// GetID returns the effective device ID (unique_id or connected_uah_id for viewers)
func (d *DeviceConfig) GetID() string {
	if d.UniqueID != "" {
		return d.UniqueID
	}
	return d.ConnectedUAHID
}

// ConfigEntry represents a device configuration entry
type ConfigEntry struct {
	Key   string `json:"key"`
	Value string `json:"value"`
}

// Extension represents device extension data
type Extension struct {
	ExtensionName string `json:"extension_name"`
	TargetName    string `json:"target_name,omitempty"`
	TargetValue   string `json:"target_value,omitempty"`
}

// DoorReference represents a reference to a door
type DoorReference struct {
	UniqueID string `json:"unique_id"`
	Name     string `json:"name"`
}

// DoorConfig represents a door configuration
type DoorConfig struct {
	UniqueID            string `json:"unique_id"`
	Name                string `json:"name"`
	DoorPositionStatus  string `json:"door_position_status,omitempty"` // "open" or "close"
	DoorLockRelayStatus string `json:"door_lock_relay_status,omitempty"` // "lock" or "unlock"
}

// FloorConfig represents a floor configuration
type FloorConfig struct {
	UniqueID string              `json:"unique_id"`
	Name     string              `json:"name"`
	Doors    []DoorLocationConfig `json:"doors"`
}

// DoorLocationConfig represents a door in the topology with its devices
type DoorLocationConfig struct {
	UniqueID            string           `json:"unique_id"`
	Name                string           `json:"name"`
	DoorPositionStatus  string           `json:"door_position_status,omitempty"`
	DoorLockRelayStatus string           `json:"door_lock_relay_status,omitempty"`
	DeviceGroups        [][]DeviceConfig `json:"device_groups"` // Array of array of devices
}

// EventPacket represents a real-time event from the WebSocket
type EventPacket struct {
	Event         string                 `json:"event"`
	EventObjectID string                 `json:"event_object_id,omitempty"`
	Data          map[string]interface{} `json:"-"`
	DataString    string                 `json:"-"`
	Meta          *EventMeta             `json:"meta,omitempty"`
	Timestamp     time.Time              `json:"timestamp,omitempty"`
}

// UnmarshalJSON accepts `data` as either a JSON object or a string.
// Some periodic WebSocket messages encode `data` as a plain string;
// those would otherwise fail strict map decoding and drop the whole event.
func (e *EventPacket) UnmarshalJSON(b []byte) error {
	aux := struct {
		Event         string          `json:"event"`
		EventObjectID string          `json:"event_object_id,omitempty"`
		Data          json.RawMessage `json:"data,omitempty"`
		Meta          *EventMeta      `json:"meta,omitempty"`
		Timestamp     time.Time       `json:"timestamp,omitempty"`
	}{}
	if err := json.Unmarshal(b, &aux); err != nil {
		return err
	}
	e.Event = aux.Event
	e.EventObjectID = aux.EventObjectID
	e.Meta = aux.Meta
	e.Timestamp = aux.Timestamp

	if len(aux.Data) == 0 {
		return nil
	}
	if err := json.Unmarshal(aux.Data, &e.Data); err == nil {
		return nil
	}
	// Fall back to string form; ignore other shapes silently.
	_ = json.Unmarshal(aux.Data, &e.DataString)
	return nil
}

// EventMeta represents event metadata
type EventMeta struct {
	ObjectType string `json:"object_type,omitempty"`
	ID         string `json:"id,omitempty"`
}

// DoorbellRingData represents doorbell ring event data
type DoorbellRingData struct {
	RequestID      string `json:"request_id"`
	ConnectedUAHID string `json:"connected_uah_id"`
	DeviceID       string `json:"device_id"`       // The actual doorbell/camera device ID
	RoomID         string `json:"room_id"`         // Room ID for the call
	DoorbellChannel string `json:"doorbell_channel"` // Doorbell channel
}

// DoorbellCancelData represents doorbell cancel event data
type DoorbellCancelData struct {
	RemoteCallRequestID string `json:"remote_call_request_id"`
}

// DeviceUpdateData represents device update event data
type DeviceUpdateData struct {
	DeviceConfig
}

// LocationState represents location state for UGT devices
type LocationState struct {
	LocationID string `json:"location_id"`
	Lock       string `json:"lock"` // "locked" or "unlocked"
	DPS        string `json:"dps"`  // "open" or "close"
}

// Event types
const (
	EventDeviceRemoteUnlock = "access.data.device.remote_unlock"
	EventDeviceUpdate       = "access.data.device.update"
	EventDeviceUpdateV2     = "access.data.v2.device.update"
	EventLocationUpdateV2   = "access.data.v2.location.update"
	EventDoorbellRing       = "access.remote_view"
	EventDoorbellCancel     = "access.remote_view.change"
	EventDeviceDelete       = "access.data.device.delete"
	EventBootstrap          = "bootstrap"
)

// Device types
const (
	DeviceTypeUAH         = "UAH"
	DeviceTypeUGT         = "UGT"
	DeviceTypeUAUltra     = "UA-ULTRA"
	DeviceTypeUAHubMini   = "UA-Hub-Door-Mini"
	DeviceTypeViewer      = "UA-Int-Viewer"
	DeviceTypeG3Reader    = "UA-G3"
	DeviceTypeG3ProReader = "UA-G3-Pro"
)

// Device capabilities
const (
	CapabilityIsHub            = "is_hub"
	CapabilityIsReader         = "is_reader"
	CapabilityDoorbell         = "door_bell"
	CapabilityFaceUnlock       = "identity_face_unlock"
	CapabilityHandWave         = "hand_wave"
	CapabilityMobileUnlock     = "mobile_unlock_ver2"
	CapabilityNFC              = "nfc"
	CapabilityPinCode          = "pin_code"
	CapabilityQRCode           = "qr_code"
)

// HasCapability checks if a device has a specific capability
func (d *DeviceConfig) HasCapability(cap string) bool {
	for _, c := range d.Capabilities {
		if c == cap {
			return true
		}
	}
	return false
}

// IsViewer checks if a device is an Intercom Viewer
func (d *DeviceConfig) IsViewer() bool {
	return strings.Contains(d.DeviceType, "Viewer")
}

// IsReader checks if a device is a Reader (G3, G3-Pro, etc.)
func (d *DeviceConfig) IsReader() bool {
	return d.HasCapability(CapabilityIsReader)
}

// GetConfigValue returns the value of a config entry by key
func (d *DeviceConfig) GetConfigValue(key string) string {
	for _, c := range d.Configs {
		if c.Key == key {
			return c.Value
		}
	}
	return ""
}

// Door represents a door with its associated device and current state
type Door struct {
	ID                  string
	Name                string
	Device              *DeviceConfig
	LockStatus          string // "locked" or "unlocked"
	DoorStatus          string // "open" or "closed"
	DoorbellRinging     bool
	DoorbellRequestID   string
	DoorbellDeviceID    string   // Device ID from active doorbell call (cleared when call ends)
	DoorbellRoomID      string   // Room ID for the active call
	DoorbellChannel     string   // Doorbell channel for the active call
	ReaderDeviceID      string   // Configured reader device ID (UA-G3, UA-G3-Pro) - set at bootstrap, never cleared
	IsOnline            bool
	ViewerIDs           []string // Associated Viewer device IDs for doorbell notifications
}

// NewDoor creates a new Door from device and door config
func NewDoor(device *DeviceConfig, door *DoorConfig) *Door {
	d := &Door{
		ID:         device.UniqueID,
		Name:       door.Name,
		Device:     device,
		IsOnline:   device.IsOnline,
		LockStatus: "locked",
		DoorStatus: "closed",
	}

	if door.DoorLockRelayStatus == "unlock" {
		d.LockStatus = "unlocked"
	}
	if door.DoorPositionStatus == "open" {
		d.DoorStatus = "open"
	}

	return d
}
