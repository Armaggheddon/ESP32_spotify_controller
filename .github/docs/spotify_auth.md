# Spotify authorization

How the firmware authenticates with Spotify, and how to get yourself a refresh token
when the current one dies.

## Why this is not a one-off step

Spotify hands out two kinds of token, and they have very different lifespans.

The **access token** is what actually signs the API calls. It lasts an hour, and the
firmware renews it on its own, so you never think about it.

The **refresh token** is what buys you new access tokens. It lasts **6 months**, and
using it does not extend that. When it finally lapses, the token endpoint answers
`invalid_grant` and the firmware can no longer authenticate. So this is maintenance
you will come back to, roughly twice a year, not a setup step you do once.

The second wrinkle is redirect URIs. Spotify no longer accepts plain HTTP redirects
except for loopback IP literals: `http://127.0.0.1:PORT` or `http://[::1]:PORT`.
`localhost` is rejected, and so are mDNS `.local` names.

That last rule is what shapes the whole design. The authorization flow has to run on
your PC, because only your PC is reachable at `127.0.0.1`. Re-authorizing therefore
never needs a re-flash until the new token is ready to be written.

For the record: the old on-device flow, which served the callback from
`http://esp32_spotify.local:3000/callback` over mDNS, is not usable under the current
rules. It has been removed from the repository.

## Prerequisites

- [uv](https://docs.astral.sh/uv/), or any Python 3.8 or newer. The helper uses
  only the standard library, so there is nothing to install
- Access to your app in the
  [Spotify Developer Dashboard](https://developer.spotify.com/dashboard)
- Two things configured in that dashboard:
  - Under **Settings**, the **Redirect URIs** list must contain
    `http://127.0.0.1:3000/callback`. It has to match what the helper sends exactly:
    scheme, host, port and path
  - The Spotify account you log in with must appear under **Users and Access**,
    as long as the app is in Development mode

## Usage

```bash
uv run tools/spotify_auth.py            # opens the browser, prints the refresh token
uv run tools/spotify_auth.py --write    # also patches include/my_credentials.h
uv run tools/spotify_auth.py --help
```

Other options worth knowing: `--no-browser` prints the URL instead of opening a
browser, and `--redirect-uri`, `--timeout` and `--credentials-file` are there for the
cases where the defaults do not fit.

Here is what the helper actually does, in order:

1. Reads `SPOTIFY_CLIENT_ID` and `SPOTIFY_CLIENT_SECRET` out of
   `include/my_credentials.h`.
2. Opens the Spotify authorize page, with a random `state` and `show_dialog=true`.
3. Listens on `127.0.0.1:<port>/callback`, checks the `state` it gets back, and
   captures the `code`.
4. Exchanges that code at `/api/token` using Basic auth built from the client id and
   secret.
5. Verifies the result the only way that really counts: it refreshes the token and
   then calls `GET /me/player` with it.
6. Prints the refresh token, or writes it into `include/my_credentials.h` if you
   passed `--write`.

Scopes requested: `user-read-playback-state`, `user-modify-playback-state` and
`user-library-modify`.

## Troubleshooting

- **`invalid_grant` in the firmware logs.** The refresh token expired or was
  revoked. Run the helper with `--write` and flash again.
- **`INVALID_CLIENT`, or `invalid redirect_uri`.** The dashboard URI and the one the
  helper sends must match exactly, trailing path and port included.
- **`403 User not registered in the Developer Dashboard`.** Add the Spotify account
  under **Users and Access**.
- **Port already in use.** Pass a different `--redirect-uri` *and* register that
  exact URI in the dashboard. Passing `--port` on its own exits with an error on
  purpose, because the listening port and the registered redirect URI have to line
  up.
- **The browser says the page cannot be reached.** Harmless. The helper already
  captured the code, so close the tab.
- **`invalid_grant` while the helper is running.** The code was already used, or it
  expired. Run the helper again.

## Notes and decisions

The helper runs on the PC rather than the board so that re-authorizing needs no
re-flash, no mDNS and no port forwarding. It prints the token by default, and only
touches the credentials file when you ask with `--write`, so nothing changes behind
your back. `include/my_credentials.h` is gitignored, which keeps the client secret
and the refresh token out of the repository.
