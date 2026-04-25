# UniFi Doorbell - Build & Deploy Makefile
# ==========================================

# Load .env file if present
-include .env
export

# Configuration
ENV ?= esp32-poe
HOST ?= doorbell.local
USER ?= admin
PASS ?= admin

# Python venv (for PlatformIO - requires Python 3.10-3.13)
VENV_DIR := .venv
PYTHON := $(shell which python3.12 python3.11 python3.10 2>/dev/null | head -1)
# Use venv PIO if available, otherwise fall back to global (for CI)
PIO := $(shell if [ -x $(VENV_DIR)/bin/pio ]; then echo $(VENV_DIR)/bin/pio; else echo pio; fi)

# Colors
GREEN := \033[0;32m
YELLOW := \033[1;33m
CYAN := \033[0;36m
RED := \033[0;31m
NC := \033[0m

# Paths
WEB_DIR := web
DATA_DIR := data
BUILD_DIR := .pio/build/$(ENV)

.PHONY: all build build-web build-firmware build-fs clean deploy deploy-firmware deploy-fs \
        dev monitor help install-deps check-host setup-venv

# Default target
all: build

# ==========================================
# Help
# ==========================================

help:
	@echo ""
	@echo "$(YELLOW)UniFi Doorbell - Build & Deploy$(NC)"
	@echo "=================================="
	@echo ""
	@echo "$(CYAN)Build targets:$(NC)"
	@echo "  make build          - Build everything (web + firmware + filesystem)"
	@echo "  make build-web      - Build web UI only (outputs to data/)"
	@echo "  make build-firmware - Build firmware only"
	@echo "  make build-fs       - Build filesystem image only"
	@echo ""
	@echo "$(CYAN)Deploy targets:$(NC)"
	@echo "  make deploy         - Build and deploy everything via OTA"
	@echo "  make deploy-firmware- Build and deploy firmware only via OTA"
	@echo "  make deploy-fs      - Build and deploy filesystem only via OTA"
	@echo ""
	@echo "$(CYAN)Setup:$(NC)"
	@echo "  make setup-venv     - Create Python venv and install PlatformIO"
	@echo "  make install-deps   - Install npm dependencies for web"
	@echo ""
	@echo "$(CYAN)Development:$(NC)"
	@echo "  make dev            - Start web dev server with hot reload"
	@echo "  make monitor        - Open serial monitor"
	@echo "  make clean          - Clean all build artifacts"
	@echo ""
	@echo "$(CYAN)USB upload (requires physical connection):$(NC)"
	@echo "  make upload         - Upload firmware via USB"
	@echo "  make upload-fs      - Upload filesystem via USB"
	@echo ""
	@echo "$(CYAN)Configuration:$(NC)"
	@echo "  ENV=$(ENV)  (esp32-poe, esp32-s3-zero, esp32-s3-wroom)"
	@echo "  HOST=$(HOST)"
	@echo "  USER=$(USER)"
	@echo "  PASS=$(if $(filter environment override command,$(origin PASS)),***,not yet set)"
	@echo ""
	@echo "$(CYAN)Examples:$(NC)"
	@echo "  make deploy ENV=esp32-s3-zero HOST=192.168.1.50"
	@echo "  make deploy USER=admin PASS=admin"
	@echo "  make build-web && make deploy-fs"
	@echo ""

# ==========================================
# Build targets
# ==========================================

build: build-web build-firmware build-fs
	@echo ""
	@echo "$(GREEN)Build complete!$(NC)"

build-web:
	@echo "$(CYAN)Building web UI...$(NC)"
	@cd $(WEB_DIR) && npm run build
	@echo "$(GREEN)Web UI built -> $(DATA_DIR)/$(NC)"

build-firmware:
	@echo "$(CYAN)Building firmware ($(ENV))...$(NC)"
	@$(PIO) run -e $(ENV)
	@echo "$(GREEN)Firmware built -> $(BUILD_DIR)/firmware.bin$(NC)"

build-fs:
	@echo "$(CYAN)Building filesystem ($(ENV))...$(NC)"
	@$(PIO) run -t buildfs -e $(ENV)
	@echo "$(GREEN)Filesystem built -> $(BUILD_DIR)/littlefs.bin$(NC)"

# ==========================================
# Deploy targets (OTA)
# ==========================================

deploy: build
	@echo ""
	@echo "$(CYAN)Deploying via OTA to $(HOST)...$(NC)"
	@./upload-ota.sh --host $(HOST) --user $(USER) --pass $(PASS) $(ENV)

deploy-firmware: build-firmware
	@echo ""
	@echo "$(CYAN)Deploying firmware via OTA to $(HOST)...$(NC)"
	@./upload-ota.sh --firmware --host $(HOST) --user $(USER) --pass $(PASS) $(ENV)

deploy-fs: build-web build-fs
	@echo ""
	@echo "$(CYAN)Deploying filesystem via OTA to $(HOST)...$(NC)"
	@./upload-ota.sh --filesystem --host $(HOST) --user $(USER) --pass $(PASS) $(ENV)

# Quick deploy without rebuild (use existing build artifacts)
deploy-quick:
	@echo "$(CYAN)Deploying existing build via OTA to $(HOST)...$(NC)"
	@./upload-ota.sh --host $(HOST) --user $(USER) --pass $(PASS) $(ENV)

deploy-firmware-quick:
	@echo "$(CYAN)Deploying existing firmware via OTA to $(HOST)...$(NC)"
	@./upload-ota.sh --firmware --host $(HOST) --user $(USER) --pass $(PASS) $(ENV)

deploy-fs-quick:
	@echo "$(CYAN)Deploying existing filesystem via OTA to $(HOST)...$(NC)"
	@./upload-ota.sh --filesystem --host $(HOST) --user $(USER) --pass $(PASS) $(ENV)

# ==========================================
# USB Upload targets
# ==========================================

upload: build-firmware
	@echo "$(CYAN)Uploading firmware via USB...$(NC)"
	@$(PIO) run -t upload -e $(ENV)

upload-fs: build-web build-fs
	@echo "$(CYAN)Uploading filesystem via USB...$(NC)"
	@$(PIO) run -t uploadfs -e $(ENV)

upload-all: upload upload-fs
	@echo "$(GREEN)Upload complete!$(NC)"

# ==========================================
# Development
# ==========================================

dev:
	@echo "$(CYAN)Starting web dev server...$(NC)"
	@echo "$(YELLOW)Note: API calls will fail without the ESP32 backend$(NC)"
	@cd $(WEB_DIR) && npm run dev

monitor:
	@$(PIO) device monitor -e $(ENV)

# ==========================================
# Utilities
# ==========================================

install-deps:
	@echo "$(CYAN)Installing web dependencies...$(NC)"
	@cd $(WEB_DIR) && npm install
	@echo "$(GREEN)Dependencies installed!$(NC)"

setup-venv:
	@if [ -z "$(PYTHON)" ]; then \
		echo "$(RED)Error: No compatible Python (3.10-3.12) found$(NC)"; \
		echo "Install with: brew install python@3.12"; \
		exit 1; \
	fi
	@echo "$(CYAN)Creating venv with $(PYTHON)...$(NC)"
	@$(PYTHON) -m venv $(VENV_DIR)
	@echo "$(CYAN)Installing PlatformIO...$(NC)"
	@$(VENV_DIR)/bin/pip install --upgrade pip
	@$(VENV_DIR)/bin/pip install platformio
	@echo "$(GREEN)Venv ready! PlatformIO installed.$(NC)"

clean:
	@echo "$(CYAN)Cleaning build artifacts...$(NC)"
	@$(PIO) run -t clean -e $(ENV) 2>/dev/null || true
	@rm -rf $(WEB_DIR)/node_modules/.vite
	@rm -f $(DATA_DIR)/app.js $(DATA_DIR)/style.css $(DATA_DIR)/index.html
	@rm -f $(DATA_DIR)/*.gz
	@echo "$(GREEN)Clean complete!$(NC)"

clean-all: clean
	@rm -rf .pio
	@rm -rf $(WEB_DIR)/node_modules
	@echo "$(GREEN)Full clean complete!$(NC)"

check-host:
	@echo -n "Checking connectivity to $(HOST)... "
	@ping -c 1 -W 2 $(HOST) > /dev/null 2>&1 && \
		echo "$(GREEN)OK$(NC)" || \
		(echo "$(RED)Failed$(NC)" && exit 1)

# ==========================================
# Environment-specific shortcuts
# ==========================================

poe:
	@$(MAKE) build ENV=esp32-poe

s3-zero:
	@$(MAKE) build ENV=esp32-s3-zero

s3-wroom:
	@$(MAKE) build ENV=esp32-s3-wroom

deploy-poe:
	@$(MAKE) deploy ENV=esp32-poe

deploy-s3-zero:
	@$(MAKE) deploy ENV=esp32-s3-zero

deploy-s3-wroom:
	@$(MAKE) deploy ENV=esp32-s3-wroom
