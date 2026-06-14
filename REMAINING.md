# SENTINEL — Remaining Work (Phase 3+)

Phase 1 and Phase 2 are **complete** (June 2026). v1.0 code is shipped. **495/495 tests pass** (excluding optional slow real-data targets). CI/release polish applied (NSIS in workflow, Linux `/usr` prefix fix, code signing in deploy script, tag-driven `SENTINEL_VERSION`). Build/test/package use **project-local paths only**: `build/`, `packaging/pkg/`, `dist/`.

## Blocked on environment (you)

| Item | Action |
|------|--------|
| MSVC native Windows build | **Works** after clean configure — run `packaging/windows/build_msvc.bat` (script now clears stale MinGW cache) |
| Code signing | Provide `SIGN_CERT` + `SIGN_PASSWORD` for release scripts |
| Linux packages locally | Run in WSL: `./packaging/linux/build_wsl.sh` |
| GitHub Releases | Push tag `v1.0.0` to trigger `.github/workflows/release.yml` |

**Note:** Windows CI skips slow real-data eval (Linux enforces EVAL thresholds). Local Windows: `& scripts/run_real_data_tests.ps1`.

## Phase 2 — Investigation depth ✅ Complete (June 2026)

| Initiative | Status | Notes |
|------------|--------|-------|
| Case workspace | **Done** | Filter, events, series merge/split, leads, Rossmo CGT heatmap, export |
| Series refinement | **Done** | Per-crime-type eps + analyst overrides |
| Network visualisation | **Done** | CSV + SQLite DB load, zoom/pan, node tooltips |
| Narrative upgrade | Not started | Optional ONNX encoder (feature flag) |
| Calibration dashboard | **Done** | Reliability diagram on Analytics tab |
| Local read-only API | **Done** | `LocalApiServer` + `test_local_api` |

## Phase 3 — Scale & collaboration (6–12 months)

- Parallel ingest + indexed queries (500k events < 5 min)
- Multi-jurisdiction zone registry + CRS normalisation
- gRPC layer + API auth (REST starter shipped in v1.0)
- Signed immutable provenance export
- GIS tile layer (MapLibre / Qt Location) + GeoJSON boundaries

## Phase 4 — Research & policy (12–18 months)

- Fairness-by-default deployment gate
- Open public benchmark leaderboard
- Academic methods paper + shared eval harness
- Per-deployment model cards for governance review

## Phase 5 — Optional productisation (18–24 months)

- Agency desktop licence + support channel
- Training mode with synthetic scenarios
- Managed multi-tenant service for researchers

---

*For what's shipped today see [docs/ROADMAP.md](docs/ROADMAP.md) and [docs/RELEASE.md](docs/RELEASE.md).*
