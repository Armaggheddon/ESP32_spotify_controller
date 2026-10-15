#!/usr/bin/env python3
"""Retrieve a Spotify *refresh token* for the ESP32 Spotify controller.

Spotify refresh tokens now expire after 6 months (see
https://developer.spotify.com/documentation/web-api/tutorials/refreshing-tokens),
so this one-off Authorization Code flow has to be repeated periodically. When the
refresh token dies, the token endpoint returns "invalid_grant" and the firmware
can no longer authenticate.

Spotify also no longer accepts plain-HTTP redirect URIs except loopback IP
literals, so we use http://127.0.0.1:<port>/callback and run a tiny local HTTP
server here on the PC. This means the board never needs to be re-flashed just to
(re)authorize: only the resulting token in include/my_credentials.h changes.

Flow:
    1. Read CLIENT_ID / CLIENT_SECRET from include/my_credentials.h.
    2. Open the Spotify authorize URL in the browser.
    3. Capture the ?code=... that Spotify redirects to 127.0.0.1.
    4. Exchange the code for an access token + refresh token.
    5. Verify the refresh token round-trips and the access token can call the API.
    6. Print the refresh token (and optionally patch my_credentials.h).

Usage:
    uv run tools/spotify_auth.py                 # print the new refresh token
    uv run tools/spotify_auth.py --write         # also update my_credentials.h
    uv run tools/spotify_auth.py --help

Requires Python 3.8+ and only the standard library. No third-party packages, so
`uv run` (https://docs.astral.sh/uv/) is all that is needed.

Prerequisites (one-off, in the Spotify Developer Dashboard):
    * App -> Settings -> Redirect URIs must contain the loopback URI used here
      (default http://127.0.0.1:3000/callback). `localhost` and `.local` names
      are NOT accepted by Spotify.
    * While the app is in Development mode, the Spotify account you log in with
      must be listed under "Users and Access".
"""

from __future__ import annotations

import argparse
import base64
import json
import os
import re
import secrets
import socket
import sys
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import webbrowser
from http.server import BaseHTTPRequestHandler, HTTPServer

SPOTIFY_AUTHORIZE_URL = "https://accounts.spotify.com/authorize"
SPOTIFY_TOKEN_URL = "https://accounts.spotify.com/api/token"
SPOTIFY_PLAYER_URL = "https://api.spotify.com/v1/me/player"

DEFAULT_REDIRECT_URI = "http://127.0.0.1:3000/callback"
DEFAULT_SCOPES = "user-read-playback-state user-modify-playback-state user-library-modify"

# Resolve repo root (this file lives in <repo>/tools/).
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_CREDENTIALS_FILE = os.path.join(REPO_ROOT, "include", "my_credentials.h")

HTTP_TIMEOUT_S = 30
CALLBACK_TIMEOUT_S = 300

SUCCESS_PAGE = (
    "<!doctype html><html><head><meta charset='utf-8'><title>Spotify auth</title></head>"
    "<body style='font-family: sans-serif; padding: 2rem;'>"
    "<h2>Authorization received</h2>"
    "<p>You can close this tab and return to the terminal.</p></body></html>"
)
FAILURE_PAGE = (
    "<!doctype html><html><head><meta charset='utf-8'><title>Spotify auth</title></head>"
    "<body style='font-family: sans-serif; padding: 2rem;'>"
    "<h2>Authorization failed</h2><p>{reason}</p></body></html>"
)


class AuthError(RuntimeError):
    """Raised for user-facing authentication failures."""


# --------------------------------------------------------------------------- #
# Credentials
# --------------------------------------------------------------------------- #
def read_define(path: str, name: str) -> str | None:
    """Return the string value of `#define <name> "value"` in `path`, if present."""
    try:
        with open(path, "r", encoding="utf-8") as handle:
            text = handle.read()
    except FileNotFoundError:
        return None

    match = re.search(rf'#define\s+{re.escape(name)}\s+"([^"]*)"', text)
    if not match:
        return None
    value = match.group(1)
    return value or None


def load_credentials(path: str) -> tuple[str | None, str | None]:
    return read_define(path, "SPOTIFY_CLIENT_ID"), read_define(path, "SPOTIFY_CLIENT_SECRET")


def patch_refresh_token(path: str, token: str) -> None:
    """Rewrite SPOTIFY_REFRESH_TOKEN in the credentials header, preserving layout."""
    with open(path, "r", encoding="utf-8") as handle:
        text = handle.read()

    pattern = re.compile(r'(#define\s+SPOTIFY_REFRESH_TOKEN\s+")[^"]*(")')
    if not pattern.search(text):
        raise AuthError(
            f"Could not find a SPOTIFY_REFRESH_TOKEN define in {path}. "
            f"Add one (or paste the token manually)."
        )

    updated = pattern.sub(rf'\g<1>{token}\g<2>', text, count=1)

    # Write atomically so an interrupted run cannot truncate the file.
    tmp_path = path + ".tmp"
    with open(tmp_path, "w", encoding="utf-8") as handle:
        handle.write(updated)
    os.replace(tmp_path, path)


# --------------------------------------------------------------------------- #
# HTTP helpers
# --------------------------------------------------------------------------- #
def post_form(url: str, fields: dict[str, str], headers: dict[str, str] | None = None):
    """POST urlencoded form data. Returns (status, parsed_json_or_text)."""
    body = urllib.parse.urlencode(fields).encode("utf-8")
    request_headers = {"Content-Type": "application/x-www-form-urlencoded"}
    if headers:
        request_headers.update(headers)

    request = urllib.request.Request(url, data=body, headers=request_headers, method="POST")
    return _send(request)


def get_json(url: str, headers: dict[str, str] | None = None):
    request = urllib.request.Request(url, headers=headers or {}, method="GET")
    return _send(request)


def _send(request: urllib.request.Request):
    try:
        with urllib.request.urlopen(request, timeout=HTTP_TIMEOUT_S) as response:
            return response.status, _decode(response.read())
    except urllib.error.HTTPError as error:
        # Spotify returns useful JSON bodies on 4xx; surface them instead of raising.
        return error.code, _decode(error.read())


def _decode(raw: bytes):
    text = raw.decode("utf-8", errors="replace")
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        return text


def basic_auth_header(client_id: str, client_secret: str) -> dict[str, str]:
    raw = f"{client_id}:{client_secret}".encode("utf-8")
    return {"Authorization": "Basic " + base64.b64encode(raw).decode("ascii")}


def describe(payload) -> str:
    if isinstance(payload, dict):
        return payload.get("error_description") or payload.get("error") or json.dumps(payload)
    return str(payload)


# --------------------------------------------------------------------------- #
# Local redirect server
# --------------------------------------------------------------------------- #
class _CallbackHandler(BaseHTTPRequestHandler):
    """Captures ?code= on the redirect path; ignores everything else."""

    expected_path = "/callback"
    expected_state = ""
    result: dict | None = None

    def do_GET(self):  # noqa: N802 (http.server API)
        parsed = urllib.parse.urlparse(self.path)

        if parsed.path != self.expected_path:
            self._respond(404, FAILURE_PAGE.format(reason="Unexpected path."))
            return

        params = urllib.parse.parse_qs(parsed.query)
        error = params.get("error", [None])[0]
        code = params.get("code", [None])[0]
        state = params.get("state", [None])[0]

        # NOTE: assign on the *class*, not the instance. `wait_for_code` polls
        # _CallbackHandler.result; `self.result = ...` would create an instance
        # attribute that shadows it, leaving the poll stuck until timeout.
        if error:
            type(self).result = {"error": error}
            self._respond(200, FAILURE_PAGE.format(reason=f"Spotify returned: {error}"))
            return

        if state != self.expected_state:
            type(self).result = {"error": "state_mismatch"}
            self._respond(200, FAILURE_PAGE.format(reason="State mismatch (possible CSRF)."))
            return

        if not code:
            type(self).result = {"error": "missing_code"}
            self._respond(200, FAILURE_PAGE.format(reason="No authorization code in response."))
            return

        type(self).result = {"code": code}
        self._respond(200, SUCCESS_PAGE)

    def _respond(self, status: int, html: str) -> None:
        body = html.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):  # silence default request logging
        pass


def wait_for_code(host: str, port: int, path: str, expected_state: str,
                  timeout_s: int) -> dict:
    """Serve redirects until the browser hits `path`, then return the parsed result."""
    try:
        server = HTTPServer((host, port), _CallbackHandler)
    except OSError as error:
        raise AuthError(
            f"Cannot listen on {host}:{port} ({error}). "
            f"Is another process using that port? Try --port <other>."
        ) from error

    _CallbackHandler.expected_path = path
    _CallbackHandler.expected_state = expected_state
    _CallbackHandler.result = None

    server.timeout = 1
    deadline = time.monotonic() + timeout_s
    try:
        while _CallbackHandler.result is None and time.monotonic() < deadline:
            server.handle_request()
    finally:
        server.server_close()

    if _CallbackHandler.result is None:
        raise AuthError(
            f"Timed out after {timeout_s}s waiting for the redirect. "
            f"Did the redirect URI in the dashboard exactly match {DEFAULT_REDIRECT_URI}?"
        )
    return _CallbackHandler.result


# --------------------------------------------------------------------------- #
# Spotify flow
# --------------------------------------------------------------------------- #
def build_authorize_url(client_id: str, redirect_uri: str, scopes: str, state: str) -> str:
    query = urllib.parse.urlencode(
        {
            "client_id": client_id,
            "response_type": "code",
            "redirect_uri": redirect_uri,
            "scope": scopes,
            "state": state,
            "show_dialog": "true",
        }
    )
    return f"{SPOTIFY_AUTHORIZE_URL}?{query}"


def exchange_code(client_id: str, client_secret: str, code: str, redirect_uri: str) -> dict:
    status, payload = post_form(
        SPOTIFY_TOKEN_URL,
        {
            "grant_type": "authorization_code",
            "code": code,
            "redirect_uri": redirect_uri,
        },
        headers=basic_auth_header(client_id, client_secret),
    )

    if status != 200 or not isinstance(payload, dict) or "refresh_token" not in payload:
        raise AuthError(
            f"Token exchange failed (HTTP {status}): {describe(payload)}. "
            f"A common cause is that redirect_uri does not match the dashboard exactly."
        )
    return payload


def verify_refresh_token(client_id: str, client_secret: str, refresh_token: str) -> str:
    """Prove the refresh token works; returns a fresh access token."""
    status, payload = post_form(
        SPOTIFY_TOKEN_URL,
        {"grant_type": "refresh_token", "refresh_token": refresh_token},
        headers=basic_auth_header(client_id, client_secret),
    )
    if status != 200 or not isinstance(payload, dict) or "access_token" not in payload:
        raise AuthError(f"Refresh verification failed (HTTP {status}): {describe(payload)}")
    return payload["access_token"]


def verify_access_token(access_token: str) -> int | None:
    """Call the player endpoint. Returns the HTTP status (204 == nothing playing)."""
    status, _ = get_json(SPOTIFY_PLAYER_URL, {"Authorization": f"Bearer {access_token}"})
    return status


# --------------------------------------------------------------------------- #
# CLI
# --------------------------------------------------------------------------- #
def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Retrieve a Spotify refresh token via the Authorization Code flow.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("--credentials-file", default=DEFAULT_CREDENTIALS_FILE,
                        help="Header file to read/write credentials "
                             "(default: include/my_credentials.h).")
    parser.add_argument("--client-id", help="Override the client ID from the header file.")
    parser.add_argument("--client-secret", help="Override the client secret from the header file.")
    parser.add_argument("--redirect-uri", default=DEFAULT_REDIRECT_URI,
                        help=f"Must match the dashboard exactly (default: {DEFAULT_REDIRECT_URI}).")
    parser.add_argument("--port", type=int,
                        help="Local port to listen on (default: taken from --redirect-uri).")
    parser.add_argument("--scope", default=DEFAULT_SCOPES,
                        help=f"Space-separated scopes (default: {DEFAULT_SCOPES}).")
    parser.add_argument("--timeout", type=int, default=CALLBACK_TIMEOUT_S,
                        help=f"Seconds to wait for the browser redirect (default: {CALLBACK_TIMEOUT_S}).")
    parser.add_argument("--write", action="store_true",
                        help="Patch SPOTIFY_REFRESH_TOKEN in the credentials file automatically.")
    parser.add_argument("--no-browser", action="store_true",
                        help="Do not open a browser; print the URL instead.")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)

    client_id = args.client_id or read_define(args.credentials_file, "SPOTIFY_CLIENT_ID")
    client_secret = args.client_secret or read_define(args.credentials_file, "SPOTIFY_CLIENT_SECRET")

    missing = [name for name, value in
               (("CLIENT_ID", client_id), ("CLIENT_SECRET", client_secret)) if not value]
    if missing:
        print(
            f"error: missing {', '.join(missing)}. "
            f"Set them in {args.credentials_file} or pass --client-id/--client-secret.",
            file=sys.stderr,
        )
        return 2

    parsed_redirect = urllib.parse.urlparse(args.redirect_uri)
    if parsed_redirect.hostname not in ("127.0.0.1", "::1"):
        print(
            f"warning: Spotify only accepts HTTP redirects to loopback IPs "
            f"(127.0.0.1 or ::1); '{parsed_redirect.hostname}' may be rejected.",
            file=sys.stderr,
        )

    if args.port and parsed_redirect.port and args.port != parsed_redirect.port:
        print(
            f"error: --port {args.port} conflicts with the port in --redirect-uri "
            f"({parsed_redirect.port}). The listening port and the registered redirect "
            f"URI must match, so pass a matching --redirect-uri (and add that exact URI "
            f"in the dashboard).",
            file=sys.stderr,
        )
        return 2

    host = parsed_redirect.hostname or "127.0.0.1"
    port = args.port or parsed_redirect.port or 80
    path = parsed_redirect.path or "/callback"

    state = secrets.token_urlsafe(16)
    authorize_url = build_authorize_url(client_id, args.redirect_uri, args.scope, state)

    print("Spotify authorization (Authorization Code flow)")
    print(f"  redirect URI : {args.redirect_uri}")
    print(f"  scopes       : {args.scope}")
    print(f"  listening on : {host}:{port}{path}")
    print()

    if args.no_browser:
        print("Open this URL in your browser and approve access:")
        print(f"  {authorize_url}\n")
    else:
        print("Opening your browser to approve access...")
        print(f"  (if it does not open, visit: {authorize_url})\n")
        webbrowser.open(authorize_url)

    # The server must be up before the browser can redirect back to it.
    result_holder: dict = {}

    def serve() -> None:
        try:
            result_holder.update(wait_for_code(host, port, path, state, args.timeout))
        except AuthError as error:
            result_holder["error"] = str(error)

    thread = threading.Thread(target=serve, daemon=True)
    thread.start()
    thread.join(timeout=args.timeout + 5)

    if "code" not in result_holder:
        print(f"\nerror: {result_holder.get('error', 'no authorization code received')}",
              file=sys.stderr)
        return 1

    print("Authorization code received. Exchanging for tokens...")
    try:
        tokens = exchange_code(client_id, client_secret, result_holder["code"], args.redirect_uri)
        refresh_token = tokens["refresh_token"]

        print("Verifying refresh token...")
        access_token = verify_refresh_token(client_id, client_secret, refresh_token)

        print("Verifying access token against /me/player...")
        player_status = verify_access_token(access_token)
        if player_status in (200, 204):
            print(f"  /me/player -> HTTP {player_status} (OK)")
        else:
            print(f"  warning: /me/player -> HTTP {player_status}", file=sys.stderr)

        if args.write:
            patch_refresh_token(args.credentials_file, refresh_token)
            print(f"\nUpdated SPOTIFY_REFRESH_TOKEN in {args.credentials_file}")
            print("Re-flash the board so it picks up the new token.")
        else:
            print("\nNew refresh token (paste into SPOTIFY_REFRESH_TOKEN):\n")
            print(refresh_token)
            print("\nRe-run with --write to patch the credentials file automatically.")
        return 0
    except AuthError as error:
        print(f"\nerror: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\nAborted.", file=sys.stderr)
        sys.exit(130)
