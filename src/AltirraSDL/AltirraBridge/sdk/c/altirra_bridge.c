/*
 * AltirraBridge C SDK — implementation.
 *
 * Single-file client. libc + Winsock only. No JSON parser: response
 * inspection is intentionally limited to a few field probes
 * (substring search for "\"ok\":true" / "\"error\":\"...\""), which
 * is sufficient for Phase 1's small command set. Phase 2+ wrappers
 * that return structured payloads will need real parsing — at that
 * point we ship a tiny JSON peeker as a sibling file rather than
 * pulling in a full parser library.
 */

#include "altirra_bridge.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
   typedef int atb_socklen_t;
#  define ATB_INVALID_SOCK INVALID_SOCKET
#  define ATB_CLOSE(s)     closesocket(s)
   typedef SOCKET atb_sock_t;
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <sys/socket.h>
#  include <sys/un.h>
#  include <unistd.h>
#  include <netdb.h>
   typedef socklen_t atb_socklen_t;
#  define ATB_INVALID_SOCK (-1)
#  define ATB_CLOSE(s)     close(s)
   typedef int atb_sock_t;
#endif

#define ATB_RECV_BUF_SIZE   (64 * 1024)
#define ATB_ERROR_BUF_SIZE  256

struct atb_client {
	atb_sock_t sock;
	int        winsock_inited;
	char*      recv_buf;        /* dynamic, grows as needed */
	size_t     recv_len;
	size_t     recv_cap;
	char*      response;        /* most recent complete response line */
	char       error[ATB_ERROR_BUF_SIZE];
};

/* --------------------------------------------------------------------- */
/* Internal helpers                                                       */
/* --------------------------------------------------------------------- */

static void atb_set_error(atb_client_t* c, const char* msg) {
	if (!c) return;
	if (msg) {
		strncpy(c->error, msg, ATB_ERROR_BUF_SIZE - 1);
		c->error[ATB_ERROR_BUF_SIZE - 1] = '\0';
	} else {
		c->error[0] = '\0';
	}
}

static int atb_winsock_init(atb_client_t* c) {
#if defined(_WIN32)
	if (c->winsock_inited) return ATB_OK;
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
		atb_set_error(c, "WSAStartup failed");
		return ATB_ERR_NETWORK;
	}
	c->winsock_inited = 1;
#else
	(void)c;
#endif
	return ATB_OK;
}

static int atb_recv_grow(atb_client_t* c, size_t need) {
	if (c->recv_cap >= need) return ATB_OK;
	size_t new_cap = c->recv_cap ? c->recv_cap : ATB_RECV_BUF_SIZE;
	while (new_cap < need) new_cap *= 2;
	char* nb = (char*)realloc(c->recv_buf, new_cap);
	if (!nb) {
		atb_set_error(c, "out of memory");
		return ATB_ERR_NETWORK;
	}
	c->recv_buf = nb;
	c->recv_cap = new_cap;
	return ATB_OK;
}

/* Send all of len bytes; loops on partial sends. */
static int atb_send_all(atb_client_t* c, const void* buf, size_t len) {
	const char* p = (const char*)buf;
	while (len > 0) {
#if defined(_WIN32)
		int n = send(c->sock, p, (int)len, 0);
#else
		ssize_t n = send(c->sock, p, len, 0);
#endif
		if (n <= 0) {
			atb_set_error(c, "send() failed");
			return ATB_ERR_NETWORK;
		}
		p += n;
		len -= (size_t)n;
	}
	return ATB_OK;
}

/* Read until a '\n' is in the buffer, then return the line (without
 * the newline) via *outLine. The line points into the client's recv
 * buffer; valid until the next recv. Returns ATB_OK or a negative
 * error code. */
static int atb_recv_line(atb_client_t* c, char** outLine) {
	*outLine = NULL;

	for (;;) {
		/* Is there already a complete line in the buffer? */
		for (size_t i = 0; i < c->recv_len; ++i) {
			if (c->recv_buf[i] == '\n') {
				c->recv_buf[i] = '\0';
				*outLine = c->recv_buf;
				/* Stash the leftover into the front of the buffer
				 * for the next recv. */
				size_t leftover = c->recv_len - (i + 1);
				/* We give the caller a pointer into the buffer;
				 * we'll move the leftover next call (after the
				 * caller is done reading). For simplicity we move
				 * it now and adjust the returned pointer. The
				 * response data we just read is owned by us; we
				 * stash it into ->response. */
				if (c->response) free(c->response);
				c->response = (char*)malloc(i + 1);
				if (!c->response) {
					atb_set_error(c, "out of memory");
					return ATB_ERR_NETWORK;
				}
				memcpy(c->response, c->recv_buf, i + 1);
				if (leftover > 0)
					memmove(c->recv_buf, c->recv_buf + i + 1, leftover);
				c->recv_len = leftover;
				/* Strip trailing \r if present (CRLF). */
				size_t rl = strlen(c->response);
				if (rl > 0 && c->response[rl - 1] == '\r')
					c->response[rl - 1] = '\0';
				*outLine = c->response;
				return ATB_OK;
			}
		}

		/* Need more bytes. */
		int rc = atb_recv_grow(c, c->recv_len + 4096);
		if (rc != ATB_OK) return rc;
#if defined(_WIN32)
		int n = recv(c->sock, c->recv_buf + c->recv_len,
			(int)(c->recv_cap - c->recv_len), 0);
#else
		ssize_t n = recv(c->sock, c->recv_buf + c->recv_len,
			c->recv_cap - c->recv_len, 0);
#endif
		if (n == 0) {
			atb_set_error(c, "peer closed connection");
			return ATB_ERR_NETWORK;
		}
		if (n < 0) {
			atb_set_error(c, "recv() failed");
			return ATB_ERR_NETWORK;
		}
		c->recv_len += (size_t)n;
	}
}

/* Probe whether the response indicates {"ok":true,...}. Tolerates
 * leading/trailing whitespace. */
static int atb_response_is_ok(const char* response) {
	return response && strstr(response, "\"ok\":true") != NULL;
}

/* Extract the value of "error":"..." into out (truncated). */
static void atb_extract_error(const char* response, char* out, size_t outSize) {
	if (!response || outSize == 0) return;
	out[0] = '\0';
	const char* p = strstr(response, "\"error\":\"");
	if (!p) return;
	p += 9;
	size_t i = 0;
	while (*p && *p != '"' && i + 1 < outSize) {
		out[i++] = *p++;
	}
	out[i] = '\0';
}

/* --------------------------------------------------------------------- */
/* Public API                                                             */
/* --------------------------------------------------------------------- */

atb_client_t* atb_create(void) {
	atb_client_t* c = (atb_client_t*)calloc(1, sizeof(atb_client_t));
	if (!c) return NULL;
	c->sock = ATB_INVALID_SOCK;
	return c;
}

void atb_close(atb_client_t* c) {
	if (!c) return;
	if (c->sock != ATB_INVALID_SOCK) {
		ATB_CLOSE(c->sock);
		c->sock = ATB_INVALID_SOCK;
	}
#if defined(_WIN32)
	if (c->winsock_inited) {
		WSACleanup();
		c->winsock_inited = 0;
	}
#endif
	free(c->recv_buf);
	free(c->response);
	free(c);
}

int atb_connect(atb_client_t* c, const char* addrSpec) {
	if (!c || !addrSpec) return ATB_ERR_BAD_ARG;

	int rc = atb_winsock_init(c);
	if (rc != ATB_OK) return rc;

	if (strncmp(addrSpec, "tcp:", 4) == 0) {
		const char* rest = addrSpec + 4;
		const char* colon = strrchr(rest, ':');
		if (!colon) {
			atb_set_error(c, "tcp spec missing :port");
			return ATB_ERR_BAD_ARG;
		}
		char host[64];
		size_t hl = (size_t)(colon - rest);
		if (hl >= sizeof host) hl = sizeof host - 1;
		memcpy(host, rest, hl);
		host[hl] = '\0';
		int port = atoi(colon + 1);
		if (port <= 0 || port > 65535) {
			atb_set_error(c, "tcp spec bad port");
			return ATB_ERR_BAD_ARG;
		}

		c->sock = socket(AF_INET, SOCK_STREAM, 0);
		if (c->sock == ATB_INVALID_SOCK) {
			atb_set_error(c, "socket() failed");
			return ATB_ERR_NETWORK;
		}
		struct sockaddr_in sa;
		memset(&sa, 0, sizeof sa);
		sa.sin_family = AF_INET;
		sa.sin_port = htons((unsigned short)port);
		if (inet_pton(AF_INET, host, &sa.sin_addr) != 1) {
			atb_set_error(c, "bad host");
			ATB_CLOSE(c->sock);
			c->sock = ATB_INVALID_SOCK;
			return ATB_ERR_BAD_ARG;
		}
		if (connect(c->sock, (struct sockaddr*)&sa, sizeof sa) != 0) {
			atb_set_error(c, "connect() failed");
			ATB_CLOSE(c->sock);
			c->sock = ATB_INVALID_SOCK;
			return ATB_ERR_NETWORK;
		}
		/* Disable Nagle on the client side. The server already sets
		 * TCP_NODELAY on accept (see bridge_transport.cpp), but the
		 * option is per-direction: without this, the kernel batches
		 * client→server writes and interacts with delayed-ACK to
		 * add up to ~40 ms stalls on small-request / large-response
		 * patterns — which is exactly what every SDK call looks
		 * like. Failure here is non-fatal (older stacks / sockets
		 * that don't support the option still work, just slower). */
		{
			int one = 1;
			(void)setsockopt(c->sock, IPPROTO_TCP, TCP_NODELAY,
			                 (const char*)&one, sizeof one);
		}
		return ATB_OK;
	}

#if !defined(_WIN32)
	if (strncmp(addrSpec, "unix:", 5) == 0) {
		const char* path = addrSpec + 5;
		c->sock = socket(AF_UNIX, SOCK_STREAM, 0);
		if (c->sock == ATB_INVALID_SOCK) {
			atb_set_error(c, "socket(AF_UNIX) failed");
			return ATB_ERR_NETWORK;
		}
		struct sockaddr_un sa;
		memset(&sa, 0, sizeof sa);
		sa.sun_family = AF_UNIX;
		strncpy(sa.sun_path, path, sizeof sa.sun_path - 1);
		if (connect(c->sock, (struct sockaddr*)&sa, sizeof sa) != 0) {
			atb_set_error(c, "connect(AF_UNIX) failed");
			ATB_CLOSE(c->sock);
			c->sock = ATB_INVALID_SOCK;
			return ATB_ERR_NETWORK;
		}
		return ATB_OK;
	}
#endif

	atb_set_error(c, "unsupported addr spec");
	return ATB_ERR_BAD_ARG;
}

int atb_hello(atb_client_t* c, const char* token) {
	if (!c || !token) return ATB_ERR_BAD_ARG;
	char cmd[256];
	int n = snprintf(cmd, sizeof cmd, "HELLO %s\n", token);
	if (n <= 0 || (size_t)n >= sizeof cmd) {
		atb_set_error(c, "token too long");
		return ATB_ERR_BAD_ARG;
	}
	int rc = atb_send_all(c, cmd, (size_t)n);
	if (rc != ATB_OK) return rc;

	char* line = NULL;
	rc = atb_recv_line(c, &line);
	if (rc != ATB_OK) return rc;
	if (!atb_response_is_ok(line)) {
		atb_extract_error(line, c->error, ATB_ERROR_BUF_SIZE);
		if (c->error[0] == '\0')
			atb_set_error(c, "HELLO rejected");
		return ATB_ERR_AUTH;
	}
	return ATB_OK;
}

int atb_connect_token_file(atb_client_t* c, const char* tokenFilePath) {
	if (!c || !tokenFilePath) return ATB_ERR_BAD_ARG;

	FILE* fp = fopen(tokenFilePath, "r");
	if (!fp) {
		atb_set_error(c, "could not open token file");
		return ATB_ERR_TOKEN_FILE;
	}
	char addr[256] = {0};
	char token[64] = {0};
	if (!fgets(addr, sizeof addr, fp) ||
	    !fgets(token, sizeof token, fp)) {
		fclose(fp);
		atb_set_error(c, "token file is malformed");
		return ATB_ERR_TOKEN_FILE;
	}
	fclose(fp);
	/* Strip trailing newlines. */
	for (size_t i = 0; i < sizeof addr; ++i) {
		if (addr[i] == '\n' || addr[i] == '\r') { addr[i] = '\0'; break; }
	}
	for (size_t i = 0; i < sizeof token; ++i) {
		if (token[i] == '\n' || token[i] == '\r') { token[i] = '\0'; break; }
	}

	int rc = atb_connect(c, addr);
	if (rc != ATB_OK) return rc;
	return atb_hello(c, token);
}

int atb_send(atb_client_t* c, const char* command, const char** outResponse) {
	if (!c || !command) return ATB_ERR_BAD_ARG;

	size_t cmdLen = strlen(command);
	/* Build "<cmd>\n" */
	char* tmp = (char*)malloc(cmdLen + 2);
	if (!tmp) {
		atb_set_error(c, "out of memory");
		return ATB_ERR_NETWORK;
	}
	memcpy(tmp, command, cmdLen);
	tmp[cmdLen] = '\n';
	tmp[cmdLen + 1] = '\0';

	int rc = atb_send_all(c, tmp, cmdLen + 1);
	free(tmp);
	if (rc != ATB_OK) return rc;

	char* line = NULL;
	rc = atb_recv_line(c, &line);
	if (rc != ATB_OK) return rc;
	if (outResponse) *outResponse = line;
	return ATB_OK;
}

static int atb_simple_cmd(atb_client_t* c, const char* cmd) {
	const char* response = NULL;
	int rc = atb_send(c, cmd, &response);
	if (rc != ATB_OK) return rc;
	if (!atb_response_is_ok(response)) {
		atb_extract_error(response, c->error, ATB_ERROR_BUF_SIZE);
		if (c->error[0] == '\0')
			atb_set_error(c, "remote error");
		return ATB_ERR_REMOTE;
	}
	return ATB_OK;
}

int atb_ping(atb_client_t* c)   { return atb_simple_cmd(c, "PING"); }
int atb_pause(atb_client_t* c)  { return atb_simple_cmd(c, "PAUSE"); }
int atb_resume(atb_client_t* c) { return atb_simple_cmd(c, "RESUME"); }
int atb_quit(atb_client_t* c)   { return atb_simple_cmd(c, "QUIT"); }

int atb_frame(atb_client_t* c, unsigned int n) {
	char cmd[64];
	snprintf(cmd, sizeof cmd, "FRAME %u", n);
	return atb_simple_cmd(c, cmd);
}

/* --------------------------------------------------------------------- */
/* Phase 2 — state read                                                   */
/* --------------------------------------------------------------------- */

/* Pull the value of a "key":"$XXXX" or "key":N field out of a JSON
 * response line. Returns 1 on success and writes the value (parsed as
 * hex if it's a "$..." string, else decimal) into *out. Returns 0 on
 * not-found. Tolerates whitespace differences but assumes there are
 * no nested objects with the same key (none of the Phase 2 responses
 * use nested objects with colliding keys). */
static int atb_extract_uint(const char* response, const char* key, unsigned long* out) {
	if (!response || !key) return 0;
	char needle[64];
	int n = snprintf(needle, sizeof needle, "\"%s\":", key);
	if (n <= 0 || (size_t)n >= sizeof needle) return 0;
	const char* p = strstr(response, needle);
	if (!p) return 0;
	p += n;
	if (*p == '"') {
		++p;
		int base = 10;
		if (*p == '$')      { ++p; base = 16; }
		else if (*p == '0' && (p[1] == 'x' || p[1] == 'X')) { p += 2; base = 16; }
		*out = strtoul(p, NULL, base);
		return 1;
	}
	*out = strtoul(p, NULL, 10);
	return 1;
}

/* Pull a "key":"value" string field. Copies up to outSize-1 chars
 * (NUL-terminated). Returns 1 on success, 0 on not-found. */
static int atb_extract_str(const char* response, const char* key,
                           char* out, size_t outSize) {
	if (!response || !key || outSize == 0) return 0;
	char needle[64];
	int n = snprintf(needle, sizeof needle, "\"%s\":\"", key);
	if (n <= 0 || (size_t)n >= sizeof needle) return 0;
	const char* p = strstr(response, needle);
	if (!p) return 0;
	p += n;
	size_t i = 0;
	while (*p && *p != '"' && i + 1 < outSize) out[i++] = *p++;
	out[i] = '\0';
	return 1;
}

int atb_regs(atb_client_t* c, atb_cpu_state_t* out) {
	if (!out) return ATB_ERR_BAD_ARG;
	int rc = atb_simple_cmd(c, "REGS");
	if (rc != ATB_OK) return rc;
	const char* r = c->response;

	unsigned long v = 0;
	memset(out, 0, sizeof *out);
	if (atb_extract_uint(r, "PC", &v))     out->pc = (unsigned int)v;
	if (atb_extract_uint(r, "A", &v))      out->a  = (unsigned int)v;
	if (atb_extract_uint(r, "X", &v))      out->x  = (unsigned int)v;
	if (atb_extract_uint(r, "Y", &v))      out->y  = (unsigned int)v;
	if (atb_extract_uint(r, "S", &v))      out->s  = (unsigned int)v;
	if (atb_extract_uint(r, "P", &v))      out->p  = (unsigned int)v;
	if (atb_extract_uint(r, "cycles", &v)) out->cycles = v;
	atb_extract_str(r, "flags", out->flags, sizeof out->flags);
	atb_extract_str(r, "mode",  out->mode,  sizeof out->mode);
	return ATB_OK;
}

/* Hex-string -> bytes. Returns number of bytes written, 0 on error. */
static size_t atb_unhex(const char* hex, unsigned char* out, size_t out_cap) {
	size_t i = 0;
	while (hex[0] && hex[1] && i < out_cap) {
		int hi, lo;
		char c = hex[0];
		if      (c >= '0' && c <= '9') hi = c - '0';
		else if (c >= 'a' && c <= 'f') hi = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F') hi = c - 'A' + 10;
		else break;
		c = hex[1];
		if      (c >= '0' && c <= '9') lo = c - '0';
		else if (c >= 'a' && c <= 'f') lo = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F') lo = c - 'A' + 10;
		else break;
		out[i++] = (unsigned char)((hi << 4) | lo);
		hex += 2;
	}
	return i;
}

int atb_peek(atb_client_t* c, unsigned int addr, unsigned int length,
             unsigned char* out_buf) {
	if (!out_buf || length == 0) return ATB_ERR_BAD_ARG;
	char cmd[64];
	snprintf(cmd, sizeof cmd, "PEEK $%x %u", addr, length);
	int rc = atb_simple_cmd(c, cmd);
	if (rc != ATB_OK) return rc;
	const char* p = strstr(c->response, "\"data\":\"");
	if (!p) {
		atb_set_error(c, "PEEK response missing data field");
		return ATB_ERR_PROTOCOL;
	}
	p += 8;
	size_t got = atb_unhex(p, out_buf, length);
	if (got != length) {
		atb_set_error(c, "PEEK response short");
		return ATB_ERR_PROTOCOL;
	}
	return ATB_OK;
}

int atb_peek16(atb_client_t* c, unsigned int addr, unsigned int* out) {
	if (!out) return ATB_ERR_BAD_ARG;
	char cmd[64];
	snprintf(cmd, sizeof cmd, "PEEK16 $%x", addr);
	int rc = atb_simple_cmd(c, cmd);
	if (rc != ATB_OK) return rc;
	unsigned long v = 0;
	if (!atb_extract_uint(c->response, "value", &v)) {
		atb_set_error(c, "PEEK16 response missing value field");
		return ATB_ERR_PROTOCOL;
	}
	*out = (unsigned int)v;
	return ATB_OK;
}

int atb_antic(atb_client_t* c)   { return atb_simple_cmd(c, "ANTIC"); }
int atb_gtia(atb_client_t* c)    { return atb_simple_cmd(c, "GTIA"); }
int atb_pokey(atb_client_t* c)   { return atb_simple_cmd(c, "POKEY"); }
int atb_pia(atb_client_t* c)     { return atb_simple_cmd(c, "PIA"); }
int atb_dlist(atb_client_t* c)   { return atb_simple_cmd(c, "DLIST"); }
int atb_hwstate(atb_client_t* c) { return atb_simple_cmd(c, "HWSTATE"); }

int atb_palette(atb_client_t* c, unsigned char out_rgb[768]) {
	if (!out_rgb) return ATB_ERR_BAD_ARG;
	int rc = atb_simple_cmd(c, "PALETTE");
	if (rc != ATB_OK) return rc;
	const char* p = strstr(c->response, "\"data\":\"");
	if (!p) {
		atb_set_error(c, "PALETTE response missing data field");
		return ATB_ERR_PROTOCOL;
	}
	p += 8;
	size_t got = atb_unhex(p, out_rgb, 768);
	if (got != 768) {
		atb_set_error(c, "PALETTE response short");
		return ATB_ERR_PROTOCOL;
	}
	return ATB_OK;
}

/* Forward declaration — the definition lives further down next to
 * atb_memload, which also uses it. */
static void atb_base64_encode(const unsigned char* data, size_t len, char* out);

int atb_palette_load_act(atb_client_t* c, const unsigned char act_bytes[768],
                         float* out_rms_error) {
	if (!act_bytes) return ATB_ERR_BAD_ARG;
	/* Base64-encode 768 bytes -> 1024 chars + NUL. Plus the verb and
	 * a space: "PALETTE_LOAD_ACT <1024 chars>". */
	const size_t b64_size = ((size_t)768 + 2) / 3 * 4 + 1; /* = 1025 */
	const size_t cmd_size = 32 + b64_size;
	char* cmd = (char*)malloc(cmd_size);
	if (!cmd) { atb_set_error(c, "out of memory"); return ATB_ERR_NETWORK; }
	int n = snprintf(cmd, 32, "PALETTE_LOAD_ACT ");
	atb_base64_encode(act_bytes, 768, cmd + n);
	int rc = atb_simple_cmd(c, cmd);
	free(cmd);
	if (rc != ATB_OK) return rc;
	if (out_rms_error) {
		/* Parse "rms_error":NUMBER from the JSON response. */
		const char* p = strstr(c->response, "\"rms_error\":");
		if (p) {
			p += 12;
			*out_rms_error = (float)atof(p);
		} else {
			*out_rms_error = 0.0f;
		}
	}
	return ATB_OK;
}

int atb_palette_reset(atb_client_t* c) {
	return atb_simple_cmd(c, "PALETTE_RESET");
}

/* --------------------------------------------------------------------- */
/* Phase 3 — state write & input injection                                */
/* --------------------------------------------------------------------- */

int atb_poke(atb_client_t* c, unsigned int addr, unsigned int value) {
	char cmd[64];
	snprintf(cmd, sizeof cmd, "POKE $%x $%x", addr & 0xFFFF, value & 0xFF);
	return atb_simple_cmd(c, cmd);
}

int atb_poke16(atb_client_t* c, unsigned int addr, unsigned int value) {
	char cmd[64];
	snprintf(cmd, sizeof cmd, "POKE16 $%x $%x", addr & 0xFFFF, value & 0xFFFF);
	return atb_simple_cmd(c, cmd);
}

int atb_hwpoke(atb_client_t* c, unsigned int addr, unsigned int value) {
	char cmd[64];
	snprintf(cmd, sizeof cmd, "HWPOKE $%x $%x", addr & 0xFFFF, value & 0xFF);
	return atb_simple_cmd(c, cmd);
}

/* base64-encode `len` bytes from `data` into `out`. `out` must have
 * room for ((len + 2) / 3) * 4 + 1 bytes (including NUL). */
static void atb_base64_encode(const unsigned char* data, size_t len, char* out) {
	static const char alpha[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	size_t i = 0, j = 0;
	while (i + 3 <= len) {
		unsigned v = ((unsigned)data[i] << 16) | ((unsigned)data[i+1] << 8) | data[i+2];
		out[j++] = alpha[(v >> 18) & 0x3F];
		out[j++] = alpha[(v >> 12) & 0x3F];
		out[j++] = alpha[(v >>  6) & 0x3F];
		out[j++] = alpha[ v        & 0x3F];
		i += 3;
	}
	if (i < len) {
		unsigned v = (unsigned)data[i] << 16;
		if (i + 1 < len) v |= (unsigned)data[i+1] << 8;
		out[j++] = alpha[(v >> 18) & 0x3F];
		out[j++] = alpha[(v >> 12) & 0x3F];
		out[j++] = (i + 1 < len) ? alpha[(v >> 6) & 0x3F] : '=';
		out[j++] = '=';
	}
	out[j] = '\0';
}

int atb_memload(atb_client_t* c, unsigned int addr,
                const unsigned char* data, unsigned int length) {
	if (!data || length == 0) return ATB_ERR_BAD_ARG;

	/* Build "MEMLOAD $addr <base64>" — the base64 expands by 4/3,
	 * so allocate accordingly plus a small header for the verb. */
	size_t b64_size = ((size_t)length + 2) / 3 * 4 + 1;
	size_t cmd_size = 32 + b64_size;
	char* cmd = (char*)malloc(cmd_size);
	if (!cmd) {
		atb_set_error(c, "out of memory");
		return ATB_ERR_NETWORK;
	}
	int n = snprintf(cmd, 32, "MEMLOAD $%x ", addr & 0xFFFF);
	atb_base64_encode(data, length, cmd + n);
	int rc = atb_simple_cmd(c, cmd);
	free(cmd);
	return rc;
}

/* base64-decode helper for atb_memdump. Tolerates whitespace and
 * trailing '=' padding. Writes up to out_cap bytes; returns the
 * number actually written. Decodes character classes inline (no
 * lookup table) so the code is portable to MSVC, which doesn't
 * support GCC's designated-range initializers. */
static int atb_base64_decode_one(unsigned char c) {
	if (c >= 'A' && c <= 'Z') return (int)(c - 'A');
	if (c >= 'a' && c <= 'z') return (int)(c - 'a' + 26);
	if (c >= '0' && c <= '9') return (int)(c - '0' + 52);
	if (c == '+') return 62;
	if (c == '/') return 63;
	return -1;
}
static size_t atb_base64_decode(const char* src, unsigned char* out, size_t out_cap) {
	unsigned accum = 0;
	int bits = 0;
	size_t out_len = 0;
	while (*src && *src != '"') {
		unsigned char c = (unsigned char)*src++;
		if (c == '=') break;
		if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
		int v = atb_base64_decode_one(c);
		if (v < 0) break;
		accum = (accum << 6) | (unsigned)v;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			if (out_len < out_cap)
				out[out_len++] = (unsigned char)((accum >> bits) & 0xFF);
		}
	}
	return out_len;
}

int atb_memdump(atb_client_t* c, unsigned int addr, unsigned int length,
                unsigned char* out_buf) {
	if (!out_buf || length == 0) return ATB_ERR_BAD_ARG;
	char cmd[64];
	snprintf(cmd, sizeof cmd, "MEMDUMP $%x %u", addr & 0xFFFF, length);
	int rc = atb_simple_cmd(c, cmd);
	if (rc != ATB_OK) return rc;
	const char* p = strstr(c->response, "\"data\":\"");
	if (!p) {
		atb_set_error(c, "MEMDUMP response missing data field");
		return ATB_ERR_PROTOCOL;
	}
	p += 8;
	size_t got = atb_base64_decode(p, out_buf, length);
	if (got != length) {
		atb_set_error(c, "MEMDUMP response short");
		return ATB_ERR_PROTOCOL;
	}
	return ATB_OK;
}

int atb_joy(atb_client_t* c, unsigned int port, const char* direction, int fire) {
	if (!direction || port > 3) return ATB_ERR_BAD_ARG;
	char cmd[64];
	if (fire)
		snprintf(cmd, sizeof cmd, "JOY %u %s fire", port, direction);
	else
		snprintf(cmd, sizeof cmd, "JOY %u %s", port, direction);
	return atb_simple_cmd(c, cmd);
}

int atb_key(atb_client_t* c, const char* name, int shift, int ctrl) {
	if (!name) return ATB_ERR_BAD_ARG;
	char cmd[96];
	int n = snprintf(cmd, sizeof cmd, "KEY %s%s%s",
		name, shift ? " shift" : "", ctrl ? " ctrl" : "");
	if (n <= 0 || (size_t)n >= sizeof cmd) return ATB_ERR_BAD_ARG;
	return atb_simple_cmd(c, cmd);
}

int atb_consol(atb_client_t* c, int start, int select, int option) {
	char cmd[64];
	int n = snprintf(cmd, sizeof cmd, "CONSOL%s%s%s",
		start  ? " start"  : "",
		select ? " select" : "",
		option ? " option" : "");
	(void)n;
	return atb_simple_cmd(c, cmd);
}

static int atb_cmd_with_path(atb_client_t* c, const char* verb, const char* path) {
	if (!path) return ATB_ERR_BAD_ARG;
	size_t cmd_size = strlen(verb) + 2 + strlen(path) + 1;
	char* cmd = (char*)malloc(cmd_size);
	if (!cmd) {
		atb_set_error(c, "out of memory");
		return ATB_ERR_NETWORK;
	}
	snprintf(cmd, cmd_size, "%s %s", verb, path);
	int rc = atb_simple_cmd(c, cmd);
	free(cmd);
	return rc;
}

int atb_boot(atb_client_t* c, const char* path) {
	return atb_cmd_with_path(c, "BOOT", path);
}

int atb_boot_bare(atb_client_t* c, unsigned int settle_frames) {
	/* The server now does the settle synchronously inside
	 * CmdBootBare — it polls for the stub's signature bytes at
	 * $0600 and a parked CPU PC inside the JMP * loop, and only
	 * returns once both are true. The `settle_frames` argument is
	 * kept for backwards-compatibility with older callers but is
	 * ignored on servers that implement the synchronous settle
	 * (every current build). Passing zero is fine. */
	(void)settle_frames;
	return atb_simple_cmd(c, "BOOT_BARE");
}

/* Like atb_cmd_with_path, but emits "VERB key=PATH" — used by Phase 4
 * commands that take their path as a key=value option (SCREENSHOT,
 * RAWSCREEN). */
static int atb_cmd_with_key_path(atb_client_t* c, const char* verb,
                                 const char* key, const char* path) {
	if (!path || !key) return ATB_ERR_BAD_ARG;
	size_t cmd_size = strlen(verb) + 2 + strlen(key) + 1 + strlen(path) + 1;
	char* cmd = (char*)malloc(cmd_size);
	if (!cmd) { atb_set_error(c, "out of memory"); return ATB_ERR_NETWORK; }
	snprintf(cmd, cmd_size, "%s %s=%s", verb, key, path);
	int rc = atb_simple_cmd(c, cmd);
	free(cmd);
	return rc;
}

int atb_mount(atb_client_t* c, unsigned int drive, const char* path) {
	if (!path) return ATB_ERR_BAD_ARG;
	size_t cmd_size = 16 + strlen(path) + 1;
	char* cmd = (char*)malloc(cmd_size);
	if (!cmd) {
		atb_set_error(c, "out of memory");
		return ATB_ERR_NETWORK;
	}
	snprintf(cmd, cmd_size, "MOUNT %u %s", drive, path);
	int rc = atb_simple_cmd(c, cmd);
	free(cmd);
	return rc;
}

int atb_cold_reset(atb_client_t* c) { return atb_simple_cmd(c, "COLD_RESET"); }
int atb_warm_reset(atb_client_t* c) { return atb_simple_cmd(c, "WARM_RESET"); }
int atb_state_save(atb_client_t* c, const char* path) { return atb_cmd_with_path(c, "STATE_SAVE", path); }
int atb_state_load(atb_client_t* c, const char* path) { return atb_cmd_with_path(c, "STATE_LOAD", path); }

/* --------------------------------------------------------------------- */
/* Phase 4 — rendering                                                    */
/* --------------------------------------------------------------------- */

/* Decode the inline base64 "data" field of a SCREENSHOT/RAWSCREEN
 * response into a freshly malloc'd buffer. *out_buf is set to the
 * allocated pointer (caller must free()), *out_len to its length.
 * Width/height are extracted by the caller via atb_extract_uint. */
static int atb_decode_inline_data(atb_client_t* c, unsigned char** out_buf, size_t* out_len) {
	const char* p = strstr(c->response, "\"data\":\"");
	if (!p) {
		atb_set_error(c, "response missing data field");
		return ATB_ERR_PROTOCOL;
	}
	p += 8;
	/* Locate the closing quote so we can size the destination
	 * buffer. base64 expansion is at most ceil(n*3/4); we round up. */
	const char* end = strchr(p, '"');
	if (!end) {
		atb_set_error(c, "response data unterminated");
		return ATB_ERR_PROTOCOL;
	}
	size_t b64_len = (size_t)(end - p);
	size_t cap     = (b64_len / 4 + 1) * 3;
	unsigned char* buf = (unsigned char*)malloc(cap);
	if (!buf) {
		atb_set_error(c, "out of memory");
		return ATB_ERR_NETWORK;
	}
	size_t got = atb_base64_decode(p, buf, cap);
	*out_buf = buf;
	*out_len = got;
	return ATB_OK;
}

int atb_screenshot_inline(atb_client_t* c,
                          unsigned char** out_png, size_t* out_len,
                          unsigned int* out_w, unsigned int* out_h) {
	if (!out_png || !out_len) return ATB_ERR_BAD_ARG;
	int rc = atb_simple_cmd(c, "SCREENSHOT inline=true");
	if (rc != ATB_OK) return rc;
	rc = atb_decode_inline_data(c, out_png, out_len);
	if (rc != ATB_OK) return rc;
	if (out_w) { unsigned long v = 0; if (atb_extract_uint(c->response, "width",  &v)) *out_w = (unsigned)v; }
	if (out_h) { unsigned long v = 0; if (atb_extract_uint(c->response, "height", &v)) *out_h = (unsigned)v; }
	return ATB_OK;
}

int atb_screenshot_path(atb_client_t* c, const char* path) {
	return atb_cmd_with_key_path(c, "SCREENSHOT", "path", path);
}

int atb_rawscreen_inline(atb_client_t* c,
                         unsigned char** out_pixels, size_t* out_len,
                         unsigned int* out_w, unsigned int* out_h) {
	if (!out_pixels || !out_len) return ATB_ERR_BAD_ARG;
	int rc = atb_simple_cmd(c, "RAWSCREEN inline=true");
	if (rc != ATB_OK) return rc;
	rc = atb_decode_inline_data(c, out_pixels, out_len);
	if (rc != ATB_OK) return rc;
	if (out_w) { unsigned long v = 0; if (atb_extract_uint(c->response, "width",  &v)) *out_w = (unsigned)v; }
	if (out_h) { unsigned long v = 0; if (atb_extract_uint(c->response, "height", &v)) *out_h = (unsigned)v; }
	return ATB_OK;
}

/* --------------------------------------------------------------------- */
/* Phase 5a — debugger introspection                                      */
/* --------------------------------------------------------------------- */

int atb_disasm(atb_client_t* c, unsigned int addr, unsigned int count) {
	char cmd[64];
	snprintf(cmd, sizeof cmd, "DISASM $%x %u", addr & 0xFFFF, count ? count : 1);
	return atb_simple_cmd(c, cmd);
}
int atb_history(atb_client_t* c, unsigned int count) {
	char cmd[32];
	snprintf(cmd, sizeof cmd, "HISTORY %u", count ? count : 64);
	return atb_simple_cmd(c, cmd);
}
int atb_callstack(atb_client_t* c, unsigned int count) {
	char cmd[32];
	snprintf(cmd, sizeof cmd, "CALLSTACK %u", count ? count : 64);
	return atb_simple_cmd(c, cmd);
}
int atb_memmap     (atb_client_t* c) { return atb_simple_cmd(c, "MEMMAP"); }
int atb_bank_info  (atb_client_t* c) { return atb_simple_cmd(c, "BANK_INFO"); }
int atb_cart_info  (atb_client_t* c) { return atb_simple_cmd(c, "CART_INFO"); }
int atb_pmg        (atb_client_t* c) { return atb_simple_cmd(c, "PMG"); }
int atb_audio_state(atb_client_t* c) { return atb_simple_cmd(c, "AUDIO_STATE"); }

int atb_eval_expr(atb_client_t* c, const char* expr, long* out_value) {
	if (!expr) return ATB_ERR_BAD_ARG;
	size_t cmd_size = 8 + strlen(expr) + 1;
	char* cmd = (char*)malloc(cmd_size);
	if (!cmd) { atb_set_error(c, "out of memory"); return ATB_ERR_NETWORK; }
	snprintf(cmd, cmd_size, "EVAL %s", expr);
	int rc = atb_simple_cmd(c, cmd);
	free(cmd);
	if (rc != ATB_OK) return rc;
	if (out_value) {
		unsigned long v = 0;
		if (atb_extract_uint(c->response, "value", &v))
			*out_value = (long)v;
	}
	return ATB_OK;
}

/* --------------------------------------------------------------------- */
/* Phase 5b — breakpoints, symbols, memsearch, profiler, verifier         */
/* --------------------------------------------------------------------- */

int atb_bp_set(atb_client_t* c, unsigned int addr,
               const char* condition, unsigned int* out_id) {
	size_t cmd_size = 32 + (condition ? strlen(condition) + 16 : 0);
	char* cmd = (char*)malloc(cmd_size);
	if (!cmd) { atb_set_error(c, "out of memory"); return ATB_ERR_NETWORK; }
	if (condition && *condition)
		snprintf(cmd, cmd_size, "BP_SET $%x condition=%s", addr & 0xFFFF, condition);
	else
		snprintf(cmd, cmd_size, "BP_SET $%x", addr & 0xFFFF);
	int rc = atb_simple_cmd(c, cmd);
	free(cmd);
	if (rc != ATB_OK) return rc;
	if (out_id) {
		unsigned long v = 0;
		if (atb_extract_uint(c->response, "id", &v))
			*out_id = (unsigned int)v;
	}
	return ATB_OK;
}

int atb_bp_clear(atb_client_t* c, unsigned int id) {
	char cmd[32];
	snprintf(cmd, sizeof cmd, "BP_CLEAR %u", id);
	return atb_simple_cmd(c, cmd);
}
int atb_bp_clear_all(atb_client_t* c) { return atb_simple_cmd(c, "BP_CLEAR_ALL"); }
int atb_bp_list     (atb_client_t* c) { return atb_simple_cmd(c, "BP_LIST"); }

int atb_watch_set(atb_client_t* c, unsigned int addr, const char* mode) {
	if (!mode) return ATB_ERR_BAD_ARG;
	char cmd[64];
	snprintf(cmd, sizeof cmd, "WATCH_SET $%x mode=%s", addr & 0xFFFF, mode);
	return atb_simple_cmd(c, cmd);
}

int atb_sym_load(atb_client_t* c, const char* path, unsigned int* out_module_id) {
	int rc = atb_cmd_with_path(c, "SYM_LOAD", path);
	if (rc != ATB_OK) return rc;
	if (out_module_id) {
		unsigned long v = 0;
		if (atb_extract_uint(c->response, "module_id", &v))
			*out_module_id = (unsigned int)v;
	}
	return ATB_OK;
}

int atb_sym_resolve(atb_client_t* c, const char* name, unsigned int* out_addr) {
	if (!name) return ATB_ERR_BAD_ARG;
	size_t cmd_size = 16 + strlen(name) + 1;
	char* cmd = (char*)malloc(cmd_size);
	if (!cmd) { atb_set_error(c, "out of memory"); return ATB_ERR_NETWORK; }
	snprintf(cmd, cmd_size, "SYM_RESOLVE %s", name);
	int rc = atb_simple_cmd(c, cmd);
	free(cmd);
	if (rc != ATB_OK) return rc;
	if (out_addr) {
		unsigned long v = 0;
		if (atb_extract_uint(c->response, "value", &v))
			*out_addr = (unsigned int)v;
	}
	return ATB_OK;
}

int atb_sym_lookup(atb_client_t* c, unsigned int addr, const char* flags) {
	char cmd[64];
	if (flags && *flags)
		snprintf(cmd, sizeof cmd, "SYM_LOOKUP $%x flags=%s", addr & 0xFFFF, flags);
	else
		snprintf(cmd, sizeof cmd, "SYM_LOOKUP $%x", addr & 0xFFFF);
	return atb_simple_cmd(c, cmd);
}

int atb_memsearch(atb_client_t* c, const unsigned char* pattern,
                  size_t pattern_len, unsigned int start, unsigned int end) {
	if (!pattern || pattern_len == 0) return ATB_ERR_BAD_ARG;
	size_t cmd_size = 64 + pattern_len * 2;
	char* cmd = (char*)malloc(cmd_size);
	if (!cmd) { atb_set_error(c, "out of memory"); return ATB_ERR_NETWORK; }
	int n = snprintf(cmd, cmd_size, "MEMSEARCH ");
	for (size_t i = 0; i < pattern_len; ++i) {
		n += snprintf(cmd + n, cmd_size - (size_t)n, "%02x", pattern[i]);
	}
	snprintf(cmd + n, cmd_size - (size_t)n, " start=$%x end=$%x", start, end);
	int rc = atb_simple_cmd(c, cmd);
	free(cmd);
	return rc;
}

int atb_profile_start(atb_client_t* c, const char* mode) {
	const char* m = (mode && *mode) ? mode : "insns";
	char cmd[64];
	snprintf(cmd, sizeof cmd, "PROFILE_START mode=%s", m);
	return atb_simple_cmd(c, cmd);
}
int atb_profile_stop  (atb_client_t* c) { return atb_simple_cmd(c, "PROFILE_STOP"); }
int atb_profile_status(atb_client_t* c) { return atb_simple_cmd(c, "PROFILE_STATUS"); }
int atb_profile_dump(atb_client_t* c, unsigned int top) {
	char cmd[32];
	snprintf(cmd, sizeof cmd, "PROFILE_DUMP top=%u", top ? top : 32);
	return atb_simple_cmd(c, cmd);
}
int atb_profile_dump_tree(atb_client_t* c) { return atb_simple_cmd(c, "PROFILE_DUMP_TREE"); }

int atb_verifier_status(atb_client_t* c) { return atb_simple_cmd(c, "VERIFIER_STATUS"); }
int atb_verifier_set(atb_client_t* c, unsigned int flags) {
	char cmd[48];
	if (flags == 0)
		snprintf(cmd, sizeof cmd, "VERIFIER_SET flags=off");
	else
		snprintf(cmd, sizeof cmd, "VERIFIER_SET flags=0x%x", flags);
	return atb_simple_cmd(c, cmd);
}

const char* atb_last_error(const atb_client_t* c) {
	return c ? c->error : "";
}

const char* atb_last_response(const atb_client_t* c) {
	return (c && c->response) ? c->response : "";
}
