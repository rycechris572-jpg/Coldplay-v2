
# COLDPLAY

A standalone desktop companion for Geometry Dash, powered by the original Predictive Input Buffer engine.

## Project Structure

- `desktop/` — Qt 6 desktop application.
- `geode/` — Geometry Dash companion mod and prediction engine.
- `shared/` — Shared communication protocol.

## Architecture

COLDPLAY consists of two separate applications:

1. The desktop application.
2. The Geode companion mod.

They will communicate through Windows IPC.

## Planned Features

- Prediction engine integration.
- Live connection status.
- Desktop logs and diagnostics.
- Prediction settings.
- Dark desktop interface with pink accents.

## Build Requirements

- Windows 10 or newer.
- C++20 compiler.
- Qt 6 Widgets.
- Geode SDK.

## Status

Foundation stage — communication bridge not implemented yet.
