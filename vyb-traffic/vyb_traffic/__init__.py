"""vyb-traffic: archive GitHub repository traffic beyond the 14-day window."""

from __future__ import annotations

__version__ = "1.0.0"

from .errors import (  # noqa: F401
    APIError,
    AuthError,
    ConfigError,
    DBError,
    ForbiddenError,
    MalformedResponseError,
    RateLimitError,
    TransportError,
    VybTrafficError,
)
