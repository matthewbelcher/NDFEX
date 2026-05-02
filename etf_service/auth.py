"""
HTTP Basic Auth for the ETF service, backed by matching_engine/users.txt.

Each line in users.txt is `<client_id> <name> <password>`. A client authenticates
with their <name> and <password>; the resulting request is bound to that
<client_id> and may only act on its own behalf.
"""

from __future__ import annotations

import hmac
from functools import wraps
from pathlib import Path
from typing import Callable, Dict, Optional, Tuple

from flask import Response, g, jsonify, request


class UserStore:
    """Read-only user table loaded from users.txt at startup."""

    def __init__(self, path: Path):
        self.path = path
        # name -> (client_id, password)
        self._by_name: Dict[str, Tuple[int, str]] = {}
        self._load()

    def _load(self) -> None:
        if not self.path.exists():
            raise FileNotFoundError(f"users file not found: {self.path}")
        for raw in self.path.read_text().splitlines():
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            if len(parts) < 3:
                continue
            try:
                cid = int(parts[0])
            except ValueError:
                continue
            name, password = parts[1], parts[2]
            self._by_name[name] = (cid, password)

    def __len__(self) -> int:
        return len(self._by_name)

    def authenticate(self, name: str, password: str) -> Optional[int]:
        """Return client_id on success, None on failure. Constant-time comparison."""
        record = self._by_name.get(name)
        if record is None:
            # Run a dummy compare so timing doesn't reveal whether the name exists.
            hmac.compare_digest(password, password)
            return None
        cid, expected = record
        if hmac.compare_digest(password, expected):
            return cid
        return None


# Module-level store; set by app.py at startup.
_store: Optional[UserStore] = None


def init(store: UserStore) -> None:
    global _store
    _store = store


def _unauthorized(message: str = "Authentication required") -> Response:
    resp = jsonify({"success": False, "message": message})
    resp.status_code = 401
    resp.headers["WWW-Authenticate"] = 'Basic realm="NDFEX ETF Service"'
    return resp


def require_auth(fn: Callable) -> Callable:
    """Flask decorator: validate Basic Auth, set g.client_id and g.user_name."""
    @wraps(fn)
    def wrapper(*args, **kwargs):
        if _store is None:
            return jsonify({"success": False, "message": "auth not initialized"}), 500
        creds = request.authorization
        if not creds or creds.type != "basic" or not creds.username or creds.password is None:
            return _unauthorized()
        cid = _store.authenticate(creds.username, creds.password)
        if cid is None:
            return _unauthorized("Invalid credentials")
        g.client_id = cid
        g.user_name = creds.username
        return fn(*args, **kwargs)
    return wrapper
