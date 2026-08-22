"""Exception hierarchy for the vyb-traffic utility.

All errors raised by the package derive from :class:`VybTrafficError` so that
the CLI can catch a single base type. Subclasses let callers distinguish
configuration problems, API/transport problems, and storage problems.
"""

from __future__ import annotations


class VybTrafficError(Exception):
    """Base class for all vyb-traffic errors."""


class ConfigError(VybTrafficError):
    """A required setting is missing or invalid."""


class DBError(VybTrafficError):
    """A SQLite storage operation failed."""


class APIError(VybTrafficError):
    """Base class for GitHub API failures."""


class AuthError(APIError):
    """HTTP 401: the token is missing/invalid or lacks the repo scope."""


class ForbiddenError(APIError):
    """HTTP 403 (not rate-limiting): the token or repo forbids the request."""


class RateLimitError(APIError):
    """The GitHub API rate limit was exhausted."""


class TransportError(APIError):
    """A network-level error (DNS, connect, timeout)."""


class MalformedResponseError(APIError):
    """The API returned a response we could not parse as expected JSON."""
