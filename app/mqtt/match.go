package mqtt

import (
	"encoding/json"
	"strings"
)

// matchPayload returns true when the configured field/value pair matches the
// MQTT payload.
//
//   - If field is empty, the entire payload is parsed as a JSON value
//     (so `true`, `1`, `"open"` all decode naturally). If JSON parsing
//     fails, the raw trimmed string is used (handles e.g. `ON`/`OFF`).
//   - If field is set, the payload is parsed as a JSON object and the named
//     field is read.
//
// The matched value (or extracted field) is returned for logging.
func matchPayload(payload []byte, field string, expected any) (matched bool, value any, ok bool) {
	if field == "" {
		var v any
		if err := json.Unmarshal(payload, &v); err != nil {
			v = strings.TrimSpace(string(payload))
		}
		return jsonEqual(v, expected), v, true
	}

	var data map[string]any
	if err := json.Unmarshal(payload, &data); err != nil {
		return false, nil, false
	}
	v, present := data[field]
	if !present {
		return false, nil, false
	}
	return jsonEqual(v, expected), v, true
}

// jsonEqual compares two arbitrary JSON-decoded values by re-marshaling and
// string-comparing. This makes bool, string, and number comparisons uniform
// regardless of how the configured value was decoded.
func jsonEqual(a, b any) bool {
	ab, err1 := json.Marshal(a)
	bb, err2 := json.Marshal(b)
	if err1 != nil || err2 != nil {
		return false
	}
	return string(ab) == string(bb)
}
