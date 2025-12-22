package unifi

import (
	"fmt"
	"strconv"
	"strings"
	"sync"

	"github.com/philipparndt/go-logger"
)

// DoorbellConfig holds the configured doorbell devices
type DoorbellConfig struct {
	SourceReader  string   // Device ID or MAC of the reader (UA-G3, UA-G3-Pro)
	TargetViewers []string // Device IDs or MACs of viewers to notify
	// Resolved values (populated during bootstrap)
	resolvedReader  string   // Resolved device ID of the reader
	resolvedViewers []string // Resolved device IDs of viewers
}

// Controller manages the connection to UniFi Access and device state
type Controller struct {
	client         *Client
	eventListener  *EventListener
	doors          map[string]*Door
	doorsByName    map[string]*Door
	viewers        map[string]bool // Track known viewer device IDs
	doorbellConfig *DoorbellConfig // Configured doorbell devices
	mu             sync.RWMutex

	// Event callbacks
	OnDoorUpdate     func(door *Door)
	OnDoorbellRing   func(door *Door)
	OnDoorbellCancel func(door *Door)
}

// NewController creates a new UniFi Access controller
func NewController(host, username, password string, verifySSL bool) *Controller {
	client := NewClient(host, username, password, verifySSL)

	c := &Controller{
		client:      client,
		doors:       make(map[string]*Door),
		doorsByName: make(map[string]*Door),
		viewers:     make(map[string]bool),
	}

	c.eventListener = NewEventListener(client)

	return c
}

// Connect establishes connection to the UniFi Access controller
func (c *Controller) Connect() error {
	// Login to the controller
	if err := c.client.Login(); err != nil {
		return err
	}

	// Bootstrap to get initial device state
	if err := c.bootstrap(); err != nil {
		return err
	}

	// Set up event handlers
	c.setupEventHandlers()

	// Start event listener
	if err := c.eventListener.Start(); err != nil {
		logger.Warn("Failed to start event listener:", err)
		// Don't fail completely, just warn
	}

	return nil
}

// Disconnect closes the connection
func (c *Controller) Disconnect() {
	c.eventListener.Stop()
}

// GetDoors returns all doors
func (c *Controller) GetDoors() []*Door {
	c.mu.RLock()
	defer c.mu.RUnlock()

	doors := make([]*Door, 0, len(c.doors))
	for _, door := range c.doors {
		doors = append(doors, door)
	}
	return doors
}

// GetDoor returns a door by ID
func (c *Controller) GetDoor(id string) *Door {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.doors[id]
}

// GetDoorByName returns a door by name (case-insensitive)
func (c *Controller) GetDoorByName(name string) *Door {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.doorsByName[NormalizeDoorName(name)]
}

// UnlockDoor unlocks a door
func (c *Controller) UnlockDoor(door *Door) error {
	logger.Info("Unlocking door:", door.Name)
	return c.client.Unlock(door.ID)
}

// TriggerDoorbellRing triggers a doorbell ring via the remote_call API
// This uses the DoorbellRequestBody format that the reader uses when someone presses the button
func (c *Controller) TriggerDoorbellRing(door *Door) error {
	var deviceID string
	var viewerIDs []string

	c.mu.RLock()
	// Use configured values if available, otherwise fall back to auto-detected
	if c.doorbellConfig != nil && c.doorbellConfig.resolvedReader != "" {
		deviceID = c.doorbellConfig.resolvedReader
		viewerIDs = c.doorbellConfig.resolvedViewers
	} else {
		// Fall back to auto-detected values
		deviceID = door.ReaderDeviceID
		if deviceID == "" {
			deviceID = door.ID
			logger.Warn("No reader device configured for door", door.Name, "- using hub ID:", deviceID)
		}
		viewerIDs = door.ViewerIDs
	}
	c.mu.RUnlock()

	logger.Debug("Triggering doorbell ring for door:", door.Name, "device:", deviceID, "viewers:", viewerIDs)

	req := DoorbellRingRequest{
		DeviceID:   deviceID,
		DeviceName: door.Name,
		DoorName:   door.Name,
		FloorName:  "", // Could be extracted from topology if needed
		InOrOut:    "in",
		ViewerIDs:  viewerIDs,
	}

	return c.client.TriggerDoorbellRing(req)
}

// DismissDoorbellCall dismisses an active doorbell call
func (c *Controller) DismissDoorbellCall(door *Door) error {
	if door.DoorbellRequestID == "" {
		return fmt.Errorf("no active doorbell call for door %s", door.Name)
	}

	// Use the doorbell device ID if available, otherwise fall back to door ID
	deviceID := door.DoorbellDeviceID
	if deviceID == "" {
		deviceID = door.ID
	}

	logger.Info("Dismissing doorbell call for door:", door.Name, "request:", door.DoorbellRequestID, "device:", deviceID)
	err := c.client.DismissDoorbellCall(deviceID, door.DoorbellRequestID, c.client.GetUserID(), c.client.GetUserName())
	if err != nil {
		return err
	}

	// Clear doorbell state after successful dismiss
	c.mu.Lock()
	door.DoorbellRinging = false
	door.DoorbellRequestID = ""
	door.DoorbellDeviceID = ""
	door.DoorbellRoomID = ""
	door.DoorbellChannel = ""
	c.mu.Unlock()

	// Trigger callback to publish updated state
	if c.OnDoorbellCancel != nil {
		c.OnDoorbellCancel(door)
	}

	return nil
}

// bootstrap retrieves initial device configuration
func (c *Controller) bootstrap() error {
	bootstrap, err := c.client.Bootstrap()
	if err != nil {
		return err
	}

	logger.Info("UniFi Access Controller:", bootstrap.Host.Name, "(Version:", bootstrap.Version+")")
	logger.Info("Bootstrap: Found", strconv.Itoa(len(bootstrap.Devices)), "devices,", strconv.Itoa(len(bootstrap.Doors)), "doors,", strconv.Itoa(len(bootstrap.Viewers)), "viewers")

	// Debug: log all devices
	for i, device := range bootstrap.Devices {
		logger.Debug("Device", i, ":", device.Name, "type=", device.DeviceType, "id=", device.UniqueID)
	}

	c.mu.Lock()
	defer c.mu.Unlock()

	// Clear existing doors
	c.doors = make(map[string]*Door)
	c.doorsByName = make(map[string]*Door)

	// Create door map for quick lookup
	doorConfigs := make(map[string]*DoorConfig)
	for i := range bootstrap.Doors {
		doorConfigs[bootstrap.Doors[i].UniqueID] = &bootstrap.Doors[i]
	}

	// Collect all Viewer IDs
	// Building-level viewers (no Door reference) are available to all doors
	// Door-level viewers are only available to their specific door
	allViewerIDs := make([]string, 0)
	doorViewers := make(map[string][]string)

	for _, viewer := range bootstrap.Viewers {
		viewerID := viewer.GetID() // Use GetID() to handle connected_uah_id for viewers
		if viewerID == "" {
			continue
		}

		// Track this viewer ID so we can ignore updates for it
		c.viewers[viewerID] = true

		if viewer.Door != nil {
			// Door-specific viewer
			doorViewers[viewer.Door.UniqueID] = append(doorViewers[viewer.Door.UniqueID], viewerID)
			logger.Debug("Viewer", viewerID, "associated with door", viewer.Door.Name)
		} else {
			// Building-level viewer - available to all doors
			allViewerIDs = append(allViewerIDs, viewerID)
			logger.Debug("Viewer", viewerID, "(", viewer.Name, ") available to all doors (building-level)")
		}
	}

	// Find reader/doorbell devices for each door (UA-G3, UA-G3-Pro, etc.)
	// These are the devices that have the camera and doorbell button
	doorReaders := make(map[string]string)
	for _, device := range bootstrap.Devices {
		if device.IsReader() && device.Door != nil {
			doorReaders[device.Door.UniqueID] = device.GetID()
			logger.Debug("Reader", device.GetID(), "(", device.Name, ") associated with door", device.Door.Name)
		}
	}

	// Process devices
	for i := range bootstrap.Devices {
		device := &bootstrap.Devices[i]

		// Only process hub devices
		if !device.HasCapability(CapabilityIsHub) {
			continue
		}

		var doorConfig *DoorConfig

		// Try to find associated door
		if device.Door != nil {
			doorConfig = doorConfigs[device.Door.UniqueID]
		}

		if doorConfig == nil {
			// Use device name if no door config
			doorConfig = &DoorConfig{
				UniqueID: device.UniqueID,
				Name:     device.Name,
			}
		}

		door := NewDoor(device, doorConfig)

		// Get initial lock state from device config
		door.LockStatus = c.getLockStatusFromDevice(device)

		// Associate Viewers with this door
		// Include both door-specific viewers and building-level viewers
		door.ViewerIDs = append([]string{}, allViewerIDs...) // Copy building-level viewers
		if device.Door != nil {
			door.ViewerIDs = append(door.ViewerIDs, doorViewers[device.Door.UniqueID]...)
			// Set the reader/doorbell device ID (UA-G3, UA-G3-Pro, etc.)
			// This is the device with the camera that should be used for doorbell triggers
			if readerID, ok := doorReaders[device.Door.UniqueID]; ok {
				door.ReaderDeviceID = readerID
			}
		}

		c.doors[door.ID] = door
		c.doorsByName[NormalizeDoorName(door.Name)] = door

		logger.Info("Door:", door.Name,
			"(Type:", device.DeviceType+",",
			"Online:", strconv.FormatBool(device.IsOnline)+",",
			"Lock:", door.LockStatus+",",
			"Doorbell:", strconv.FormatBool(device.HasCapability(CapabilityDoorbell))+",",
			"Viewers:", strconv.Itoa(len(door.ViewerIDs))+")")
	}

	// Resolve doorbell config if set
	if c.doorbellConfig != nil {
		c.resolveDoorbellConfig(bootstrap)
	}

	return nil
}

// resolveDoorbellConfig resolves MAC addresses or device IDs to actual device IDs
func (c *Controller) resolveDoorbellConfig(bootstrap *BootstrapResponse) {
	// Build lookup maps: MAC -> device ID, device ID -> device ID
	deviceMap := make(map[string]string)
	for _, device := range bootstrap.Devices {
		id := device.GetID()
		if id != "" {
			deviceMap[id] = id                               // device ID -> device ID
			deviceMap[strings.ToLower(device.MAC)] = id      // MAC (lowercase) -> device ID
			deviceMap[strings.ToUpper(device.MAC)] = id      // MAC (uppercase) -> device ID
			deviceMap[NormalizeMAC(device.MAC)] = id         // Normalized MAC -> device ID
		}
	}
	for _, viewer := range bootstrap.Viewers {
		id := viewer.GetID()
		if id != "" {
			deviceMap[id] = id
			deviceMap[strings.ToLower(viewer.MAC)] = id
			deviceMap[strings.ToUpper(viewer.MAC)] = id
			deviceMap[NormalizeMAC(viewer.MAC)] = id
		}
	}

	// Resolve source reader
	if c.doorbellConfig.SourceReader != "" {
		normalized := NormalizeMAC(c.doorbellConfig.SourceReader)
		if resolved, ok := deviceMap[normalized]; ok {
			c.doorbellConfig.resolvedReader = resolved
			logger.Info("Resolved doorbell sourceReader:", c.doorbellConfig.SourceReader, "->", resolved)
		} else if resolved, ok := deviceMap[c.doorbellConfig.SourceReader]; ok {
			c.doorbellConfig.resolvedReader = resolved
			logger.Info("Resolved doorbell sourceReader:", c.doorbellConfig.SourceReader, "->", resolved)
		} else {
			logger.Warn("Could not resolve doorbell sourceReader:", c.doorbellConfig.SourceReader)
		}
	}

	// Resolve target viewers
	c.doorbellConfig.resolvedViewers = make([]string, 0, len(c.doorbellConfig.TargetViewers))
	for _, viewer := range c.doorbellConfig.TargetViewers {
		normalized := NormalizeMAC(viewer)
		if resolved, ok := deviceMap[normalized]; ok {
			c.doorbellConfig.resolvedViewers = append(c.doorbellConfig.resolvedViewers, resolved)
			logger.Info("Resolved doorbell targetViewer:", viewer, "->", resolved)
		} else if resolved, ok := deviceMap[viewer]; ok {
			c.doorbellConfig.resolvedViewers = append(c.doorbellConfig.resolvedViewers, resolved)
			logger.Info("Resolved doorbell targetViewer:", viewer, "->", resolved)
		} else {
			logger.Warn("Could not resolve doorbell targetViewer:", viewer)
		}
	}
}

// NormalizeMAC normalizes a MAC address (removes colons/dashes, lowercase)
func NormalizeMAC(mac string) string {
	mac = strings.ReplaceAll(mac, ":", "")
	mac = strings.ReplaceAll(mac, "-", "")
	return strings.ToLower(mac)
}

// getLockStatusFromDevice extracts lock status from device configuration
func (c *Controller) getLockStatusFromDevice(device *DeviceConfig) string {
	var relayKey string

	switch device.DeviceType {
	case DeviceTypeUAUltra, DeviceTypeUAHubMini:
		relayKey = "output_d1_lock_relay"
	case DeviceTypeUGT:
		relayKey = "output_oper1_relay"
	default: // UAH
		relayKey = "input_state_rly-lock_dry"
	}

	value := device.GetConfigValue(relayKey)
	if value == "off" {
		return "locked"
	}
	return "unlocked"
}

// setupEventHandlers sets up handlers for real-time events
func (c *Controller) setupEventHandlers() {
	// Doorbell ring event
	c.eventListener.On(EventDoorbellRing, func(event EventPacket) {
		c.handleDoorbellRing(event)
	})

	// Doorbell cancel event
	c.eventListener.On(EventDoorbellCancel, func(event EventPacket) {
		c.handleDoorbellCancel(event)
	})

	// Device update event
	c.eventListener.On(EventDeviceUpdate, func(event EventPacket) {
		c.handleDeviceUpdate(event)
	})

	// Device update v2 event
	c.eventListener.On(EventDeviceUpdateV2, func(event EventPacket) {
		c.handleDeviceUpdateV2(event)
	})

	// Remote unlock event
	c.eventListener.On(EventDeviceRemoteUnlock, func(event EventPacket) {
		c.handleRemoteUnlock(event)
	})

	// Location update v2 event
	c.eventListener.On(EventLocationUpdateV2, func(event EventPacket) {
		c.handleLocationUpdate(event)
	})

	// Bootstrap event (full refresh)
	c.eventListener.On(EventBootstrap, func(event EventPacket) {
		logger.Info("Received bootstrap event, refreshing device state")
		if err := c.bootstrap(); err != nil {
			logger.Error("Failed to refresh bootstrap:", err)
		}
	})

	// Log all events in trace mode
	c.eventListener.On("*", func(event EventPacket) {
		logger.Trace("Event:", event.Event, "(Object:", event.EventObjectID+")")
	})
}

// handleDoorbellRing handles doorbell ring events
func (c *Controller) handleDoorbellRing(event EventPacket) {
	data := ParseDoorbellRingData(event)
	if data == nil {
		return
	}

	c.mu.Lock()
	door := c.doors[data.ConnectedUAHID]
	if door != nil {
		door.DoorbellRinging = true
		door.DoorbellRequestID = data.RequestID
		door.DoorbellDeviceID = data.DeviceID
		door.DoorbellRoomID = data.RoomID
		door.DoorbellChannel = data.DoorbellChannel
	}
	c.mu.Unlock()

	if door != nil {
		logger.Info("Doorbell ring on", door.Name, "(Request ID:", data.RequestID, "Device:", data.DeviceID+")")
		if c.OnDoorbellRing != nil {
			c.OnDoorbellRing(door)
		}
	}
}

// handleDoorbellCancel handles doorbell cancel events
func (c *Controller) handleDoorbellCancel(event EventPacket) {
	data := ParseDoorbellCancelData(event)
	if data == nil {
		return
	}

	c.mu.Lock()
	var matchedDoor *Door
	for _, door := range c.doors {
		if door.DoorbellRequestID == data.RemoteCallRequestID {
			door.DoorbellRinging = false
			door.DoorbellRequestID = ""
			door.DoorbellDeviceID = ""
			door.DoorbellRoomID = ""
			door.DoorbellChannel = ""
			matchedDoor = door
			break
		}
	}
	c.mu.Unlock()

	if matchedDoor != nil {
		logger.Info("Doorbell call ended on", matchedDoor.Name)
		if c.OnDoorbellCancel != nil {
			c.OnDoorbellCancel(matchedDoor)
		}
	}
}

// handleDeviceUpdate handles device update events
func (c *Controller) handleDeviceUpdate(event EventPacket) {
	if event.EventObjectID == "" {
		return
	}

	c.mu.Lock()
	door := c.doors[event.EventObjectID]
	if door == nil {
		c.mu.Unlock()
		return
	}

	// Update device config from event data
	if event.Data != nil {
		if configs, ok := event.Data["configs"].([]interface{}); ok {
			for _, cfg := range configs {
				if cfgMap, ok := cfg.(map[string]interface{}); ok {
					key, _ := cfgMap["key"].(string)
					value, _ := cfgMap["value"].(string)
					if key != "" {
						// Update in device config
						found := false
						for i := range door.Device.Configs {
							if door.Device.Configs[i].Key == key {
								door.Device.Configs[i].Value = value
								found = true
								break
							}
						}
						if !found {
							door.Device.Configs = append(door.Device.Configs, ConfigEntry{Key: key, Value: value})
						}
					}
				}
			}
		}

		// Update lock status
		newLockStatus := c.getLockStatusFromDevice(door.Device)
		if newLockStatus != door.LockStatus {
			door.LockStatus = newLockStatus
			logger.Info("Door", door.Name, "lock status changed to", door.LockStatus)
		}

		// Update online status
		if isOnline, ok := event.Data["is_online"].(bool); ok {
			door.IsOnline = isOnline
		}
	}
	c.mu.Unlock()

	if c.OnDoorUpdate != nil {
		c.OnDoorUpdate(door)
	}
}

// handleDeviceUpdateV2 handles v2 device update events
func (c *Controller) handleDeviceUpdateV2(event EventPacket) {
	// Get device ID from meta if available
	deviceID := event.EventObjectID
	if event.Meta != nil && event.Meta.ID != "" {
		deviceID = event.Meta.ID
	}

	if deviceID == "" {
		logger.Debug("handleDeviceUpdateV2: no device ID found")
		return
	}

	logger.Debug("handleDeviceUpdateV2: device ID =", deviceID)

	// Check for location_states in the event (for UGT devices)
	states := ParseLocationStates(event)
	logger.Debug("handleDeviceUpdateV2: location_states count =", strconv.Itoa(len(states)))

	c.mu.Lock()
	door := c.doors[deviceID]
	c.mu.Unlock()

	if door == nil {
		// Check if this is a viewer device - if so, skip silently
		c.mu.RLock()
		isViewer := c.viewers[deviceID]
		c.mu.RUnlock()
		if isViewer {
			// Viewer updates are expected and can be ignored
			return
		}

		logger.Debug("handleDeviceUpdateV2: door not found for ID", deviceID, "- known doors:", strconv.Itoa(len(c.doors)))
		// Fall back to regular device update handling
		c.handleDeviceUpdate(event)
		return
	}

	logger.Debug("handleDeviceUpdateV2: found door", door.Name)

	if len(states) > 0 {
		c.mu.Lock()
		for _, state := range states {
			logger.Debug("handleDeviceUpdateV2: state lock=", state.Lock, "dps=", state.DPS)
			if state.Lock == "unlocked" {
				door.LockStatus = "unlocked"
			} else if state.Lock == "locked" {
				door.LockStatus = "locked"
			}
			if state.DPS == "open" {
				door.DoorStatus = "open"
			} else if state.DPS == "close" {
				door.DoorStatus = "closed"
			}
		}
		c.mu.Unlock()

		logger.Debug("handleDeviceUpdateV2: door updated, calling OnDoorUpdate")
		if c.OnDoorUpdate != nil {
			c.OnDoorUpdate(door)
		}
		return
	}

	// Fall back to regular device update handling
	c.handleDeviceUpdate(event)
}

// handleRemoteUnlock handles remote unlock events
func (c *Controller) handleRemoteUnlock(event EventPacket) {
	if event.EventObjectID == "" {
		return
	}

	c.mu.Lock()
	door := c.doors[event.EventObjectID]
	if door != nil {
		door.LockStatus = "unlocked"
	}
	c.mu.Unlock()

	if door != nil {
		logger.Info("Door", door.Name, "unlocked remotely")
		if c.OnDoorUpdate != nil {
			c.OnDoorUpdate(door)
		}
	}
}

// handleLocationUpdate handles location update events
func (c *Controller) handleLocationUpdate(event EventPacket) {
	if event.Data == nil {
		return
	}

	// Get state from event data
	lockStatus, _ := event.Data["door_lock_relay_status"].(string)
	doorStatus, _ := event.Data["door_position_status"].(string)
	doorID, _ := event.Data["unique_id"].(string)

	if doorID == "" {
		doorID = event.EventObjectID
	}

	// Find the door by looking at all doors' associated door configs
	c.mu.Lock()
	var matchedDoor *Door
	for _, door := range c.doors {
		if door.Device.Door != nil && door.Device.Door.UniqueID == doorID {
			matchedDoor = door
			break
		}
	}

	if matchedDoor != nil {
		if lockStatus == "unlock" {
			matchedDoor.LockStatus = "unlocked"
		} else if lockStatus == "lock" {
			matchedDoor.LockStatus = "locked"
		}
		if doorStatus == "open" {
			matchedDoor.DoorStatus = "open"
		} else if doorStatus == "close" {
			matchedDoor.DoorStatus = "closed"
		}
	}
	c.mu.Unlock()

	if matchedDoor != nil && c.OnDoorUpdate != nil {
		c.OnDoorUpdate(matchedDoor)
	}
}

// SetDoorbellConfig sets the doorbell configuration from config file
func (c *Controller) SetDoorbellConfig(sourceReader string, targetViewers []string) {
	c.mu.Lock()
	defer c.mu.Unlock()

	c.doorbellConfig = &DoorbellConfig{
		SourceReader:  sourceReader,
		TargetViewers: targetViewers,
	}
	logger.Info("Doorbell config set: sourceReader=", sourceReader, "targetViewers=", targetViewers)
}

// SanitizeName sanitizes a door name for use in MQTT topics
func SanitizeName(name string) string {
	// Replace spaces and special characters
	name = strings.ReplaceAll(name, " ", "-")
	name = strings.ReplaceAll(name, "/", "-")
	name = strings.ToLower(name)
	return name
}
