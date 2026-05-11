package metrics

import (
	"sort"
	"sync"
	"time"
)

// Store tracks viewer-wake and doorbell-ring events with time-windowed counts.
//
// A doorbell ring is considered "missed" when its cancel event arrives without
// any handling signal during the ring: either the door's lock transitioned to
// "unlocked" (any source: MQTT command, NFC, app, etc.) or an explicit
// DismissDoorbellCall was issued via this gateway. Rings answered solely on
// the viewer screen cannot be distinguished from missed and will be counted
// as missed.
type Store struct {
	mu        sync.Mutex
	startTime time.Time

	viewerWakes    map[string][]time.Time
	doorbellRings  map[string][]time.Time
	doorbellMissed map[string][]time.Time

	viewerWakesTotal    map[string]int
	doorbellRingsTotal  map[string]int
	doorbellMissedTotal map[string]int

	activeRings map[string]*activeRing
}

type activeRing struct {
	doorName string
	handled  bool
}

// retention bounds the per-event timestamp lists; "since restart" totals
// are kept separately as integer counters so they remain accurate past 7d.
const retention = 7 * 24 * time.Hour

func New() *Store {
	return &Store{
		startTime:           time.Now(),
		viewerWakes:         map[string][]time.Time{},
		doorbellRings:       map[string][]time.Time{},
		doorbellMissed:      map[string][]time.Time{},
		viewerWakesTotal:    map[string]int{},
		doorbellRingsTotal:  map[string]int{},
		doorbellMissedTotal: map[string]int{},
		activeRings:         map[string]*activeRing{},
	}
}

func (s *Store) RecordViewerWake(viewerID string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.viewerWakes[viewerID] = pruneAndAppend(s.viewerWakes[viewerID], time.Now())
	s.viewerWakesTotal[viewerID]++
}

func (s *Store) RecordDoorbellRing(doorID, doorName string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.doorbellRings[doorName] = pruneAndAppend(s.doorbellRings[doorName], time.Now())
	s.doorbellRingsTotal[doorName]++
	s.activeRings[doorID] = &activeRing{doorName: doorName}
}

// MarkDoorbellHandled flags the in-flight ring on this door as handled so the
// subsequent cancel does not count as missed. Safe to call when no ring is
// active.
func (s *Store) MarkDoorbellHandled(doorID string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if r, ok := s.activeRings[doorID]; ok {
		r.handled = true
	}
}

func (s *Store) RecordDoorbellCancel(doorID string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	r, ok := s.activeRings[doorID]
	if !ok {
		return
	}
	delete(s.activeRings, doorID)
	if !r.handled {
		s.doorbellMissed[r.doorName] = pruneAndAppend(s.doorbellMissed[r.doorName], time.Now())
		s.doorbellMissedTotal[r.doorName]++
	}
}

type Counts struct {
	Last1h       int `json:"last_1h"`
	Last24h      int `json:"last_24h"`
	Last7d       int `json:"last_7d"`
	SinceRestart int `json:"since_restart"`
}

type Snapshot struct {
	UptimeSeconds       int64             `json:"uptime_seconds"`
	StartTime           time.Time         `json:"start_time"`
	ViewerWakes         map[string]Counts `json:"viewer_wakes"`
	ViewerWakesTotal    Counts            `json:"viewer_wakes_total"`
	DoorbellRings       map[string]Counts `json:"doorbell_rings"`
	DoorbellRingsTotal  Counts            `json:"doorbell_rings_total"`
	DoorbellMissed      map[string]Counts `json:"doorbell_missed"`
	DoorbellMissedTotal Counts            `json:"doorbell_missed_total"`
}

func (s *Store) Snapshot() Snapshot {
	s.mu.Lock()
	defer s.mu.Unlock()
	now := time.Now()
	snap := Snapshot{
		UptimeSeconds:  int64(now.Sub(s.startTime).Seconds()),
		StartTime:      s.startTime,
		ViewerWakes:    map[string]Counts{},
		DoorbellRings:  map[string]Counts{},
		DoorbellMissed: map[string]Counts{},
	}
	snap.ViewerWakes, snap.ViewerWakesTotal = countAll(s.viewerWakes, s.viewerWakesTotal, now)
	snap.DoorbellRings, snap.DoorbellRingsTotal = countAll(s.doorbellRings, s.doorbellRingsTotal, now)
	snap.DoorbellMissed, snap.DoorbellMissedTotal = countAll(s.doorbellMissed, s.doorbellMissedTotal, now)
	return snap
}

func countAll(series map[string][]time.Time, totals map[string]int, now time.Time) (map[string]Counts, Counts) {
	perKey := make(map[string]Counts, len(series))
	var agg Counts
	for k, ts := range series {
		c := counts(ts, totals[k], now)
		perKey[k] = c
		agg.Last1h += c.Last1h
		agg.Last24h += c.Last24h
		agg.Last7d += c.Last7d
	}
	for _, t := range totals {
		agg.SinceRestart += t
	}
	return perKey, agg
}

func counts(timestamps []time.Time, sinceRestart int, now time.Time) Counts {
	return Counts{
		Last1h:       countAfter(timestamps, now.Add(-time.Hour)),
		Last24h:      countAfter(timestamps, now.Add(-24*time.Hour)),
		Last7d:       countAfter(timestamps, now.Add(-7*24*time.Hour)),
		SinceRestart: sinceRestart,
	}
}

func countAfter(timestamps []time.Time, cutoff time.Time) int {
	i := sort.Search(len(timestamps), func(i int) bool {
		return !timestamps[i].Before(cutoff)
	})
	return len(timestamps) - i
}

func pruneAndAppend(timestamps []time.Time, now time.Time) []time.Time {
	cutoff := now.Add(-retention)
	i := sort.Search(len(timestamps), func(i int) bool {
		return !timestamps[i].Before(cutoff)
	})
	if i > 0 {
		timestamps = timestamps[i:]
	}
	return append(timestamps, now)
}
