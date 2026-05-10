package unifi

import (
	"crypto/tls"
	"encoding/json"
	"fmt"
	"net/http"
	"strings"
	"sync"
	"time"

	"github.com/gorilla/websocket"
	"github.com/philipparndt/go-logger"
)

// EventHandler handles events from a specific event type
type EventHandler func(event EventPacket)

// EventListener listens for real-time events from UniFi Access
type EventListener struct {
	client       *Client
	conn         *websocket.Conn
	handlers     map[string][]EventHandler
	mu           sync.RWMutex
	stopChan     chan struct{}
	reconnecting bool
}

// NewEventListener creates a new event listener
func NewEventListener(client *Client) *EventListener {
	return &EventListener{
		client:   client,
		handlers: make(map[string][]EventHandler),
		stopChan: make(chan struct{}),
	}
}

// On registers an event handler for a specific event type
func (e *EventListener) On(eventType string, handler EventHandler) {
	e.mu.Lock()
	defer e.mu.Unlock()

	e.handlers[eventType] = append(e.handlers[eventType], handler)
}

// Start begins listening for events
func (e *EventListener) Start() error {
	return e.connect()
}

// Stop stops the event listener
func (e *EventListener) Stop() {
	close(e.stopChan)
	if e.conn != nil {
		e.conn.Close()
	}
}

// connect establishes the WebSocket connection
func (e *EventListener) connect() error {
	wsURL := e.client.GetWebSocketURL()

	dialer := websocket.Dialer{
		TLSClientConfig: &tls.Config{
			InsecureSkipVerify: !e.client.verifySSL,
		},
		HandshakeTimeout: 30 * time.Second,
	}

	// Build cookie header from client's session
	cookies := e.client.GetCookies()
	cookieHeader := ""
	for i, cookie := range cookies {
		if i > 0 {
			cookieHeader += "; "
		}
		cookieHeader += cookie.Name + "=" + cookie.Value
	}

	headers := http.Header{}
	if cookieHeader != "" {
		headers.Set("Cookie", cookieHeader)
	}

	logger.Debug("Connecting to WebSocket", "url", wsURL)
	logger.Trace("WebSocket cookies", "header", cookieHeader)
	if cookieHeader == "" {
		logger.Warn("No cookies found for WebSocket connection - events may not work")
	}

	conn, resp, err := dialer.Dial(wsURL, headers)
	if err != nil {
		if resp != nil {
			logger.Error("WebSocket connection failed", "status", resp.StatusCode)
		}
		return err
	}

	e.conn = conn
	logger.Info("Connected to UniFi Access WebSocket for real-time events")

	go e.readLoop()
	go e.pingLoop()

	return nil
}

// readLoop reads messages from the WebSocket
func (e *EventListener) readLoop() {
	defer func() {
		if e.conn != nil {
			e.conn.Close()
		}
		e.scheduleReconnect()
	}()

	for {
		select {
		case <-e.stopChan:
			return
		default:
			_, message, err := e.conn.ReadMessage()
			if err != nil {
				if websocket.IsUnexpectedCloseError(err, websocket.CloseGoingAway, websocket.CloseAbnormalClosure) {
					logger.Error("WebSocket read error", "err", err)
				}
				return
			}

			e.handleMessage(message)
		}
	}
}

// pingLoop sends periodic pings to keep the connection alive
func (e *EventListener) pingLoop() {
	ticker := time.NewTicker(30 * time.Second)
	defer ticker.Stop()

	for {
		select {
		case <-e.stopChan:
			return
		case <-ticker.C:
			if e.conn != nil {
				if err := e.conn.WriteMessage(websocket.PingMessage, nil); err != nil {
					logger.Debug("Ping failed", "err", err)
					return
				}
			}
		}
	}
}

// handleMessage processes incoming WebSocket messages
func (e *EventListener) handleMessage(message []byte) {
	msgStr := strings.TrimSpace(string(message))

	// Skip empty messages
	if len(msgStr) == 0 {
		return
	}

	// Skip heartbeat messages (just "Hello" every 5 seconds)
	// Handle both "Hello" (quoted string) and variations
	if msgStr == `"Hello"` || strings.Contains(msgStr, "Hello") && len(msgStr) < 20 {
		logger.Trace("Heartbeat received")
		return
	}

	// Full payload only at trace level
	logger.Trace("WebSocket message", "msg", msgStr)

	// Enhanced logging for doorbell-related events (for reverse engineering)
	if strings.Contains(msgStr, "remote_view") || strings.Contains(msgStr, "doorbell") || strings.Contains(msgStr, "remote_call") {
		logger.Trace("Doorbell event payload", "msg", msgStr)
	}

	// Skip non-JSON object messages
	if !strings.HasPrefix(msgStr, "{") {
		logger.Debug("Ignoring non-JSON WebSocket message")
		logger.Trace("non-JSON WebSocket payload", "msg", msgStr)
		return
	}

	var event EventPacket
	if err := json.Unmarshal(message, &event); err != nil {
		logger.Debug("Failed to parse WebSocket message", "err", err)
		logger.Trace("Unparsable WebSocket payload", "raw", msgStr)
		return
	}

	logger.Debug("Received event", "event", event.Event, "object", event.EventObjectID)
	event.Timestamp = time.Now()

	e.dispatchEvent(event)
}

// dispatchEvent dispatches an event to registered handlers
func (e *EventListener) dispatchEvent(event EventPacket) {
	e.mu.RLock()
	defer e.mu.RUnlock()

	// Call handlers for specific event type
	if handlers, ok := e.handlers[event.Event]; ok {
		for _, handler := range handlers {
			go handler(event)
		}
	}

	// Call handlers registered for all events (wildcard)
	if handlers, ok := e.handlers["*"]; ok {
		for _, handler := range handlers {
			go handler(event)
		}
	}

	// Also dispatch by event object ID for device-specific handlers
	if event.EventObjectID != "" {
		key := event.Event + "." + event.EventObjectID
		if handlers, ok := e.handlers[key]; ok {
			for _, handler := range handlers {
				go handler(event)
			}
		}
	}
}

// scheduleReconnect schedules a reconnection attempt
func (e *EventListener) scheduleReconnect() {
	select {
	case <-e.stopChan:
		return
	default:
	}

	if e.reconnecting {
		return
	}
	e.reconnecting = true

	go func() {
		defer func() { e.reconnecting = false }()

		for {
			select {
			case <-e.stopChan:
				return
			case <-time.After(5 * time.Second):
				logger.Info("Attempting to reconnect WebSocket...")

				// Re-login before reconnecting
				if err := e.client.Login(); err != nil {
					logger.Error("Failed to re-login", "err", err)
					continue
				}

				if err := e.connect(); err != nil {
					logger.Error("Failed to reconnect WebSocket", "err", err)
					continue
				}

				return
			}
		}
	}()
}

// ParseDoorbellRingData extracts doorbell ring data from an event
func ParseDoorbellRingData(event EventPacket) *DoorbellRingData {
	if event.Data == nil {
		return nil
	}

	requestID, _ := event.Data["request_id"].(string)
	connectedUAHID, _ := event.Data["connected_uah_id"].(string)
	deviceID, _ := event.Data["device_id"].(string)
	roomID, _ := event.Data["room_id"].(string)
	doorbellChannel, _ := event.Data["doorbell_channel"].(string)

	if requestID == "" {
		return nil
	}

	return &DoorbellRingData{
		RequestID:       requestID,
		ConnectedUAHID:  connectedUAHID,
		DeviceID:        deviceID,
		RoomID:          roomID,
		DoorbellChannel: doorbellChannel,
	}
}

// ParseDoorbellCancelData extracts doorbell cancel data from an event
func ParseDoorbellCancelData(event EventPacket) *DoorbellCancelData {
	if event.Data == nil {
		return nil
	}

	remoteCallRequestID, _ := event.Data["remote_call_request_id"].(string)

	if remoteCallRequestID == "" {
		return nil
	}

	return &DoorbellCancelData{
		RemoteCallRequestID: remoteCallRequestID,
	}
}

// ParseLocationStates extracts location states from a v2 device update event
func ParseLocationStates(event EventPacket) []LocationState {
	if event.Data == nil {
		return nil
	}

	locationStatesRaw, ok := event.Data["location_states"].([]interface{})
	if !ok {
		return nil
	}

	var states []LocationState
	for _, ls := range locationStatesRaw {
		lsMap, ok := ls.(map[string]interface{})
		if !ok {
			continue
		}

		state := LocationState{}
		if id, ok := lsMap["location_id"].(string); ok {
			state.LocationID = id
		}
		if lock, ok := lsMap["lock"].(string); ok {
			state.Lock = lock
		}
		if dps, ok := lsMap["dps"].(string); ok {
			state.DPS = dps
		}

		states = append(states, state)
	}

	return states
}

// NormalizeDoorName normalizes a door name for comparison
func NormalizeDoorName(name string) string {
	return strings.ToLower(strings.TrimSpace(name))
}

// fmtInt formats an integer for logging
func fmtInt(n int) string {
	return fmt.Sprintf("%d", n)
}
