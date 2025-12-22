package unifi

import (
	"bytes"
	"crypto/tls"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/http/cookiejar"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/philipparndt/go-logger"
)

// Client represents the UniFi Access API client
type Client struct {
	host       string
	username   string
	password   string
	verifySSL  bool
	httpClient *http.Client
	csrfToken  string
	userID     string
	userName   string
	mu         sync.RWMutex
}

// NewClient creates a new UniFi Access API client
func NewClient(host, username, password string, verifySSL bool) *Client {
	jar, _ := cookiejar.New(nil)

	transport := &http.Transport{
		TLSClientConfig: &tls.Config{
			InsecureSkipVerify: !verifySSL,
		},
	}

	return &Client{
		host:      strings.TrimSuffix(host, "/"),
		username:  username,
		password:  password,
		verifySSL: verifySSL,
		httpClient: &http.Client{
			Jar:       jar,
			Transport: transport,
			Timeout:   30 * time.Second,
		},
	}
}

// Login authenticates with the UniFi Access controller
func (c *Client) Login() error {
	c.mu.Lock()
	defer c.mu.Unlock()

	// Step 1: Get initial CSRF token by making a request to the base URL
	if err := c.acquireCSRFToken(); err != nil {
		logger.Warn("Failed to acquire initial CSRF token:", err)
		// Continue anyway, some controllers might not require this
	}

	// Step 2: Perform login
	url := fmt.Sprintf("%s/api/auth/login", c.host)

	payload := map[string]interface{}{
		"username":   c.username,
		"password":   c.password,
		"token":      "",
		"rememberMe": true,
	}

	body, err := json.Marshal(payload)
	if err != nil {
		return fmt.Errorf("failed to marshal login payload: %w", err)
	}

	req, err := http.NewRequest("POST", url, bytes.NewBuffer(body))
	if err != nil {
		return fmt.Errorf("failed to create login request: %w", err)
	}

	req.Header.Set("Content-Type", "application/json")
	if c.csrfToken != "" {
		req.Header.Set("X-Csrf-Token", c.csrfToken)
	}

	logger.Debug("Attempting login to", url)

	resp, err := c.httpClient.Do(req)
	if err != nil {
		return fmt.Errorf("login request failed: %w", err)
	}
	defer resp.Body.Close()

	// Log response headers for debugging
	logger.Debug("Login response status:", resp.StatusCode)
	for name, values := range resp.Header {
		for _, value := range values {
			logger.Debug("  Response header:", name, "=", value)
		}
	}

	if resp.StatusCode != http.StatusOK {
		bodyBytes, _ := io.ReadAll(resp.Body)
		return fmt.Errorf("login failed with status %d: %s", resp.StatusCode, string(bodyBytes))
	}

	// Extract CSRF token from response header
	if token := resp.Header.Get("X-Updated-Csrf-Token"); token != "" {
		c.csrfToken = token
		logger.Debug("Got CSRF token from X-Updated-Csrf-Token")
	} else if token := resp.Header.Get("X-Csrf-Token"); token != "" {
		c.csrfToken = token
		logger.Debug("Got CSRF token from X-Csrf-Token")
	}

	// Log cookies after login and extract user info from JWT
	cookies := c.httpClient.Jar.Cookies(req.URL)
	logger.Debug("Cookies after login:", strconv.Itoa(len(cookies)))
	for _, cookie := range cookies {
		logger.Debug("  Cookie:", cookie.Name)
		if cookie.Name == "TOKEN" {
			c.extractUserFromJWT(cookie.Value)
		}
	}

	// Use username as display name if not extracted from JWT
	if c.userName == "" {
		c.userName = c.username
	}

	logger.Info("Successfully logged in to UniFi Access controller")
	return nil
}

// acquireCSRFToken gets the initial CSRF token from the controller
func (c *Client) acquireCSRFToken() error {
	req, err := http.NewRequest("GET", c.host, nil)
	if err != nil {
		return err
	}

	resp, err := c.httpClient.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()

	// Log all headers for debugging
	logger.Debug("Response headers from", c.host)
	for name, values := range resp.Header {
		for _, value := range values {
			logger.Debug("  Header:", name, "=", value)
		}
	}

	// Try different possible header names (case-insensitive in Go's http.Header)
	token := resp.Header.Get("X-Csrf-Token")
	if token == "" {
		token = resp.Header.Get("X-CSRF-Token")
	}
	if token == "" {
		token = resp.Header.Get("Csrf-Token")
	}

	if token != "" {
		c.csrfToken = token
		logger.Debug("Acquired CSRF token")
	} else {
		logger.Debug("No CSRF token found in response headers")
	}

	return nil
}

// extractUserFromJWT extracts user information from the JWT token
func (c *Client) extractUserFromJWT(token string) {
	// JWT format: header.payload.signature
	parts := strings.Split(token, ".")
	if len(parts) != 3 {
		logger.Debug("Invalid JWT token format")
		return
	}

	// Decode payload (second part)
	payload, err := base64.RawURLEncoding.DecodeString(parts[1])
	if err != nil {
		// Try standard encoding with padding
		payload, err = base64.StdEncoding.DecodeString(parts[1])
		if err != nil {
			logger.Debug("Failed to decode JWT payload:", err)
			return
		}
	}

	// Parse JSON payload
	var claims map[string]interface{}
	if err := json.Unmarshal(payload, &claims); err != nil {
		logger.Debug("Failed to parse JWT payload:", err)
		return
	}

	// Extract userId
	if userID, ok := claims["userId"].(string); ok {
		c.userID = userID
		logger.Debug("Extracted user ID from JWT:", userID)
	}
}

// GetUserID returns the logged-in user's ID
func (c *Client) GetUserID() string {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.userID
}

// GetUserName returns the logged-in user's name
func (c *Client) GetUserName() string {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return c.userName
}

// Bootstrap retrieves the initial configuration from the controller
func (c *Client) Bootstrap() (*BootstrapResponse, error) {
	url := c.getAccessAPIURL("/devices/topology4")
	logger.Debug("Bootstrap URL:", url)

	data, err := c.get(url)
	if err != nil {
		return nil, fmt.Errorf("bootstrap request failed: %w", err)
	}

	// Log more of the response for debugging
	logger.Debug("Full topology response length:", strconv.Itoa(len(data)))
	if len(data) > 2000 {
		logger.Debug("Response excerpt:", string(data[:2000]))
	}

	// Parse topology response
	var topology TopologyResponse
	if err := json.Unmarshal(data, &topology); err != nil {
		return nil, fmt.Errorf("failed to parse topology response: %w", err)
	}

	logger.Debug("Topology: parsed", strconv.Itoa(len(topology.Data)), "buildings")

	// Extract devices and doors from the hierarchical topology
	response := &BootstrapResponse{
		Devices: []DeviceConfig{},
		Doors:   []DoorConfig{},
		Viewers: []DeviceConfig{},
	}

	// Get controller info
	if len(topology.Data) > 0 {
		response.Host.Name = topology.Data[0].Name
	}

	// Traverse: buildings -> floors -> doors -> device_groups (devices)
	for _, building := range topology.Data {
		// Extract Viewers from building-level device_groups
		// Viewers are associated with the building, not specific doors
		for _, group := range building.DeviceGroups {
			for i := range group {
				device := group[i]
				if device.IsViewer() {
					response.Viewers = append(response.Viewers, device)
					logger.Debug("Found Viewer at building level:", device.GetID(), device.Name)
				}
			}
		}

		for _, floor := range building.Floors {
			for _, door := range floor.Doors {
				// Add door to list
				doorConfig := DoorConfig{
					UniqueID:            door.UniqueID,
					Name:                door.Name,
					DoorPositionStatus:  door.DoorPositionStatus,
					DoorLockRelayStatus: door.DoorLockRelayStatus,
				}
				response.Doors = append(response.Doors, doorConfig)

				// Extract devices from device_groups (array of arrays)
				for _, group := range door.DeviceGroups {
					for i := range group {
						device := group[i]
						// Associate device with door
						device.Door = &DoorReference{
							UniqueID: door.UniqueID,
							Name:     door.Name,
						}
						response.Devices = append(response.Devices, device)

						// Also track Viewers separately (door-level viewers, if any)
						if device.IsViewer() {
							response.Viewers = append(response.Viewers, device)
						}
					}
				}
			}
		}
	}

	logger.Debug("Bootstrap: extracted", strconv.Itoa(len(response.Devices)), "devices,", strconv.Itoa(len(response.Doors)), "doors,", strconv.Itoa(len(response.Viewers)), "viewers")
	return response, nil
}

// Unlock unlocks a device/door
func (c *Client) Unlock(deviceID string) error {
	url := c.getAccessAPIURL(fmt.Sprintf("/device/%s/unlock", deviceID))

	_, err := c.put(url, map[string]interface{}{})
	if err != nil {
		return fmt.Errorf("unlock request failed: %w", err)
	}

	logger.Info("Successfully unlocked device:", deviceID)
	return nil
}

// UnlockLocation unlocks a door by location ID (for UGT devices)
func (c *Client) UnlockLocation(locationID string) error {
	url := c.getAccessAPIURL(fmt.Sprintf("/location/%s/unlock", locationID))

	_, err := c.put(url, map[string]interface{}{})
	if err != nil {
		return fmt.Errorf("unlock location request failed: %w", err)
	}

	logger.Info("Successfully unlocked location:", locationID)
	return nil
}

// DismissDoorbellCall dismisses/declines an active doorbell call
func (c *Client) DismissDoorbellCall(deviceID, requestID, userID, userName string) error {
	url := c.getAccessAPIURL(fmt.Sprintf("/device/%s/reply_remote", deviceID))
	logger.Debug("Dismissing doorbell call:", url)

	payload := map[string]interface{}{
		"device_id":  deviceID,
		"response":   "denied",
		"request_id": requestID,
		"user_id":    userID,
		"user_name":  userName,
	}

	_, err := c.post(url, payload)
	if err != nil {
		return fmt.Errorf("dismiss doorbell call failed: %w", err)
	}

	logger.Info("Successfully dismissed doorbell call:", requestID)
	return nil
}

// GetWebSocketURL returns the WebSocket URL for real-time events
func (c *Client) GetWebSocketURL() string {
	host := strings.TrimPrefix(c.host, "https://")
	host = strings.TrimPrefix(host, "http://")
	return fmt.Sprintf("wss://%s/proxy/access/api/v2/ws/notification", host)
}

// DoorbellRingRequest contains the information needed to trigger a doorbell ring
type DoorbellRingRequest struct {
	DeviceID     string   // The reader/doorbell device ID
	DeviceName   string   // Name of the device (e.g., "Eingang")
	DoorName     string   // Name of the door
	FloorName    string   // Name of the floor (optional)
	ControllerID string   // Controller ID (optional, will use device ID if empty)
	InOrOut      string   // "in" or "out" (default: "in")
	ViewerIDs    []string // Viewer device IDs to notify (notify_door_guards)
}

// TriggerDoorbellRing triggers a doorbell ring via the remote_call API
// This uses the DoorbellRequestBody format that the reader uses when someone presses the button
func (c *Client) TriggerDoorbellRing(req DoorbellRingRequest) error {
	url := c.getAccessAPIURL(fmt.Sprintf("/device/%s/remote_call", req.DeviceID))
	logger.Debug("Triggering doorbell ring:", url)

	// Generate unique IDs similar to what the reader does
	roomID := fmt.Sprintf("PR-%s", generateUUID())
	requestID := generateRandomString(32)
	now := time.Now().Unix()

	// Use device ID as controller ID if not provided
	controllerID := req.ControllerID
	if controllerID == "" {
		controllerID = req.DeviceID
	}

	// Default to "in" if not specified
	inOrOut := req.InOrOut
	if inOrOut == "" {
		inOrOut = "in"
	}

	// Use provided ViewerIDs or empty slice
	viewerIDs := req.ViewerIDs
	if viewerIDs == nil {
		viewerIDs = []string{}
	}

	// DoorbellRequestBody format - this is what the reader sends when the button is pressed
	payload := map[string]interface{}{
		"request_id":         requestID,
		"agora_channel":      roomID,
		"controller_id":      controllerID,
		"device_id":          req.DeviceID,
		"device_name":        req.DeviceName,
		"door_name":          req.DoorName,
		"floor_name":         req.FloorName,
		"in_or_out":          inOrOut,
		"mode":               "webrtc",
		"create_time_uid":    now,
		"create_time":        now,
		"room_id":            roomID,
		"notify_door_guards": viewerIDs,
	}

	logger.Debug("DoorbellRequestBody payload:", payload)

	respBody, err := c.post(url, payload)
	if err != nil {
		return fmt.Errorf("remote_call request failed: %w", err)
	}

	logger.Debug("Remote call response:", string(respBody))
	return nil
}

// generateUUID generates a simple UUID v4
func generateUUID() string {
	b := make([]byte, 16)
	for i := range b {
		b[i] = byte(time.Now().UnixNano() % 256)
		time.Sleep(time.Nanosecond)
	}
	return fmt.Sprintf("%x-%x-%x-%x-%x", b[0:4], b[4:6], b[6:8], b[8:10], b[10:16])
}

// generateRandomString generates a random alphanumeric string
func generateRandomString(length int) string {
	const charset = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
	result := make([]byte, length)
	for i := range result {
		result[i] = charset[time.Now().UnixNano()%int64(len(charset))]
		time.Sleep(time.Nanosecond)
	}
	return string(result)
}

// GetCookies returns the current session cookies
func (c *Client) GetCookies() []*http.Cookie {
	c.mu.RLock()
	defer c.mu.RUnlock()

	if c.httpClient.Jar == nil {
		return nil
	}

	// Parse the host URL to get cookies
	req, _ := http.NewRequest("GET", c.host, nil)
	return c.httpClient.Jar.Cookies(req.URL)
}

// getAccessAPIURL constructs the full Access API URL (v2)
func (c *Client) getAccessAPIURL(path string) string {
	return fmt.Sprintf("%s/proxy/access/api/v2%s", c.host, path)
}

// get performs a GET request
func (c *Client) get(url string) ([]byte, error) {
	req, err := http.NewRequest("GET", url, nil)
	if err != nil {
		return nil, err
	}

	return c.doRequest(req)
}

// put performs a PUT request
func (c *Client) put(url string, payload interface{}) ([]byte, error) {
	var body io.Reader
	if payload != nil {
		data, err := json.Marshal(payload)
		if err != nil {
			return nil, err
		}
		body = bytes.NewBuffer(data)
	} else {
		body = bytes.NewBuffer([]byte("{}"))
	}

	req, err := http.NewRequest("PUT", url, body)
	if err != nil {
		return nil, err
	}

	req.Header.Set("Content-Type", "application/json")

	return c.doRequest(req)
}

// post performs a POST request
func (c *Client) post(url string, payload interface{}) ([]byte, error) {
	var body io.Reader
	if payload != nil {
		data, err := json.Marshal(payload)
		if err != nil {
			return nil, err
		}
		body = bytes.NewBuffer(data)
	}

	req, err := http.NewRequest("POST", url, body)
	if err != nil {
		return nil, err
	}

	req.Header.Set("Content-Type", "application/json")

	return c.doRequest(req)
}

// doRequest performs an HTTP request with proper headers
func (c *Client) doRequest(req *http.Request) ([]byte, error) {
	c.mu.RLock()
	csrfToken := c.csrfToken
	c.mu.RUnlock()

	if csrfToken != "" {
		req.Header.Set("X-Csrf-Token", csrfToken)
	}

	resp, err := c.httpClient.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	// Update CSRF token if present in response
	if newToken := resp.Header.Get("X-Updated-Csrf-Token"); newToken != "" {
		c.mu.Lock()
		c.csrfToken = newToken
		c.mu.Unlock()
	} else if newToken := resp.Header.Get("X-Csrf-Token"); newToken != "" {
		c.mu.Lock()
		c.csrfToken = newToken
		c.mu.Unlock()
	}

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, err
	}

	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return nil, fmt.Errorf("request failed with status %d: %s", resp.StatusCode, string(body))
	}

	return body, nil
}
