# core/ratelimit.py: small in-process sliding-window rate limiter.
#
# Good enough for a single-process prototype. Limits are per client IP as
# seen by the API; behind a proxy every request shares the proxy's IP, so put
# the real limit in the proxy. State is lost on restart and not shared
# between worker processes.

import threading
import time
from collections import defaultdict, deque

from fastapi import HTTPException, Request


class RateLimiter:
    def __init__(self, limit: int, window_sec: int, clock=time.monotonic):
        self.limit, self.window, self.clock = limit, window_sec, clock
        self._hits = defaultdict(deque)
        self._lock = threading.Lock()

    def check(self, key: str) -> None:
        now = self.clock()
        with self._lock:
            hits = self._hits[key]
            while hits and now - hits[0] >= self.window:
                hits.popleft()
            if len(hits) >= self.limit:
                retry = int(self.window - (now - hits[0])) + 1
                raise HTTPException(status_code=429, detail="Too many attempts. Try again later.",
                                    headers={"Retry-After": str(retry)})
            hits.append(now)

    def reset(self) -> None:
        with self._lock:
            self._hits.clear()

    def dependency(self, request: Request) -> None:
        self.check(request.client.host if request.client else "unknown")
