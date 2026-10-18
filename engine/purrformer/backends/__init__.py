from .base import Backend
from .tone import ToneBackend

_REGISTRY: dict[str, Backend] = {}


def register(backend: Backend) -> None:
    _REGISTRY[backend.name] = backend


def get(name: str) -> Backend | None:
    return _REGISTRY.get(name)


def all_backends() -> list[Backend]:
    return list(_REGISTRY.values())


register(ToneBackend())
