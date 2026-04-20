package main

import (
	"bytes"
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"
)

func testStore(t *testing.T) *store {
	t.Helper()
	s := newStore(config{
		bind:        ":0",
		maxSessions: 1000,
		ttl:         90 * time.Second,
		rateBurst:   10_000,
		rateRefill:  time.Second,
	})
	return s
}

func do(t *testing.T, h http.Handler, method, path string, body any, headers map[string]string) *httptest.ResponseRecorder {
	t.Helper()
	var r io.Reader
	if body != nil {
		b, err := json.Marshal(body)
		if err != nil {
			t.Fatalf("marshal: %v", err)
		}
		r = bytes.NewReader(b)
	}
	req := httptest.NewRequest(method, path, r)
	req.RemoteAddr = "127.0.0.1:1234"
	for k, v := range headers {
		req.Header.Set(k, v)
	}
	w := httptest.NewRecorder()
	h.ServeHTTP(w, req)
	return w
}

func mustCreate(t *testing.T, h http.Handler, cart, handle, endpoint string) (string, string) {
	t.Helper()
	w := do(t, h, "POST", "/v1/session", createReq{
		CartName:        cart,
		HostHandle:      handle,
		HostEndpoint:    endpoint,
		Region:          "eu",
		PlayerCount:     1,
		MaxPlayers:      2,
		ProtocolVersion: 1,
	}, nil)
	if w.Code != http.StatusCreated {
		t.Fatalf("create: %d body=%s", w.Code, w.Body.String())
	}
	var resp createResp
	if err := json.Unmarshal(w.Body.Bytes(), &resp); err != nil {
		t.Fatalf("decode: %v", err)
	}
	return resp.SessionID, resp.Token
}

func TestHealthz(t *testing.T) {
	s := testStore(t)
	w := do(t, s.routes(), "GET", "/healthz", nil, nil)
	// "ok sessions=N" — suffix reports the live session count and
	// also exercises the store mutex, so a deadlocked store fails
	// its health check instead of falsely reporting "ok".
	if w.Code != 200 ||
		!strings.HasPrefix(w.Body.String(), "ok sessions=") {
		t.Fatalf("healthz: %d %q", w.Code, w.Body.String())
	}
}

func TestCreateSession_Validation(t *testing.T) {
	cases := []struct {
		name string
		req  createReq
		code int
	}{
		{"empty cart", createReq{CartName: "", HostHandle: "a", HostEndpoint: "1.2.3.4:1", MaxPlayers: 2, PlayerCount: 1, ProtocolVersion: 1}, 400},
		{"long cart", createReq{CartName: strings.Repeat("x", 65), HostHandle: "a", HostEndpoint: "1.2.3.4:1", MaxPlayers: 2, PlayerCount: 1, ProtocolVersion: 1}, 400},
		{"long handle", createReq{CartName: "c", HostHandle: strings.Repeat("x", 33), HostEndpoint: "1.2.3.4:1", MaxPlayers: 2, PlayerCount: 1, ProtocolVersion: 1}, 400},
		{"bad endpoint", createReq{CartName: "c", HostHandle: "a", HostEndpoint: "nope", MaxPlayers: 2, PlayerCount: 1, ProtocolVersion: 1}, 400},
		{"bad maxplayers", createReq{CartName: "c", HostHandle: "a", HostEndpoint: "1.2.3.4:1", MaxPlayers: 1, PlayerCount: 1, ProtocolVersion: 1}, 400},
		{"bad playercount", createReq{CartName: "c", HostHandle: "a", HostEndpoint: "1.2.3.4:1", MaxPlayers: 2, PlayerCount: 3, ProtocolVersion: 1}, 400},
		{"no protocol", createReq{CartName: "c", HostHandle: "a", HostEndpoint: "1.2.3.4:1", MaxPlayers: 2, PlayerCount: 1}, 400},
		{"bad visibility", createReq{CartName: "c", HostHandle: "a", HostEndpoint: "1.2.3.4:1", MaxPlayers: 2, PlayerCount: 1, ProtocolVersion: 1, Visibility: "friends-only"}, 400},
		{"requiresCode without private", createReq{CartName: "c", HostHandle: "a", HostEndpoint: "1.2.3.4:1", MaxPlayers: 2, PlayerCount: 1, ProtocolVersion: 1, Visibility: "public", RequiresCode: true}, 400},
		{"private without requiresCode", createReq{CartName: "c", HostHandle: "a", HostEndpoint: "1.2.3.4:1", MaxPlayers: 2, PlayerCount: 1, ProtocolVersion: 1, Visibility: "private", RequiresCode: false}, 400},
		{"non-hex art hash", createReq{CartName: "c", HostHandle: "a", HostEndpoint: "1.2.3.4:1", MaxPlayers: 2, PlayerCount: 1, ProtocolVersion: 1, CartArtHash: "zzz"}, 400},
		{"long art hash", createReq{CartName: "c", HostHandle: "a", HostEndpoint: "1.2.3.4:1", MaxPlayers: 2, PlayerCount: 1, ProtocolVersion: 1, CartArtHash: strings.Repeat("a", 65)}, 400},
		{"ok", createReq{CartName: "Joust", HostHandle: "alice", HostEndpoint: "1.2.3.4:26100", Region: "eu", MaxPlayers: 2, PlayerCount: 1, ProtocolVersion: 1}, 201},
		{"ok public explicit", createReq{CartName: "Joust", HostHandle: "alice", HostEndpoint: "1.2.3.4:26100", Region: "eu", MaxPlayers: 2, PlayerCount: 1, ProtocolVersion: 1, Visibility: "public", CartArtHash: "ABCDEF0123456789"}, 201},
		{"ok private with code", createReq{CartName: "Joust", HostHandle: "alice", HostEndpoint: "1.2.3.4:26100", Region: "eu", MaxPlayers: 2, PlayerCount: 1, ProtocolVersion: 1, Visibility: "private", RequiresCode: true}, 201},
	}
	h := testStore(t).routes()
	for _, c := range cases {
		t.Run(c.name, func(t *testing.T) {
			w := do(t, h, "POST", "/v1/session", c.req, nil)
			if w.Code != c.code {
				t.Fatalf("want %d got %d body=%s", c.code, w.Code, w.Body.String())
			}
		})
	}
}

func TestCreate_MalformedJSON(t *testing.T) {
	h := testStore(t).routes()
	req := httptest.NewRequest("POST", "/v1/session", strings.NewReader("{not json"))
	req.RemoteAddr = "127.0.0.1:1"
	w := httptest.NewRecorder()
	h.ServeHTTP(w, req)
	if w.Code != 400 {
		t.Fatalf("want 400 got %d", w.Code)
	}
}

func TestCreate_UnknownField(t *testing.T) {
	h := testStore(t).routes()
	req := httptest.NewRequest("POST", "/v1/session", strings.NewReader(`{"cartName":"c","extra":1}`))
	req.RemoteAddr = "127.0.0.1:1"
	w := httptest.NewRecorder()
	h.ServeHTTP(w, req)
	if w.Code != 400 {
		t.Fatalf("want 400 got %d", w.Code)
	}
}

func TestListAndGet(t *testing.T) {
	s := testStore(t)
	h := s.routes()
	id, _ := mustCreate(t, h, "Joust", "alice", "1.2.3.4:1")

	// list
	w := do(t, h, "GET", "/v1/sessions", nil, nil)
	if w.Code != 200 {
		t.Fatalf("list: %d", w.Code)
	}
	var list []Session
	if err := json.Unmarshal(w.Body.Bytes(), &list); err != nil {
		t.Fatal(err)
	}
	if len(list) != 1 || list[0].ID != id {
		t.Fatalf("list wrong: %+v", list)
	}
	if w.Header().Get("Access-Control-Allow-Origin") != "*" {
		t.Fatalf("missing CORS header")
	}

	// get
	w = do(t, h, "GET", "/v1/session/"+id, nil, nil)
	if w.Code != 200 {
		t.Fatalf("get: %d", w.Code)
	}
	var one Session
	if err := json.Unmarshal(w.Body.Bytes(), &one); err != nil {
		t.Fatal(err)
	}
	if one.ID != id || one.CartName != "Joust" {
		t.Fatalf("get body wrong: %+v", one)
	}

	// get unknown
	w = do(t, h, "GET", "/v1/session/does-not-exist", nil, nil)
	if w.Code != 404 {
		t.Fatalf("want 404 got %d", w.Code)
	}
}

func TestList_NewestFirstAndCap(t *testing.T) {
	s := testStore(t)
	// Pin now to deterministic sequence so CreatedAt differs.
	base := time.Unix(1_700_000_000, 0)
	step := 0
	s.now = func() time.Time {
		step++
		return base.Add(time.Duration(step) * time.Second)
	}
	h := s.routes()

	// create 503 sessions; list should cap at 500 newest-first
	for i := 0; i < 503; i++ {
		_, _ = mustCreate(t, h, "Cart", "h", "1.1.1.1:1")
	}
	w := do(t, h, "GET", "/v1/sessions", nil, nil)
	if w.Code != 200 {
		t.Fatalf("list: %d", w.Code)
	}
	var list []Session
	_ = json.Unmarshal(w.Body.Bytes(), &list)
	if len(list) != 500 {
		t.Fatalf("want 500 got %d", len(list))
	}
	for i := 1; i < len(list); i++ {
		if list[i-1].CreatedAt.Before(list[i].CreatedAt) {
			t.Fatalf("not newest-first at %d", i)
		}
	}
}

func TestHeartbeat(t *testing.T) {
	s := testStore(t)
	now := time.Unix(1_700_000_000, 0)
	s.now = func() time.Time { return now }
	h := s.routes()
	id, tok := mustCreate(t, h, "c", "h", "1.1.1.1:1")

	// bad token
	w := do(t, h, "POST", "/v1/session/"+id+"/heartbeat", heartbeatReq{Token: "nope"}, nil)
	if w.Code != 401 {
		t.Fatalf("want 401 got %d", w.Code)
	}

	// unknown id
	w = do(t, h, "POST", "/v1/session/unknown/heartbeat", heartbeatReq{Token: tok}, nil)
	if w.Code != 404 {
		t.Fatalf("want 404 got %d", w.Code)
	}

	// move clock forward, heartbeat, verify LastSeen updated
	now = now.Add(30 * time.Second)
	w = do(t, h, "POST", "/v1/session/"+id+"/heartbeat", heartbeatReq{Token: tok, PlayerCount: 2}, nil)
	if w.Code != 200 {
		t.Fatalf("want 200 got %d body=%s", w.Code, w.Body.String())
	}
	s.mu.RLock()
	sess := s.items[id]
	s.mu.RUnlock()
	if !sess.LastSeen.Equal(now) {
		t.Fatalf("LastSeen not updated: %v vs %v", sess.LastSeen, now)
	}
	if sess.PlayerCount != 2 {
		t.Fatalf("PlayerCount not updated: %d", sess.PlayerCount)
	}
}

func TestDelete(t *testing.T) {
	s := testStore(t)
	h := s.routes()
	id, tok := mustCreate(t, h, "c", "h", "1.1.1.1:1")

	// bad token
	w := do(t, h, "DELETE", "/v1/session/"+id, nil, map[string]string{"X-Session-Token": "nope"})
	if w.Code != 401 {
		t.Fatalf("want 401 got %d", w.Code)
	}

	// unknown
	w = do(t, h, "DELETE", "/v1/session/unknown", nil, map[string]string{"X-Session-Token": tok})
	if w.Code != 404 {
		t.Fatalf("want 404 got %d", w.Code)
	}

	// ok
	w = do(t, h, "DELETE", "/v1/session/"+id, nil, map[string]string{"X-Session-Token": tok})
	if w.Code != 204 {
		t.Fatalf("want 204 got %d", w.Code)
	}
	// gone
	w = do(t, h, "GET", "/v1/session/"+id, nil, nil)
	if w.Code != 404 {
		t.Fatalf("want 404 after delete got %d", w.Code)
	}
}

func TestVisibilityFieldsRoundTrip(t *testing.T) {
	s := testStore(t)
	h := s.routes()

	// private + code + art hash
	w := do(t, h, "POST", "/v1/session", createReq{
		CartName: "Joust", HostHandle: "alice", HostEndpoint: "1.2.3.4:26100",
		Region: "eu", MaxPlayers: 2, PlayerCount: 1, ProtocolVersion: 1,
		Visibility: "private", RequiresCode: true, CartArtHash: "deadbeefcafef00d",
	}, nil)
	if w.Code != 201 {
		t.Fatalf("create: %d body=%s", w.Code, w.Body.String())
	}
	var cr createResp
	_ = json.Unmarshal(w.Body.Bytes(), &cr)

	w = do(t, h, "GET", "/v1/session/"+cr.SessionID, nil, nil)
	if w.Code != 200 {
		t.Fatalf("get: %d", w.Code)
	}
	var got Session
	if err := json.Unmarshal(w.Body.Bytes(), &got); err != nil {
		t.Fatal(err)
	}
	if got.Visibility != "private" || !got.RequiresCode || got.CartArtHash != "deadbeefcafef00d" {
		t.Fatalf("round-trip fields wrong: %+v", got)
	}

	// omitted visibility defaults to "public"
	w = do(t, h, "POST", "/v1/session", createReq{
		CartName: "Joust", HostHandle: "bob", HostEndpoint: "1.2.3.4:26101",
		Region: "eu", MaxPlayers: 2, PlayerCount: 1, ProtocolVersion: 1,
	}, nil)
	if w.Code != 201 {
		t.Fatalf("create2: %d body=%s", w.Code, w.Body.String())
	}
	_ = json.Unmarshal(w.Body.Bytes(), &cr)
	w = do(t, h, "GET", "/v1/session/"+cr.SessionID, nil, nil)
	_ = json.Unmarshal(w.Body.Bytes(), &got)
	if got.Visibility != "public" {
		t.Fatalf("default visibility: %q", got.Visibility)
	}
}

func TestBrowserOriginBlocked(t *testing.T) {
	h := testStore(t).routes()
	w := do(t, h, "POST", "/v1/session", createReq{
		CartName: "c", HostHandle: "h", HostEndpoint: "1.1.1.1:1",
		MaxPlayers: 2, PlayerCount: 1, ProtocolVersion: 1,
	}, map[string]string{"Origin": "https://evil.example"})
	if w.Code != 403 {
		t.Fatalf("want 403 got %d", w.Code)
	}
}

func TestCORSPreflight(t *testing.T) {
	h := testStore(t).routes()
	req := httptest.NewRequest("OPTIONS", "/v1/sessions", nil)
	req.RemoteAddr = "127.0.0.1:1"
	req.Header.Set("Origin", "https://x.example")
	req.Header.Set("Access-Control-Request-Method", "GET")
	w := httptest.NewRecorder()
	h.ServeHTTP(w, req)
	if w.Code != 204 {
		t.Fatalf("want 204 got %d", w.Code)
	}
	if w.Header().Get("Access-Control-Allow-Methods") != "GET" {
		t.Fatalf("preflight must allow GET only, got %q", w.Header().Get("Access-Control-Allow-Methods"))
	}
}

func TestMaxSessionsCap(t *testing.T) {
	s := newStore(config{
		maxSessions: 2, ttl: 90 * time.Second,
		rateBurst: 10_000, rateRefill: time.Second,
	})
	h := s.routes()
	_, _ = mustCreate(t, h, "a", "h", "1.1.1.1:1")
	_, _ = mustCreate(t, h, "b", "h", "1.1.1.1:1")
	w := do(t, h, "POST", "/v1/session", createReq{
		CartName: "c", HostHandle: "h", HostEndpoint: "1.1.1.1:1",
		MaxPlayers: 2, PlayerCount: 1, ProtocolVersion: 1,
	}, nil)
	if w.Code != 429 {
		t.Fatalf("want 429 got %d", w.Code)
	}
}

func TestRateLimit(t *testing.T) {
	s := newStore(config{
		maxSessions: 1000, ttl: 90 * time.Second,
		rateBurst: 3, rateRefill: time.Hour,
	})
	h := s.routes()
	for i := 0; i < 3; i++ {
		w := do(t, h, "GET", "/healthz", nil, nil)
		if w.Code != 200 {
			t.Fatalf("req %d: %d", i, w.Code)
		}
	}
	w := do(t, h, "GET", "/healthz", nil, nil)
	if w.Code != 429 {
		t.Fatalf("want 429 got %d", w.Code)
	}
}

func TestExpireLoop(t *testing.T) {
	s := testStore(t)
	now := time.Unix(1_700_000_000, 0)
	s.now = func() time.Time { return now }
	h := s.routes()
	id, _ := mustCreate(t, h, "c", "h", "1.1.1.1:1")

	// jump past TTL
	now = now.Add(91 * time.Second)
	if n := s.expireOnce(); n != 1 {
		t.Fatalf("expired %d", n)
	}
	w := do(t, h, "GET", "/v1/session/"+id, nil, nil)
	if w.Code != 404 {
		t.Fatalf("want 404 got %d", w.Code)
	}
}

func TestUUIDAndToken(t *testing.T) {
	a := newUUIDv4()
	b := newUUIDv4()
	if a == b {
		t.Fatal("uuid collision")
	}
	if len(a) != 36 || a[14] != '4' {
		t.Fatalf("bad v4 uuid: %q", a)
	}
	if len(newToken()) != 32 {
		t.Fatal("bad token")
	}
}

func TestIsEndpoint(t *testing.T) {
	cases := []struct {
		in string
		ok bool
	}{
		{"", false}, {":80", false}, {"host", false}, {"host:", false},
		{"host:abc", false}, {"host:0", false}, {"host:65536", false},
		{"1.2.3.4:26100", true}, {"[::1]:80", true}, {"example.com:443", true},
	}
	for _, c := range cases {
		if got := isEndpoint(c.in); got != c.ok {
			t.Errorf("%q: got %v want %v", c.in, got, c.ok)
		}
	}
}

func TestClientIP(t *testing.T) {
	r := httptest.NewRequest("GET", "/", nil)
	r.RemoteAddr = "10.0.0.1:4242"
	if clientIP(r) != "10.0.0.1" {
		t.Fatalf("got %q", clientIP(r))
	}
	r.Header.Set("X-Forwarded-For", "1.2.3.4, 10.0.0.1")
	if clientIP(r) != "1.2.3.4" {
		t.Fatalf("xff got %q", clientIP(r))
	}
}

func TestEnvHelpers(t *testing.T) {
	t.Setenv("FOO_STR", "bar")
	if envString("FOO_STR", "d") != "bar" {
		t.Fatal()
	}
	if envString("NOPE_XX", "d") != "d" {
		t.Fatal()
	}
	t.Setenv("FOO_INT", "42")
	if envInt("FOO_INT", 1) != 42 {
		t.Fatal()
	}
	t.Setenv("FOO_INT_BAD", "x")
	if envInt("FOO_INT_BAD", 7) != 7 {
		t.Fatal()
	}
	if envInt("NOPE_INT", 9) != 9 {
		t.Fatal()
	}
}

func TestRateLimiterPrune(t *testing.T) {
	// burst=2, 1-token-per-second refill.
	r := newRateLimiter(2, time.Second)

	// Two IPs use a token each at t=0; both buckets live.
	now := time.Unix(0, 0)
	r.allow("1.1.1.1", now)
	r.allow("2.2.2.2", now)
	if got := len(r.byIP); got != 2 {
		t.Fatalf("want 2 buckets, got %d", got)
	}

	// Prune with keep=1min at t=5min: both entries older → gone.
	removed := r.prune(now.Add(5*time.Minute), time.Minute)
	if removed != 2 || len(r.byIP) != 0 {
		t.Fatalf("prune: removed=%d remaining=%d", removed, len(r.byIP))
	}

	// Touch one IP at t=5m, prune with keep=1min at t=5m+30s:
	// fresh entry survives.
	fresh := now.Add(5 * time.Minute)
	r.allow("3.3.3.3", fresh)
	r.prune(fresh.Add(30*time.Second), time.Minute)
	if got := len(r.byIP); got != 1 {
		t.Fatalf("fresh entry pruned too early: remaining=%d", got)
	}
}

func TestSortByCreatedDesc(t *testing.T) {
	t0 := time.Unix(100, 0)
	xs := []*Session{
		{ID: "a", CreatedAt: t0.Add(3 * time.Second)},
		{ID: "b", CreatedAt: t0.Add(1 * time.Second)},
		{ID: "c", CreatedAt: t0.Add(5 * time.Second)},
		{ID: "d", CreatedAt: t0.Add(2 * time.Second)},
	}
	sortByCreatedDesc(xs)
	want := []string{"c", "a", "d", "b"}
	for i, w := range want {
		if xs[i].ID != w {
			t.Fatalf("index %d: got %q want %q", i, xs[i].ID, w)
		}
	}
}
