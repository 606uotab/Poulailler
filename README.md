# Poulailler

Real-time financial & news monitoring system written in C17. Tracks 1000+ cryptocurrencies, 90+ stock indices, 40+ commodity futures, 1200+ forex pairs, economic calendar events, and news from 120+ RSS feeds — all from free public APIs.

## Features

- **Backend daemon** (`mc-daemon`) — fetches data from 61 REST APIs, 3 WebSocket streams, and 122 RSS feeds
- **TUI client** (`mc-tui`) — ncurses terminal interface with 7 tabs, search, detail popups, and browser open
- **Web dashboard** — single-file HTML dashboard with 8 tabs (Crypto, Indices, Forex, Commodities, Exchanges, News, Finance, Official)
- **HTTP API** — JSON REST API on port 8420 for bots and custom clients
- **Unix socket API** — local IPC at `~/.monitorcrebirth/mc.sock`
- **Economic calendar** — Forex Factory-style events with impact, forecast, previous, actual
- **Official publications** — Central banks (Fed, ECB, BoE, BoJ...), statistical offices (BLS, Eurostat...), intl orgs (IMF, BIS, WTO)
- **Region/country filtering** — news tagged by continent and ISO country code
- **Parallel fetching** — 8-worker thread pool, first data visible in ~1s
- **SQLite storage** — WAL mode, 24h rolling data with price history

## Quick Start

### Dependencies

```bash
# Debian/Ubuntu
sudo apt install build-essential cmake pkg-config \
  libsqlite3-dev libxml2-dev libcurl4-openssl-dev \
  libmicrohttpd-dev libwebsockets-dev libncursesw5-dev
```

### Build & Run

```bash
git clone https://github.com/606uotab/Poulailler.git
cd Poulailler
mkdir build && cd build
cmake .. && make -j$(nproc)
cd ..
./start.sh both
```

### start.sh Commands

| Command | Description |
|---------|-------------|
| `./start.sh both` | Start daemon in background + launch TUI (default) |
| `./start.sh daemon` | Run daemon in foreground |
| `./start.sh tui` | Launch TUI only (daemon must be running) |
| `./start.sh stop` | Stop background daemon |
| `./start.sh status` | Show daemon status |

Options: `--light` / `--dark` for TUI theme.

## TUI Keybindings

| Key | Action |
|-----|--------|
| `1-7` | Switch tab (Crypto, Exchanges, Forex, Indices, Commodities, News, Custom) |
| `Tab` / `Shift+Tab` | Next / previous tab |
| `j` / `k` | Scroll down / up |
| `n` / `p` | Page down / up |
| `g` / `G` | Jump to top / bottom |
| `/` | Search (filters by symbol, name, source) |
| `Enter` | Open detail popup |
| `o` (in detail) | Open in browser |
| `L` | Toggle light/dark theme |
| `r` | Force refresh |
| `q` | Quit |

## HTTP API

The daemon exposes a JSON REST API on port **8420** (configurable with `--port`). All responses include `Access-Control-Allow-Origin: *` for browser clients.

### Endpoints

| Method | Route | Description | Query params |
|--------|-------|-------------|-------------|
| `GET` | `/api/v1/status` | Daemon status and statistics | — |
| `GET` | `/api/v1/entries` | Market data (crypto, forex, indices, commodities) | `?category=` `?symbol=` |
| `GET` | `/api/v1/news` | All news, calendar events, official publications | `?category=` `?region=` `?country=` |
| `GET` | `/api/v1/sources` | Health status of all sources | — |
| `GET` | `/api/v1/entries/{symbol}/history` | Price history for a symbol | — |
| `POST` | `/api/v1/refresh` | Force immediate re-fetch | — |

### GET /api/v1/status

```bash
curl http://localhost:8420/api/v1/status
```

```json
{
  "status": "running",
  "version": "0.1.0",
  "uptime_sec": 3600,
  "entries_count": 26908,
  "news_count": 2364
}
```

### GET /api/v1/entries

Financial data entries.

| Parameter | Description |
|-----------|-------------|
| `category` | `crypto`, `crypto_exchange`, `forex`, `stock_index`, `commodity` |
| `symbol` | Filter by symbol substring (e.g. `BTC`, `GC=F`) |

```bash
curl "http://localhost:8420/api/v1/entries?category=crypto"
curl "http://localhost:8420/api/v1/entries?symbol=BTC"
```

```json
{
  "data": [
    {
      "id": 12345,
      "source": "CoinGecko Markets",
      "source_type": "rest",
      "category": "crypto",
      "symbol": "BTC",
      "display_name": "Bitcoin",
      "value": 92150.00,
      "currency": "USD",
      "change_pct": 2.35,
      "volume": 28500000000,
      "timestamp": 1772127883,
      "fetched_at": 1772127883
    }
  ],
  "count": 1
}
```

### GET /api/v1/news

News, economic calendar events, and official publications.

| Parameter | Description |
|-----------|-------------|
| `category` | `news`, `financial_news`, `official_pub`, `crypto`, `stock_index`, `forex`, `commodity` |
| `region` | `Europe`, `North America`, `Latin America`, `Asia-Pacific`, `Middle East`, `Africa`, `Oceania`, `Global` |
| `country` | ISO code: `US`, `UK`, `FR`, `DE`, `JP`, `AU`, `EU`, etc. |

```bash
# General news
curl "http://localhost:8420/api/v1/news?category=news"

# Economic calendar (Forex Factory-style)
curl "http://localhost:8420/api/v1/news?category=financial_news"

# Official publications (central banks, stats offices)
curl "http://localhost:8420/api/v1/news?category=official_pub"

# Filter by region/country
curl "http://localhost:8420/api/v1/news?category=official_pub&region=Europe"
curl "http://localhost:8420/api/v1/news?category=financial_news&country=US"
```

**News item:**

```json
{
  "id": 5678,
  "title": "Bitcoin Surges Past $90K",
  "source": "CoinTelegraph",
  "url": "https://...",
  "summary": "...",
  "category": "news",
  "published_at": 1772127000,
  "fetched_at": 1772127883,
  "score": 85.0,
  "region": "Global",
  "country": ""
}
```

**Economic calendar event** (category `financial_news`, url starts with `cal://`):

```json
{
  "title": "Core CPI m/m (USD)",
  "source": "Economic Calendar",
  "url": "cal://2026-03-01T08:30:00-05:00/Core CPI m/m",
  "summary": "Impact: High | Forecast: 0.3% | Previous: 0.5% | Actual: 0.4%",
  "category": "financial_news",
  "published_at": 1772177400,
  "score": 100.0,
  "region": "North America",
  "country": "US"
}
```

The `summary` field for calendar events is structured and parsable: split on `|` then on `:` to extract Impact, Forecast, Previous, Actual.

### GET /api/v1/sources

```bash
curl http://localhost:8420/api/v1/sources
```

```json
{
  "sources": [
    {
      "name": "CoinGecko Markets",
      "type": "rest",
      "last_fetched": 1772127883,
      "seconds_ago": 5,
      "last_error": null,
      "error_count": 0,
      "health": "healthy"
    }
  ],
  "count": 186
}
```

Health values: `healthy` (0 errors), `degraded` (1-2 errors), `failing` (3+ errors).

### GET /api/v1/entries/{symbol}/history

```bash
curl http://localhost:8420/api/v1/entries/BTC/history
```

```json
{
  "symbol": "BTC",
  "data": [
    {"id": 1, "value": 92150.00, "change_pct": 2.35, "volume": 28500000000, "timestamp": 1772127883, "fetched_at": 1772127883}
  ],
  "count": 288
}
```

### POST /api/v1/refresh

```bash
curl -X POST http://localhost:8420/api/v1/refresh
```

```json
{"status": "refresh_triggered"}
```

## News Scoring

News items have a `score` field computed from two factors:

- **Base score** (from source tier): Tier 1 = 100, Tier 2 = 75, Tier 3 = 50
- **Time decay**: score × decay factor based on article age

| Age | Decay |
|-----|-------|
| < 1h | 1.00 |
| 1-3h | 0.85 |
| 3-6h | 0.65 |
| 6-12h | 0.45 |
| 12-24h | 0.25 |
| > 24h | 0.10 |

Calendar events use impact-based scoring: High = 100, Medium = 75, Low = 50, Holiday = 25.

## Data Sources

| Category | Sources | Count |
|----------|---------|-------|
| Crypto | CoinGecko (4 pages), Binance, DEX pools (GeckoTerminal, DexPaprika, UniSat, MagicEden) | ~1000+ tokens |
| Exchanges | CoinGecko Exchanges | 20 exchanges |
| Forex | ExchangeRate-API, FloatRates, Frankfurter, NBP Poland, CBR Russia | 1200+ pairs |
| Indices | Yahoo Finance (US, Americas, Europe, Asia, Oceania, Middle East, Africa, Frontier) | 92 indices |
| Commodities | Yahoo Finance (Metals, Energy, Grains, Softs, Livestock, Industrial, Uranium, Water) | 40+ futures |
| News | Bloomberg, BBC, CNBC, Financial Times, CoinTelegraph, CoinDesk, Le Monde, etc. | 73 RSS feeds |
| Economic Calendar | faireconomy.media (Forex Factory data), Myfxbook | 100+ weekly events |
| Official Publications | Fed, ECB, BoE, BoJ, RBA, SNB, BLS, BEA, Eurostat, IMF, BIS, WTO, etc. | 47 RSS feeds |
| Real-time | Binance WebSocket (BTC, ETH, SOL ticker streams) | 3 streams |

**Total: 122 RSS + 61 REST + 3 WebSocket = 186 sources**, all free public APIs — no API keys required.

## Configuration

Config file: `~/.monitorcrebirth/config.toml` (auto-created from `config_example/config.toml` on first run).

```toml
[general]
db_path = "~/.monitorcrebirth/monitorcrebirth.db"
refresh_interval_sec = 300

[api]
http_port = 8420
unix_socket_path = "~/.monitorcrebirth/mc.sock"

# REST source with field mappings
[[source.rest]]
name = "Binance"
base_url = "https://api.binance.com"
endpoint = "/api/v3/ticker/24hr"
category = "crypto"
response_format = "json_array"
field_symbol = "symbol"
field_price = "lastPrice"
field_change = "priceChangePercent"
field_volume = "quoteVolume"

# RSS source with region/country tagging
[[source.rss]]
name = "Fed - Press Releases"
url = "https://www.federalreserve.gov/feeds/press_all.xml"
category = "official_pub"
tier = 1
region = "North America"
country = "US"
refresh_interval_sec = 900

# Economic calendar (REST → news items)
[[source.rest]]
name = "Economic Calendar"
base_url = "https://nfs.faireconomy.media"
endpoint = "/ff_calendar_thisweek.json"
category = "financial_news"
response_format = "json_array"
refresh_interval_sec = 3600

# WebSocket source
[[source.ws]]
name = "Binance BTC"
url = "wss://stream.binance.com:9443/ws/btcusdt@ticker"
category = "crypto"
```

## Architecture

```
┌─────────────┐     ┌──────────────────────────────────┐
│   mc-tui    │────▶│          mc-daemon                │
│  (ncurses)  │HTTP │                                    │
└─────────────┘     │  ┌───────────┐  ┌──────────────┐ │
                    │  │ Scheduler │  │  HTTP API     │ │
┌─────────────┐     │  │           │  │  :8420        │ │
│  Your Bot   │────▶│  │ 8 REST    │  └──────────────┘ │
│  (any lang) │HTTP │  │ workers   │  ┌──────────────┐ │
└─────────────┘     │  │ 1 RSS     │  │ Unix Socket  │ │
                    │  │ 3 WS      │  │  mc.sock     │ │
┌─────────────┐     │  │ 1 prune   │  └──────────────┘ │
│  Web UI     │────▶│  └─────┬─────┘                    │
│  (browser)  │HTTP │        │                          │
└─────────────┘     │  ┌─────▼─────┐                    │
                    │  │  SQLite   │                    │
                    │  │  (WAL)    │                    │
                    │  └───────────┘                    │
                    └──────────────────────────────────┘
```

## Daemon Options

```
mc-daemon [OPTIONS]
  --config PATH    Config file (default: ~/.monitorcrebirth/config.toml)
  --port PORT      Override HTTP API port
  --no-http        Disable HTTP API
  --no-unix        Disable Unix socket API
  --version        Print version
  --help           Show help
```

## Bot Example (Python)

```python
import requests

API = "http://localhost:8420/api/v1"

# Get Bitcoin price
btc = requests.get(f"{API}/entries", params={"symbol": "BTC"}).json()
print(f"BTC: ${btc['data'][0]['value']:,.2f}")

# Get all commodities
commodities = requests.get(f"{API}/entries", params={"category": "commodity"}).json()
for c in commodities["data"]:
    print(f"{c['symbol']:10s} {c['display_name']:20s} ${c['value']:>12,.2f} {c['change_pct']:+.2f}%")

# Get economic calendar (Forex Factory-style)
cal = requests.get(f"{API}/news", params={"category": "financial_news"}).json()
for ev in cal["data"]:
    if ev["url"].startswith("cal://"):
        parts = dict(p.split(":", 1) for p in ev["summary"].split("|"))
        print(f"[{ev['country']}] {parts.get(' Impact','').strip():8s} {ev['title']:40s} "
              f"F={parts.get(' Forecast','').strip():8s} P={parts.get(' Previous','').strip()}")

# Get official publications from European central banks
pubs = requests.get(f"{API}/news", params={"category": "official_pub", "region": "Europe"}).json()
for p in pubs["data"][:10]:
    print(f"[{p['source']}] {p['title']}")

# Get latest news
news = requests.get(f"{API}/news", params={"category": "news"}).json()
for n in news["data"][:5]:
    print(f"[{n['source']}] {n['title']}")
```

## License

MIT
