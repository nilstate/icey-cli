.PHONY: configure build install package web-install web-build smoke-stream release-check docker-build

ICEY_SOURCE_DIR ?= ../icey
BUILD_DIR ?= build-dev
STAGE_DIR ?= .stage
NPM ?= npm --prefix web

configure:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release -DICEY_SOURCE_DIR=$(ICEY_SOURCE_DIR)

build:
	cmake --build $(BUILD_DIR) -j1 --target icey-server

install:
	cmake --install $(BUILD_DIR) --prefix $(STAGE_DIR) --component apps

package:
	ICEY_SOURCE_DIR=$(ICEY_SOURCE_DIR) BUILD_DIR=$(BUILD_DIR) ./scripts/package-release.sh

web-install:
	$(NPM) ci

web-build:
	$(NPM) run build

smoke-stream:
	$(NPM) run test:smoke:chromium

release-check:
	ICEY_SOURCE_DIR=$(ICEY_SOURCE_DIR) BUILD_DIR=$(BUILD_DIR) ./scripts/release-check.sh

docker-build:
	docker compose -f docker/compose.yaml build
