package unifi

import "testing"

func TestParseRemoteViewMessage(t *testing.T) {
	message := buildRemoteViewMessage("rpc-test-123", "reader-abc")

	requestID, sourceReader, ok := parseRemoteViewMessage(message)
	if !ok {
		t.Fatal("expected remote_view message to parse")
	}
	if requestID != "rpc-test-123" {
		t.Fatalf("request ID = %q, want %q", requestID, "rpc-test-123")
	}
	if sourceReader != "reader-abc" {
		t.Fatalf("source reader = %q, want %q", sourceReader, "reader-abc")
	}
}

func TestParseRemoteViewMessageRejectsOtherPath(t *testing.T) {
	message := append(encodeMetaData("path", "/remote_open_door"),
		encodeMetaData("requestId", "rpc-test-123")...)

	if _, _, ok := parseRemoteViewMessage(message); ok {
		t.Fatal("expected non-remote_view message to be rejected")
	}
}

func TestParseRemoteViewMessageRejectsMissingRequestID(t *testing.T) {
	inner := buildRemoteViewPayload("", "reader-abc")
	message := append(encodeMetaData("path", "/remote_view"), 0x12)
	message = append(message, encodeProtoVarint(uint64(len(inner)))...)
	message = append(message, inner...)

	if _, _, ok := parseRemoteViewMessage(message); ok {
		t.Fatal("expected remote_view without request ID to be rejected")
	}
}
