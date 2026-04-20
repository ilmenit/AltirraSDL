// Altirra SDL netplay lobby — reference server.
//
// Minimal stdlib-only HTTP/JSON session directory. In-memory, no DB.
// See NETPLAY_DESIGN_PLAN.md §11 in the AltirraSDL project for the
// protocol rationale. This is the PoC scope: no /join rendezvous,
// per-session token on heartbeat/delete.
package main

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"os/signal"
	"runtime/debug"
	"sort"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"
)

// --- config ------------------------------------------------------------------

type config struct {
	bind        string
	maxSessions int
	ttl         time.Duration
	rateBurst   int
	rateRefill  time.Duration
}

func loadConfig() config {
	return config{
		bind:        envString("BIND", ":8080"),
		maxSessions: envInt("MAX_SESSIONS", 1000),
		ttl:         time.Duration(envInt("TTL_SECONDS", 90)) * time.Second,
		// 60 was tight: a single buggy client spinning on a failed
		// List could drain the bucket before backoff kicks in.  Doubled
		// to 120 so small bursts from polling UIs don't 429 even when
		// two or three peers share one NAT public IP.
		rateBurst:   envInt("RATE_BURST", 120),
		rateRefill:  time.Second,
	}
}

func envString(k, d string) string {
	if v := os.Getenv(k); v != "" {
		return v
	}
	return d
}

func envInt(k string, d int) int {
	if v := os.Getenv(k); v != "" {
		if n, err := strconv.Atoi(v); err == nil {
			return n
		}
	}
	return d
}

// --- session store -----------------------------------------------------------

type Session struct {
	ID           string    `json:"sessionId"`
	CartName     string    `json:"cartName"`
	HostHandle   string    `json:"hostHandle"`
	HostEndpoint string    `json:"hostEndpoint"`
	Region       string    `json:"region"`
	PlayerCount  int       `json:"playerCount"`
	MaxPlayers   int       `json:"maxPlayers"`
	ProtocolVer  int       `json:"protocolVersion"`
	Visibility   string    `json:"visibility"`             // "public" | "private"
	RequiresCode bool      `json:"requiresCode"`           // true → joiners must supply entry code P2P
	CartArtHash  string    `json:"cartArtHash,omitempty"`  // optional hex; lets clients match local art cache
	CreatedAt    time.Time `json:"createdAt"`
	LastSeen     time.Time `json:"lastSeen"`

	// token is returned on create only; never in list/get responses.
	token string
}

// store is the in-memory session table + rate limiter + config.
// Kept on a struct (not globals) so tests can spin up isolated instances.
type store struct {
	cfg   config
	mu    sync.RWMutex
	items map[string]*Session
	rate  *rateLimiter
	now   func() time.Time
}

func newStore(cfg config) *store {
	return &store{
		cfg:   cfg,
		items: map[string]*Session{},
		rate:  newRateLimiter(cfg.rateBurst, cfg.rateRefill),
		now:   time.Now,
	}
}

func (s *store) expireLoop(ctx context.Context, tick time.Duration) {
	t := time.NewTicker(tick)
	defer t.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-t.C:
			s.expireOnce()
			// Prune rate-limiter buckets untouched for 10 min.
			// The burst replenishes in seconds, so anyone idle
			// for 10 min has effectively reset their bucket
			// anyway — the memory is pure overhead.
			s.rate.prune(s.now(), 10*time.Minute)
		}
	}
}

func (s *store) expireOnce() int {
	s.mu.Lock()
	defer s.mu.Unlock()
	cutoff := s.now().Add(-s.cfg.ttl)
	n := 0
	for k, v := range s.items {
		if v.LastSeen.Before(cutoff) {
			delete(s.items, k)
			n++
		}
	}
	return n
}

// --- request/response types --------------------------------------------------

type createReq struct {
	CartName        string `json:"cartName"`
	HostHandle      string `json:"hostHandle"`
	HostEndpoint    string `json:"hostEndpoint"`
	Region          string `json:"region"`
	PlayerCount     int    `json:"playerCount"`
	MaxPlayers      int    `json:"maxPlayers"`
	ProtocolVersion int    `json:"protocolVersion"`
	Visibility      string `json:"visibility"`    // "public" | "private"; empty defaults to "public"
	RequiresCode    bool   `json:"requiresCode"`  // entry code required P2P for private sessions
	CartArtHash     string `json:"cartArtHash"`   // optional hex content hash for client-side art match
}

type createResp struct {
	SessionID  string `json:"sessionId"`
	Token      string `json:"token"`
	TTLSeconds int    `json:"ttlSeconds"`
}

type heartbeatReq struct {
	Token       string `json:"token"`
	PlayerCount int    `json:"playerCount"`
}

type errorResp struct {
	Error string `json:"error"`
}

// --- handlers ----------------------------------------------------------------

func (s *store) routes() http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("GET /healthz", s.health)
	mux.HandleFunc("GET /v1/sessions", s.listSessions)
	mux.HandleFunc("POST /v1/session", s.createSession)
	mux.HandleFunc("GET /v1/session/{id}", s.getSession)
	mux.HandleFunc("POST /v1/session/{id}/heartbeat", s.heartbeatSession)
	mux.HandleFunc("DELETE /v1/session/{id}", s.deleteSession)
	mux.HandleFunc("OPTIONS /", s.preflight)
	return s.middleware(mux)
}

func (s *store) health(w http.ResponseWriter, r *http.Request) {
	// Deliberately exercise the store lock so /healthz detects a
	// wedged mutex (the most likely failure mode if a handler
	// panicked under a lock before panic-recovery was added).
	// Returns fast under normal load; hangs if the store is
	// deadlocked — which makes upstream health-check probes
	// surface the problem instead of silently thinking the
	// process is alive just because TCP accepts.
	s.mu.RLock()
	n := len(s.items)
	s.mu.RUnlock()
	w.Header().Set("Content-Type", "text/plain; charset=utf-8")
	_, _ = io.WriteString(w, fmt.Sprintf("ok sessions=%d", n))
}

func (s *store) listSessions(w http.ResponseWriter, r *http.Request) {
	s.mu.RLock()
	out := make([]*Session, 0, len(s.items))
	for _, v := range s.items {
		out = append(out, v)
	}
	s.mu.RUnlock()
	// newest first
	sortByCreatedDesc(out)
	if len(out) > 500 {
		out = out[:500]
	}
	writeJSON(w, http.StatusOK, out)
}

func (s *store) createSession(w http.ResponseWriter, r *http.Request) {
	var req createReq
	if err := decodeJSON(r, &req); err != nil {
		writeErr(w, http.StatusBadRequest, err.Error())
		return
	}
	if err := validateCreate(&req); err != nil {
		writeErr(w, http.StatusBadRequest, err.Error())
		return
	}

	s.mu.Lock()
	if len(s.items) >= s.cfg.maxSessions {
		s.mu.Unlock()
		writeErr(w, http.StatusTooManyRequests, "lobby full")
		return
	}
	id := newUUIDv4()
	tok := newToken()
	now := s.now()
	visibility := req.Visibility
	if visibility == "" {
		visibility = "public"
	}
	sess := &Session{
		ID:           id,
		CartName:     req.CartName,
		HostHandle:   req.HostHandle,
		HostEndpoint: req.HostEndpoint,
		Region:       req.Region,
		PlayerCount:  req.PlayerCount,
		MaxPlayers:   req.MaxPlayers,
		ProtocolVer:  req.ProtocolVersion,
		Visibility:   visibility,
		RequiresCode: req.RequiresCode,
		CartArtHash:  req.CartArtHash,
		CreatedAt:    now,
		LastSeen:     now,
		token:        tok,
	}
	s.items[id] = sess
	s.mu.Unlock()

	writeJSON(w, http.StatusCreated, createResp{
		SessionID:  id,
		Token:      tok,
		TTLSeconds: int(s.cfg.ttl.Seconds()),
	})
}

func (s *store) getSession(w http.ResponseWriter, r *http.Request) {
	id := r.PathValue("id")
	s.mu.RLock()
	sess, ok := s.items[id]
	s.mu.RUnlock()
	if !ok {
		writeErr(w, http.StatusNotFound, "no such session")
		return
	}
	writeJSON(w, http.StatusOK, sess)
}

func (s *store) heartbeatSession(w http.ResponseWriter, r *http.Request) {
	id := r.PathValue("id")
	var req heartbeatReq
	if err := decodeJSON(r, &req); err != nil {
		writeErr(w, http.StatusBadRequest, err.Error())
		return
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	sess, ok := s.items[id]
	if !ok {
		writeErr(w, http.StatusNotFound, "no such session")
		return
	}
	if sess.token != req.Token {
		writeErr(w, http.StatusUnauthorized, "bad token")
		return
	}
	sess.LastSeen = s.now()
	if req.PlayerCount > 0 && req.PlayerCount <= sess.MaxPlayers {
		sess.PlayerCount = req.PlayerCount
	}
	writeJSON(w, http.StatusOK, map[string]int{"ttlSeconds": int(s.cfg.ttl.Seconds())})
}

func (s *store) deleteSession(w http.ResponseWriter, r *http.Request) {
	id := r.PathValue("id")
	tok := r.Header.Get("X-Session-Token")
	s.mu.Lock()
	defer s.mu.Unlock()
	sess, ok := s.items[id]
	if !ok {
		writeErr(w, http.StatusNotFound, "no such session")
		return
	}
	if sess.token != tok {
		writeErr(w, http.StatusUnauthorized, "bad token")
		return
	}
	delete(s.items, id)
	w.WriteHeader(http.StatusNoContent)
}

func (s *store) preflight(w http.ResponseWriter, r *http.Request) {
	// We advertise GET-only CORS; browsers trying to POST/DELETE will
	// fail preflight and never reach the handler.
	w.Header().Set("Access-Control-Allow-Origin", "*")
	w.Header().Set("Access-Control-Allow-Methods", "GET")
	w.Header().Set("Access-Control-Allow-Headers", "Content-Type")
	w.Header().Set("Access-Control-Max-Age", "600")
	w.WriteHeader(http.StatusNoContent)
}

// --- middleware --------------------------------------------------------------

type statusWriter struct {
	http.ResponseWriter
	code int
	n    int
}

func (w *statusWriter) WriteHeader(code int) {
	w.code = code
	w.ResponseWriter.WriteHeader(code)
}

func (w *statusWriter) Write(b []byte) (int, error) {
	if w.code == 0 {
		w.code = http.StatusOK
	}
	n, err := w.ResponseWriter.Write(b)
	w.n += n
	return n, err
}

func (s *store) middleware(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		ip := clientIP(r)

		// Panic recovery.  Without this, a panic in any handler
		// leaks whatever locks the goroutine held (s.mu in
		// particular) — subsequent requests deadlock, the server
		// looks like it accepts TCP but never responds, and the
		// only fix is a process restart.  Always log the stack.
		defer func() {
			if rv := recover(); rv != nil {
				log.Printf("PANIC in handler %s %s from %s: %v\n%s",
					r.Method, r.URL.Path, ip, rv, debug.Stack())
				// Best-effort response; headers may already be sent.
				writeErr(w, http.StatusInternalServerError, "internal error")
			}
		}()

		// Browser-origin guard: block POST/DELETE that carries an Origin
		// header (native clients don't send one). GET is CORS-open.
		if r.Method == http.MethodPost || r.Method == http.MethodDelete {
			if o := r.Header.Get("Origin"); o != "" {
				writeErr(w, http.StatusForbidden, "browser origin not permitted on write routes")
				logReq(r, ip, http.StatusForbidden, 0, 0)
				return
			}
		}

		// CORS for GET.
		if r.Method == http.MethodGet {
			w.Header().Set("Access-Control-Allow-Origin", "*")
			w.Header().Set("Vary", "Origin")
		}

		if !s.rate.allow(ip, s.now()) {
			// Tell the client how long to wait before retrying; at the
			// current refill rate the next token is ~1 * rateRefill
			// away.  Rounded up to whole seconds for the header.
			retry := int(s.cfg.rateRefill/time.Second) + 1
			if retry < 1 {
				retry = 1
			}
			w.Header().Set("Retry-After", strconv.Itoa(retry))
			writeErr(w, http.StatusTooManyRequests, "rate limit exceeded")
			logReq(r, ip, http.StatusTooManyRequests, 0, 0)
			return
		}

		start := s.now()
		sw := &statusWriter{ResponseWriter: w}
		next.ServeHTTP(sw, r)
		logReq(r, ip, sw.code, sw.n, s.now().Sub(start))
	})
}

func clientIP(r *http.Request) string {
	if v := r.Header.Get("X-Forwarded-For"); v != "" {
		// first entry
		if i := strings.Index(v, ","); i >= 0 {
			return strings.TrimSpace(v[:i])
		}
		return strings.TrimSpace(v)
	}
	host := r.RemoteAddr
	if i := strings.LastIndex(host, ":"); i >= 0 {
		host = host[:i]
	}
	return host
}

// --- rate limiter (token bucket per IP) -------------------------------------

type bucket struct {
	tokens   float64
	lastFill time.Time
}

type rateLimiter struct {
	mu     sync.Mutex
	byIP   map[string]*bucket
	burst  float64
	refill time.Duration // interval between 1-token refills
}

func newRateLimiter(burst int, refill time.Duration) *rateLimiter {
	return &rateLimiter{
		byIP:   map[string]*bucket{},
		burst:  float64(burst),
		refill: refill,
	}
}

func (r *rateLimiter) allow(ip string, now time.Time) bool {
	r.mu.Lock()
	defer r.mu.Unlock()
	b, ok := r.byIP[ip]
	if !ok {
		b = &bucket{tokens: r.burst, lastFill: now}
		r.byIP[ip] = b
	}
	// refill
	elapsed := now.Sub(b.lastFill)
	if elapsed > 0 {
		add := float64(elapsed) / float64(r.refill)
		b.tokens += add
		if b.tokens > r.burst {
			b.tokens = r.burst
		}
		b.lastFill = now
	}
	if b.tokens < 1 {
		return false
	}
	b.tokens -= 1
	return true
}

// prune removes bucket entries that haven't been touched in `keep`.
// Without this, byIP grows unbounded over the process lifetime as
// new source IPs hit the service; over weeks/months that becomes a
// slow memory leak and a longer-and-longer lock hold inside allow().
func (r *rateLimiter) prune(now time.Time, keep time.Duration) int {
	r.mu.Lock()
	defer r.mu.Unlock()
	cutoff := now.Add(-keep)
	n := 0
	for ip, b := range r.byIP {
		if b.lastFill.Before(cutoff) {
			delete(r.byIP, ip)
			n++
		}
	}
	return n
}

// --- helpers -----------------------------------------------------------------

func decodeJSON(r *http.Request, dst any) error {
	if r.Body == nil {
		return errors.New("empty body")
	}
	defer r.Body.Close()
	dec := json.NewDecoder(io.LimitReader(r.Body, 8*1024))
	dec.DisallowUnknownFields()
	if err := dec.Decode(dst); err != nil {
		return fmt.Errorf("invalid json: %w", err)
	}
	return nil
}

func writeJSON(w http.ResponseWriter, code int, body any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(code)
	_ = json.NewEncoder(w).Encode(body)
}

func writeErr(w http.ResponseWriter, code int, msg string) {
	writeJSON(w, code, errorResp{Error: msg})
}

func validateCreate(r *createReq) error {
	if l := len(r.CartName); l == 0 || l > 64 {
		return errors.New("cartName: 1..64 chars required")
	}
	if l := len(r.HostHandle); l == 0 || l > 32 {
		return errors.New("hostHandle: 1..32 chars required")
	}
	if !isEndpoint(r.HostEndpoint) {
		return errors.New("hostEndpoint: host:port required")
	}
	if r.MaxPlayers < 2 || r.MaxPlayers > 8 {
		return errors.New("maxPlayers: 2..8 required")
	}
	if r.PlayerCount < 1 || r.PlayerCount > r.MaxPlayers {
		return errors.New("playerCount: 1..maxPlayers required")
	}
	if r.ProtocolVersion <= 0 {
		return errors.New("protocolVersion required")
	}
	if len(r.Region) > 32 {
		return errors.New("region: <=32 chars")
	}
	switch r.Visibility {
	case "", "public", "private":
	default:
		return errors.New(`visibility: "" | "public" | "private" required`)
	}
	if r.RequiresCode && r.Visibility != "private" {
		return errors.New(`requiresCode requires visibility "private"`)
	}
	if r.Visibility == "private" && !r.RequiresCode {
		// A private session without an entry code is incoherent: the
		// padlock UI would be shown but no P2P challenge would be
		// issued.  Force the two flags to agree.
		return errors.New(`visibility "private" requires requiresCode=true`)
	}
	if l := len(r.CartArtHash); l > 64 {
		return errors.New("cartArtHash: <=64 hex chars")
	}
	for _, c := range r.CartArtHash {
		if !isHex(c) {
			return errors.New("cartArtHash: hex digits only")
		}
	}
	return nil
}

func isHex(c rune) bool {
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')
}

func isEndpoint(s string) bool {
	i := strings.LastIndex(s, ":")
	if i <= 0 || i == len(s)-1 {
		return false
	}
	port, err := strconv.Atoi(s[i+1:])
	return err == nil && port > 0 && port < 65536
}

func newUUIDv4() string {
	var b [16]byte
	_, _ = rand.Read(b[:])
	b[6] = (b[6] & 0x0f) | 0x40
	b[8] = (b[8] & 0x3f) | 0x80
	return fmt.Sprintf("%x-%x-%x-%x-%x", b[0:4], b[4:6], b[6:8], b[8:10], b[10:16])
}

func newToken() string {
	var b [16]byte
	_, _ = rand.Read(b[:])
	return hex.EncodeToString(b[:])
}

func sortByCreatedDesc(xs []*Session) {
	// stdlib sort — O(N log N).  The old O(N²) insertion sort was
	// fine for tiny lists but /v1/sessions can hold maxSessions
	// entries (default 1000) and is a hot endpoint; insertion sort
	// was doing up to 500 000 compares per list call.
	sort.Slice(xs, func(i, j int) bool {
		return xs[i].CreatedAt.After(xs[j].CreatedAt)
	})
}

// --- logging -----------------------------------------------------------------

func logReq(r *http.Request, ip string, code, nbytes int, dur time.Duration) {
	entry := map[string]any{
		"ts":     time.Now().UTC().Format(time.RFC3339Nano),
		"ip":     ip,
		"method": r.Method,
		"path":   r.URL.Path,
		"status": code,
		"bytes":  nbytes,
		"dur_ms": dur.Milliseconds(),
	}
	b, _ := json.Marshal(entry)
	fmt.Fprintln(os.Stdout, string(b))
}

// --- main --------------------------------------------------------------------

func main() {
	cfg := loadConfig()
	s := newStore(cfg)

	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGINT, syscall.SIGTERM)
	defer stop()

	go s.expireLoop(ctx, 30*time.Second)

	// Every http.Server timeout matters for robustness.  Without
	// Write/Idle timeouts, a single stuck goroutine (blocked on a
	// mutex, or a handler that panics mid-write) can hang a
	// connection indefinitely.  That's the symptom clients see as
	// "TCP accept OK, then recv hdr timeout" — the listener is up
	// but no response ever comes out of the worker.
	srv := &http.Server{
		Addr:              cfg.bind,
		Handler:           s.routes(),
		ReadHeaderTimeout: 5 * time.Second,
		ReadTimeout:       10 * time.Second,
		WriteTimeout:      10 * time.Second,
		IdleTimeout:       60 * time.Second,
		MaxHeaderBytes:    8 * 1024,
	}

	log.Printf("altirra-sdl-lobby listening on %s (max=%d, ttl=%s)",
		cfg.bind, cfg.maxSessions, cfg.ttl)

	errCh := make(chan error, 1)
	go func() {
		if err := srv.ListenAndServe(); err != nil && !errors.Is(err, http.ErrServerClosed) {
			errCh <- err
		}
		close(errCh)
	}()

	select {
	case <-ctx.Done():
		log.Printf("shutdown signal received")
	case err := <-errCh:
		log.Fatalf("server error: %v", err)
	}

	shutCtx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	if err := srv.Shutdown(shutCtx); err != nil {
		log.Printf("shutdown error: %v", err)
	}
	log.Printf("bye")
}
