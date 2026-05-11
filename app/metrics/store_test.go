package metrics

import (
	"testing"
	"time"
)

func TestViewerWakeCounts(t *testing.T) {
	s := New()
	for range 3 {
		s.RecordViewerWake("viewer-a")
	}
	s.RecordViewerWake("viewer-b")

	snap := s.Snapshot()
	if got := snap.ViewerWakes["viewer-a"].SinceRestart; got != 3 {
		t.Fatalf("viewer-a SinceRestart = %d, want 3", got)
	}
	if got := snap.ViewerWakes["viewer-a"].Last1h; got != 3 {
		t.Fatalf("viewer-a Last1h = %d, want 3", got)
	}
	if got := snap.ViewerWakesTotal.SinceRestart; got != 4 {
		t.Fatalf("total SinceRestart = %d, want 4", got)
	}
}

func TestDoorbellMissedWhenNotHandled(t *testing.T) {
	s := New()
	s.RecordDoorbellRing("door1", "Front")
	s.RecordDoorbellCancel("door1")

	snap := s.Snapshot()
	if got := snap.DoorbellMissed["Front"].SinceRestart; got != 1 {
		t.Fatalf("missed = %d, want 1", got)
	}
	if got := snap.DoorbellRings["Front"].SinceRestart; got != 1 {
		t.Fatalf("rings = %d, want 1", got)
	}
}

func TestDoorbellNotMissedWhenHandled(t *testing.T) {
	s := New()
	s.RecordDoorbellRing("door1", "Front")
	s.MarkDoorbellHandled("door1")
	s.RecordDoorbellCancel("door1")

	snap := s.Snapshot()
	if got := snap.DoorbellMissed["Front"].SinceRestart; got != 0 {
		t.Fatalf("missed = %d, want 0 (handled)", got)
	}
	if got := snap.DoorbellRings["Front"].SinceRestart; got != 1 {
		t.Fatalf("rings = %d, want 1", got)
	}
}

func TestMarkHandledWithoutRingIsNoop(t *testing.T) {
	s := New()
	s.MarkDoorbellHandled("nonexistent") // must not panic
	s.RecordDoorbellCancel("nonexistent")
	snap := s.Snapshot()
	if snap.DoorbellMissedTotal.SinceRestart != 0 {
		t.Fatalf("missed total = %d, want 0", snap.DoorbellMissedTotal.SinceRestart)
	}
}

func TestCountAfter(t *testing.T) {
	now := time.Date(2026, 5, 11, 12, 0, 0, 0, time.UTC)
	ts := []time.Time{
		now.Add(-8 * 24 * time.Hour),    // older than 7d
		now.Add(-25 * time.Hour),        // older than 24h, in 7d
		now.Add(-30 * time.Minute),      // in 1h
		now.Add(-5 * time.Minute),       // in 1h
	}
	if got := countAfter(ts, now.Add(-time.Hour)); got != 2 {
		t.Errorf("Last1h = %d, want 2", got)
	}
	if got := countAfter(ts, now.Add(-24*time.Hour)); got != 2 {
		t.Errorf("Last24h = %d, want 2", got)
	}
	if got := countAfter(ts, now.Add(-7*24*time.Hour)); got != 3 {
		t.Errorf("Last7d = %d, want 3", got)
	}
}

func TestPruneAndAppendDropsOldEntries(t *testing.T) {
	now := time.Now()
	ts := []time.Time{
		now.Add(-10 * 24 * time.Hour), // beyond retention
		now.Add(-1 * time.Hour),
	}
	out := pruneAndAppend(ts, now)
	if len(out) != 2 {
		t.Fatalf("len = %d, want 2 (old dropped, new appended)", len(out))
	}
	if out[0].Equal(ts[0]) {
		t.Fatalf("retention not enforced; old entry still present")
	}
}
