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
	opts.SetOnConnectHandler(func(_ paho.Client) {
		logger.Info("viewer-waker: connected", "broker", w.broker)
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
