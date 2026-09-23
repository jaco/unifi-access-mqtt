package unifi

import (
	"crypto/rand"
	"crypto/tls"
	"crypto/x509"
	"fmt"
	"os"
	"strings"
	"time"

	paho "github.com/eclipse/paho.mqtt.golang"
	"github.com/philipparndt/go-logger"
)

// Default port of the UniFi Cloud Gateway's internal MQTT broker.
const defaultViewerBrokerPort = 12812

// ViewerWaker connects to the UniFi controller's internal MQTT broker (mTLS)
// and sends remote_view RPC commands that wake viewer displays without
// triggering an audible doorbell ring on the reader.
//
// Wire format is reverse-engineered from the official client; see
// access-mqtt-trace/main.go for the original implementation.
type ViewerWaker struct {
	broker       string
	controllerID string
	caFile       string
	certFile     string
	keyFile      string
	clientID     string
	client       paho.Client

	// OnWake fires once per viewer after a successful publish.
	OnWake func(viewerID string)
	// OnRemoteView fires when the controller sends a real remote_view request
	// to a Viewer. It exposes the request ID needed by reply_remote to dismiss
	// the call later.
	OnRemoteView func(requestID, sourceReader string)
}

// NewViewerWaker constructs a waker. Connect must be called before Wake.
func NewViewerWaker(broker, controllerID, caFile, certFile, keyFile string) *ViewerWaker {
	return &ViewerWaker{
		broker:       normalizeBroker(broker),
		controllerID: controllerID,
		caFile:       caFile,
		certFile:     certFile,
		keyFile:      keyFile,
		clientID:     fmt.Sprintf("unifi-access-mqtt-%d", time.Now().UnixNano()),
	}
}

// Connect opens the mTLS MQTT connection.
func (w *ViewerWaker) Connect() error {
	tlsConfig, err := loadViewerTLS(w.caFile, w.certFile, w.keyFile)
	if err != nil {
		return err
	}

	opts := paho.NewClientOptions()
	opts.AddBroker(fmt.Sprintf("ssl://%s", w.broker))
	opts.SetClientID(w.clientID)
	opts.SetTLSConfig(tlsConfig)
	opts.SetAutoReconnect(true)
	opts.SetConnectRetry(true)
	opts.SetConnectRetryInterval(5 * time.Second)
	opts.SetOnConnectHandler(func(client paho.Client) {
		logger.Info("viewer-waker: connected", "broker", w.broker)
		w.subscribeRemoteViewRequests(client)
	})
	opts.SetConnectionLostHandler(func(_ paho.Client, err error) {
		logger.Warn("viewer-waker: connection lost", "err", err)
	})

	w.client = paho.NewClient(opts)
	token := w.client.Connect()
	if !token.WaitTimeout(10 * time.Second) {
		return fmt.Errorf("viewer-waker: connect timeout to %s", w.broker)
	}
	if err := token.Error(); err != nil {
		return fmt.Errorf("viewer-waker: connect failed: %w", err)
	}
	return nil
}

// subscribeRemoteViewRequests observes calls sent by the controller to Viewer
// devices. UniFi's Access WebSocket does not reliably expose physical button
// calls on all controller versions, while the Viewer MQTT request always
// carries the request ID required by reply_remote.
func (w *ViewerWaker) subscribeRemoteViewRequests(client paho.Client) {
	topic := fmt.Sprintf("/uctrl/%s/device/+/rpc/+/request", w.controllerID)
	token := client.Subscribe(topic, 0, func(_ paho.Client, msg paho.Message) {
		// Ignore wake requests emitted by this process itself.
		if strings.Contains(msg.Topic(), "/rpc/"+w.clientID+"/request") {
			return
		}

		requestID, sourceReader, ok := parseRemoteViewMessage(msg.Payload())
		if !ok {
			return
		}

		logger.Info("viewer-waker: observed remote_view call",
			"request_id", requestID, "source_reader", sourceReader)
		if w.OnRemoteView != nil {
			w.OnRemoteView(requestID, sourceReader)
		}
	})
	token.Wait()
	if err := token.Error(); err != nil {
		logger.Warn("viewer-waker: remote_view subscription failed", "topic", topic, "err", err)
		return
	}
	logger.Info("viewer-waker: subscribed to remote_view calls", "topic", topic)
}

// Disconnect closes the MQTT connection.
func (w *ViewerWaker) Disconnect() {
	if w.client != nil && w.client.IsConnected() {
		w.client.Disconnect(250)
	}
}

// Wake sends a remote_view RPC to each given viewer. sourceReader may be empty.
func (w *ViewerWaker) Wake(viewerIDs []string, sourceReader string) error {
	if w.client == nil || !w.client.IsConnected() {
		return fmt.Errorf("viewer-waker: not connected")
	}
	if w.controllerID == "" {
		return fmt.Errorf("viewer-waker: controllerID not configured")
	}
	if len(viewerIDs) == 0 {
		return fmt.Errorf("viewer-waker: no viewer IDs to wake")
	}

	for _, viewerID := range viewerIDs {
		requestID := generateViewerRequestID()
		topic := fmt.Sprintf("/uctrl/%s/device/%s/rpc/%s/request",
			w.controllerID, viewerID, w.clientID)
		payload := buildRemoteViewMessage(requestID, sourceReader)

		token := w.client.Publish(topic, 0, false, payload)
		token.Wait()
		if err := token.Error(); err != nil {
			logger.Warn("viewer-waker: publish failed",
				"viewer", viewerID, "err", err)
			continue
		}
		logger.Info("viewer-waker: wake sent",
			"viewer", viewerID, "request_id", requestID)
		if w.OnWake != nil {
			w.OnWake(viewerID)
		}
	}
	return nil
}

// normalizeBroker appends the default port if the broker string is host-only.
func normalizeBroker(broker string) string {
	if strings.Contains(broker, ":") {
		return broker
	}
	return fmt.Sprintf("%s:%d", broker, defaultViewerBrokerPort)
}

func loadViewerTLS(caFile, certFile, keyFile string) (*tls.Config, error) {
	caCert, err := os.ReadFile(caFile)
	if err != nil {
		return nil, fmt.Errorf("read CA: %w", err)
	}
	pool := x509.NewCertPool()
	if !pool.AppendCertsFromPEM(caCert) {
		return nil, fmt.Errorf("parse CA: invalid PEM")
	}
	cert, err := tls.LoadX509KeyPair(certFile, keyFile)
	if err != nil {
		return nil, fmt.Errorf("load client cert: %w", err)
	}
	return &tls.Config{
		RootCAs:            pool,
		Certificates:       []tls.Certificate{cert},
		InsecureSkipVerify: true, // controller uses self-signed cert for the internal broker
		MinVersion:         tls.VersionTLS12,
	}, nil
}

func generateViewerRequestID() string {
	b := make([]byte, 8)
	_, _ = rand.Read(b)
	return fmt.Sprintf("rpc-%x", b)
}

// buildRemoteViewMessage builds the outer Message wrapper containing the
// remote_view RPC.
func buildRemoteViewMessage(requestID, sourceReader string) []byte {
	inner := buildRemoteViewPayload(requestID, sourceReader)

	var buf []byte
	buf = append(buf, encodeMetaData("path", "/remote_view")...)
	buf = append(buf, encodeMetaData("requestId", requestID)...)
	if len(inner) > 0 {
		buf = append(buf, 0x12) // field 2, wire type 2 (length-delimited)
		buf = append(buf, encodeProtoVarint(uint64(len(inner)))...)
		buf = append(buf, inner...)
	}
	return buf
}

// buildRemoteViewPayload builds the DARemoteView protobuf message.
func buildRemoteViewPayload(requestID, sourceReader string) []byte {
	var buf []byte
	channelID := fmt.Sprintf("PR-%s", requestID)

	// agura_channel (field 1)
	buf = appendProtoString(buf, 0x0a, channelID)
	// device_id (field 5) - source reader
	if sourceReader != "" {
		buf = appendProtoString(buf, 0x2a, sourceReader)
	}
	// action (field 6)
	buf = appendProtoString(buf, 0x32, "ring")
	// room_id (field 9)
	buf = appendProtoString(buf, 0x4a, channelID)
	// request_id (field 10)
	buf = appendProtoString(buf, 0x52, requestID)
	return buf
}

// parseRemoteViewMessage decodes the small protobuf subset used by UniFi's
// internal MQTT RPC wrapper. The outer message contains metadata (field 1)
// and a DARemoteView payload (field 2). The inner fields used here are
// device_id (5), action (6), and request_id (10).
func parseRemoteViewMessage(message []byte) (requestID, sourceReader string, ok bool) {
	var path string
	var inner []byte
	for _, field := range parseProtoFields(message) {
		switch field.number {
		case 1:
			var key, value string
			for _, meta := range parseProtoFields(field.data) {
				switch meta.number {
				case 1:
					key = string(meta.data)
				case 2:
					value = string(meta.data)
				}
			}
			if key == "path" {
				path = value
			}
		case 2:
			inner = field.data
		}
	}
	if path != "/remote_view" || len(inner) == 0 {
		return "", "", false
	}

	var action string
	for _, field := range parseProtoFields(inner) {
		switch field.number {
		case 5:
			sourceReader = string(field.data)
		case 6:
			action = string(field.data)
		case 10:
			requestID = string(field.data)
		}
	}
	return requestID, sourceReader, action == "ring" && requestID != ""
}

type protoField struct {
	number int
	data   []byte
}

func parseProtoFields(message []byte) []protoField {
	var fields []protoField
	for offset := 0; offset < len(message); {
		key, n := decodeProtoVarint(message[offset:])
		if n == 0 {
			return fields
		}
		offset += n
		number := int(key >> 3)
		switch key & 0x7 {
		case 0:
			_, n = decodeProtoVarint(message[offset:])
			if n == 0 {
				return fields
			}
			offset += n
		case 1:
			if offset+8 > len(message) {
				return fields
			}
			offset += 8
		case 2:
			length, sizeBytes := decodeProtoVarint(message[offset:])
			if sizeBytes == 0 {
				return fields
			}
			offset += sizeBytes
			end := offset + int(length)
			if end < offset || end > len(message) {
				return fields
			}
			fields = append(fields, protoField{number: number, data: message[offset:end]})
			offset = end
		case 5:
			if offset+4 > len(message) {
				return fields
			}
			offset += 4
		default:
			return fields
		}
	}
	return fields
}

func decodeProtoVarint(data []byte) (uint64, int) {
	var value uint64
	for i, b := range data {
		if i >= 10 {
			return 0, 0
		}
		value |= uint64(b&0x7f) << (7 * i)
		if b < 0x80 {
			return value, i + 1
		}
	}
	return 0, 0
}

// encodeMetaData encodes a MetaData(key, value) submessage as Message.field 1.
func encodeMetaData(key, value string) []byte {
	var meta []byte
	meta = appendProtoString(meta, 0x0a, key)   // MetaData.key (field 1)
	meta = appendProtoString(meta, 0x12, value) // MetaData.value (field 2)

	var buf []byte
	buf = append(buf, 0x0a) // Message.metadata (field 1, repeated MetaData)
	buf = append(buf, encodeProtoVarint(uint64(len(meta)))...)
	buf = append(buf, meta...)
	return buf
}

func appendProtoString(buf []byte, tag byte, s string) []byte {
	buf = append(buf, tag)
	buf = append(buf, encodeProtoVarint(uint64(len(s)))...)
	return append(buf, []byte(s)...)
}

func encodeProtoVarint(v uint64) []byte {
	var buf []byte
	for v >= 0x80 {
		buf = append(buf, byte(v)|0x80)
		v >>= 7
	}
	return append(buf, byte(v))
}
