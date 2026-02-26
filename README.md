# MonitorCrebirth

Real-time financial & news monitoring system written in C17. Tracks 1000+ cryptocurrencies, 90+ stock indices, 40+ commodity futures, 1200+ forex pairs, and news from 17 RSS feeds — all from free public APIs.

## Features

- **Backend daemon** (`mc-daemon`) — fetches data from 60+ REST APIs, 3 WebSocket streams, and 17 RSS feeds
- **TUI client** (`mc-tui`) — ncurses terminal interface with 7 tabs, search, detail popups, and browser open
- **HTTP API** — JSON REST API on port 8420 for bots and custom clients
- **Unix socket API** — local IPC at `~/.monitorcrebirth/mc.sock`
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
git clone https://github.com/<user>/MonitorCrebirth.git
cd MonitorCrebirth
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

The daemon exposes a JSON REST API on port **8420** (configurable with `--port`).

### Endpoints

#### GET /api/v1/status

Daemon status and statistics.

```bash
curl http://localhost:8420/api/v1/status
```

```json
{
  "status": "running",
  "version": "0.1.0",
  "uptime_sec": 3600,
  "entries_count": 1247,
  "news_count": 235
}
```

#### GET /api/v1/entries

Financial data entries (crypto, forex, indices, commodities).

| Parameter | Description |
|-----------|-------------|
| `category` | Filter: `crypto`, `crypto_exchange`, `forex`, `stock_index`, `commodity` |
| `symbol` | Filter by symbol (e.g. `BTC`, `GC=F`) |

```bash
# All crypto entries
curl "http://localhost:8420/api/v1/entries?category=crypto"

# Single symbol
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

#### GET /api/v1/news

News items from RSS feeds.

| Parameter | Description |
|-----------|-------------|
| `category` | Filter: `news` |

```bash
curl "http://localhost:8420/api/v1/news"
```

```json
{
  "data": [
    {
      "id": 5678,
      "title": "Bitcoin Surges Past $90K",
      "source": "CoinTelegraph",
      "url": "https://...",
      "summary": "...",
      "category": "news",
      "published_at": 1772127000
    }
  ],
  "count": 235
}
```

#### GET /api/v1/sources

Health status of all configured data sources.

```bash
curl http://localhost:8420/api/v1/sources
```

```json
{
  "sources": [
    {
      "name": "CoinGecko Markets",
      "type": "rest",
      "last_fetched": "2026-02-26T12:00:00Z",
      "seconds_ago": 5,
      "last_error": "",
      "error_count": 0,
      "health": "healthy"
    }
  ],
  "count": 80
}
```

Health values: `healthy`, `degraded`, `failing`.

#### GET /api/v1/entries/{symbol}/history

Price history for a specific symbol.

```bash
curl http://localhost:8420/api/v1/entries/BTC/history
```

```json
{
  "symbol": "BTC",
  "data": [
    {"value": 92150.00, "change_pct": 2.35, "volume": 28500000000, "timestamp": 1772127883},
    {"value": 90100.00, "change_pct": -0.5, "volume": 25000000000, "timestamp": 1772127583}
  ],
  "count": 288
}
```

#### POST /api/v1/refresh

Force immediate re-fetch of all sources.

```bash
curl -X POST http://localhost:8420/api/v1/refresh
```

```json
{"status": "refresh_triggered"}
```

### CORS

All responses include `Access-Control-Allow-Origin: *` — safe to call from browser-based clients.

## Data Sources

| Category | Sources | Count |
|----------|---------|-------|
| Crypto | CoinGecko (4 pages), Binance, DEX pools (GeckoTerminal, DexPaprika, UniSat, MagicEden) | ~1000+ tokens |
| Exchanges | CoinGecko Exchanges | 20 exchanges |
| Forex | ExchangeRate-API, FloatRates, Frankfurter, NBP Poland | 1200+ pairs |
| Indices | Yahoo Finance (US, Americas, Europe, Asia, Oceania, Middle East, Africa, Frontier) | 92 indices |
| Commodities | Yahoo Finance (Metals, Energy, Grains, Softs, Livestock, Industrial, Uranium, Water) | 40+ futures |
| News | CoinTelegraph, CoinDesk, BBC, CNBC, MarketWatch, France 24, The Guardian, etc. | 17 RSS feeds |
| Real-time | Binance WebSocket (BTC, ETH, SOL ticker streams) | 3 streams |

All sources are **free public APIs** — no API keys required.

## Configuration

Config file: `~/.monitorcrebirth/config.toml` (auto-created from `config_example/config.toml` on first run).

```toml
[general]
db_path = "~/.monitorcrebirth/monitorcrebirth.db"
refresh_interval_sec = 300
prune_older_than_sec = 86400

[api]
http_port = 8420
unix_socket_path = "~/.monitorcrebirth/mc.sock"

# Example REST source
[[rest_sources]]
name = "Binance"
url = "https://api.binance.com/api/v3/ticker/24hr"
category = "crypto"
format = "json_array"
field_symbol = "symbol"
field_price = "lastPrice"
field_change = "priceChangePercent"
field_volume = "quoteVolume"

# Example RSS source
[[rss_sources]]
name = "CoinTelegraph"
url = "https://cointelegraph.com/rss"
category = "news"

# Example WebSocket source
[[ws_sources]]
name = "Binance BTC"
url = "stream.binance.com"
port = 9443
path = "/ws/btcusdt@ticker"
category = "crypto"
```

### REST Format Types

| Format | Description |
|--------|-------------|
| `json_array` | Top-level JSON array of objects |
| `json_object` | Object where keys are symbols |
| (flat) | Single flat object (one entry per response) |

### Field Mappings

`field_symbol`, `field_price`, `field_change`, `field_volume`, `field_name` — map JSON keys to data fields. Use `data_path` for nested JSON (e.g. `data_path = "data"`).

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
                    │  │ 1 prune   │  └──────────────┘ │
                    │  └─────┬─────┘                    │
                    │        │                          │
                    │  ┌─────▼─────┐                    │
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

# Get latest news
news = requests.get(f"{API}/news").json()
for n in news["data"][:5]:
    print(f"[{n['source']}] {n['title']}")
```

## License

MIT
