#!/usr/bin/env python3
"""Loopback integration tests of the real server; never uses a public lobby."""
import json
import os
import socket
import struct
import subprocess
import sys
import time
import urllib.error
import urllib.request
import uuid


def port(kind=socket.SOCK_STREAM):
    with socket.socket(socket.AF_INET, kind) as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


def main():
    http_port, udp_port = port(), port(socket.SOCK_DGRAM)
    environment = dict(os.environ, BIND='127.0.0.1', PORT=str(http_port),
                       UDP_REFLECTOR_PORT=str(udp_port), WS_BRIDGE_PORT='0')
    process = subprocess.Popen([sys.argv[1]], env=environment,
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    base = f'http://127.0.0.1:{http_port}'
    sockets = []

    def request(path, value=None):
        req = urllib.request.Request(base + path,
                data=json.dumps(value).encode() if value is not None else None,
                headers={'Content-Type': 'application/json'})
        try:
            with urllib.request.urlopen(req, timeout=2) as response:
                return response.status, json.load(response)
        except urllib.error.HTTPError as error:
            return error.code, error.read()

    def udp():
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind(('127.0.0.1', 0))
        sock.settimeout(.25)
        sockets.append(sock)
        return sock

    def absent(sock):
        try:
            sock.recv(1500)
        except socket.timeout:
            return
        raise AssertionError('Unauthorized traffic was forwarded')

    try:
        for _ in range(100):
            try:
                with urllib.request.urlopen(base + '/healthz', timeout=.1):
                    break
            except (OSError, urllib.error.URLError):
                time.sleep(.03)
        else:
            raise AssertionError('Server did not start')
        status, capabilities = request('/v1/capabilities')
        assert status == 200 and capabilities['relayPort'] == udp_port
        assert capabilities['relayAuthVersion'] == 1 and capabilities['relayHeaderBytes'] == 56
        body = dict(cartName='Local relay regression', hostHandle='owner',
                    hostEndpoint='127.0.0.1:26000', playerCount=1,
                    maxPlayers=2, protocolVersion=8)
        status, session = request('/v1/session', body)
        assert status == 201 and session['relayPort'] == udp_port
        sid = uuid.UUID(session['sessionId']).bytes
        host_ticket = bytes.fromhex(session['token'])
        nonce_a, nonce_b = uuid.uuid4().bytes, uuid.uuid4().bytes
        def claim(nonce):
            status, ticket = request('/v1/session/' + session['sessionId'] + '/relay-ticket',
                                    {'sessionNonce': nonce.hex()})
            assert status == 201 and ticket['sessionNonce'] == nonce.hex()
            return bytes.fromhex(ticket['ticket'])
        ticket_a, ticket_b = claim(nonce_a), claim(nonce_b)
        status, _ = request('/v1/session/' + session['sessionId'] + '/relay-ticket',
                            {'sessionNonce': nonce_a.hex()})
        assert status == 409
        host, first, second, migrated, attacker = [udp() for _ in range(5)]
        address = ('127.0.0.1', udp_port)
        def send(sock, magic, role, ticket, nonce, payload=b'', session_id=sid):
            sock.sendto(magic + session_id + bytes([role]) + bytes(3) + ticket + nonce + payload, address)
        def packet(nonce, marker):
            return b'ANPU' + nonce + struct.pack('<I', 0) + marker
        send(host, b'ASGR', 0, host_ticket, bytes(16))
        send(first, b'ASGR', 1, ticket_a, nonce_a)
        send(second, b'ASGR', 1, ticket_b, nonce_b)
        time.sleep(.03)
        for target, nonce, marker in [(first, nonce_a, b'one'), (second, nonce_b, b'two')]:
            payload = packet(nonce, marker)
            send(host, b'ASDF', 0, host_ticket, nonce, payload)
            assert target.recv(1500) == payload
        # A new source without the route's private ticket cannot divert it.
        send(attacker, b'ASGR', 1, bytes(16), nonce_a)
        send(attacker, b'ASDF', 1, ticket_b, nonce_a, packet(nonce_a, b'bad'))
        time.sleep(.03)
        payload = packet(nonce_a, b'owner preserved')
        send(host, b'ASDF', 0, host_ticket, nonce_a, payload)
        assert first.recv(1500) == payload
        absent(attacker)
        absent(host)
        # Valid authority permits NAT/source-port rebinding.
        send(migrated, b'ASGR', 1, ticket_a, nonce_a)
        time.sleep(.03)
        payload = packet(nonce_a, b'migrated')
        send(host, b'ASDF', 0, host_ticket, nonce_a, payload)
        assert migrated.recv(1500) == payload
        # Joiner can reach the host and cannot impersonate another nonce.
        payload = b'ANPL' + struct.pack('<HH', 8, 0) + nonce_a + bytes(66)
        send(migrated, b'ASDF', 1, ticket_a, nonce_a, payload)
        assert host.recv(1500) == payload
        send(migrated, b'ASDF', 1, ticket_a, nonce_a, packet(nonce_b, b'wrong nonce'))
        absent(host)
        unknown = uuid.uuid4().bytes
        send(attacker, b'ASGR', 0, host_ticket, bytes(16), session_id=unknown)
        send(attacker, b'ASGR', 1, ticket_a, nonce_a, session_id=unknown)
        send(attacker, b'ASDF', 0, host_ticket, nonce_a, packet(nonce_a, b'unknown'), unknown)
        absent(attacker)
        print('PASS: private role tickets, independent pending routes, takeover rejection, authorized rebinding, nonce binding, arbitrary-session rejection, capability port')
    finally:
        for sock in sockets:
            sock.close()
        process.terminate()
        try:
            process.wait(timeout=8)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


if __name__ == '__main__':
    main()
