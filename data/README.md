# SENTINEL local test datasets

Curated real-world data for integration and evaluation tests. Each file maps to a pipeline capability in the C++ desktop app.

## Quick start

```powershell
# From sentinel/ — fetch data (if missing) and run all real-data tests
& scripts/run_real_data_tests.ps1
```

Or manually:

```powershell
& data/scripts/fetch_datasets.ps1
cd build
cmake --build . --target test_local_datasets test_real_data_evaluation -j4
ctest -R "test_local_datasets|test_real_data_evaluation" -V
```

Full evaluation report: [docs/REAL_DATA_EVALUATION.md](../docs/REAL_DATA_EVALUATION.md)

## Layout

| Path | Rows | Pipeline coverage |
|------|------|-------------------|
| `crimes/uk_metropolitan_street_2024.csv` | 57,099 | UK Police API format, lat/lon, crime types |
| `crimes/cincinnati_pdi_crimes_sample.csv` | 15,000 | US CSV import (Cincinnati PDI) |
| `crimes/sfpd_incidents_sample.csv` | 15,000 | US CSV import (SFPD) |
| `crimes/london_crimes_2024.csv` | 100 | Crime-type fixture + coords |
| `co_offending/moreno_person_crime.csv` | 1,476 | Person–crime bipartite network |
| `co_offending/chicago_co_offending.csv` | 1,960 | Co-arrest pairs (shared case number) |
| `weather/london_2024_h1.json` | 4,368 hourly | Open-Meteo weather cache |
| `manifest.json` | — | Provenance and row counts |

### Bulk downloads (`bulk/`)

Large full-city exports (~200 MB each) are stored separately and used only to regenerate 15k samples:

```
bulk/crimes/cincinnati_pdi_crimes.csv
bulk/crimes/sfpd_incidents.csv
bulk/co_offending/chicago_arrests.csv
bulk/co_offending/_moreno.tar.bz2
```

The `bulk/` folder is gitignored. Test samples in `crimes/` and `co_offending/` are what tests read directly.

## Fetch / refresh

```powershell
& data/scripts/fetch_datasets.ps1
```

Downloads UK street crimes (6 months, London grid), US 15k samples, Moreno co-offending edges, and H1 2024 London weather JSON. Reuses existing files when present.

## Tests

| Test | What it checks |
|------|----------------|
| `test_local_datasets` | Import each file, co-offending graphs, weather parse, mini pipeline |
| `test_real_data_evaluation` | Quantitative metrics: quality pass rates, KDE hotspots, temporal PAI holdout, series detection |

## Licenses

- UK Police data: [Open Government Licence v3.0](https://www.nationalarchives.gov.uk/doc/open-government-licence/version/3/)
- US city open data: public domain / local government terms
- Moreno Crime: academic / KONECT network repository
- Open-Meteo: [CC BY 4.0](https://open-meteo.com/)
