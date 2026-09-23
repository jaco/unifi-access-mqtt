package unifi

import (
	"encoding/json"
	"testing"
)

func TestEventPacketDecodesObjectData(t *testing.T) {
	var event EventPacket
	if err := json.Unmarshal([]byte(`{"event":"access.remote_view","data":{"request_id":"req-1","device_id":"reader-1"}}`), &event); err != nil {
		t.Fatal(err)
	}
	data := ParseDoorbellRingData(event)
	if data == nil || data.RequestID != "req-1" || data.DeviceID != "reader-1" {
		t.Fatalf("unexpected parsed data: %#v", data)
	}
}

func TestEventPacketDecodesJSONStringData(t *testing.T) {
	var event EventPacket
	if err := json.Unmarshal([]byte(`{"event":"access.remote_view","data":"{\"request_id\":\"req-2\",\"device_id\":\"reader-2\"}"}`), &event); err != nil {
		t.Fatal(err)
	}
	data := ParseDoorbellRingData(event)
	if data == nil || data.RequestID != "req-2" || data.DeviceID != "reader-2" {
		t.Fatalf("unexpected parsed data: %#v", data)
	}
}
