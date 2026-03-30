.PHONY: configure build install package package-deb package-apt package-managers \
	publish-homebrew publish-aur publish-apt-repo publish-apt-site web-install web-build \
	smoke-stream release-check docker-build

ICEY_SOURCE_DIR ?= ../icey
BUILD_DIR ?= build-dev
STAGE_DIR ?= .stage
NPM ?= npm --prefix web
PACKAGE_PUBLISH_GIT_NAME ?= 0state OSS
PACKAGE_PUBLISH_GIT_EMAIL ?= oss@0state.com
AUR_SSH_KEY ?= $(HOME)/.ssh/aur_icey
AUR_GIT_SSH_COMMAND ?= ssh -i $(AUR_SSH_KEY) -o IdentitiesOnly=yes -o StrictHostKeyChecking=accept-new

configure:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release -DICEY_SOURCE_DIR=$(ICEY_SOURCE_DIR)

build:
	cmake --build $(BUILD_DIR) -j1 --target icey-server

install:
	cmake --install $(BUILD_DIR) --prefix $(STAGE_DIR) --component apps

package:
	ICEY_SOURCE_DIR=$(ICEY_SOURCE_DIR) BUILD_DIR=$(BUILD_DIR) ./scripts/package-release.sh

package-deb:
	ICEY_SOURCE_DIR=$(ICEY_SOURCE_DIR) BUILD_DIR=$(BUILD_DIR) ./scripts/build-deb.sh

package-apt:
	ICEY_SOURCE_DIR=$(ICEY_SOURCE_DIR) BUILD_DIR=$(BUILD_DIR) ./scripts/build-apt-repo.sh

package-managers:
	ICEY_SOURCE_DIR=$(ICEY_SOURCE_DIR) BUILD_DIR=$(BUILD_DIR) ./scripts/package-manager-check.sh

publish-homebrew:
	./scripts/publish-homebrew.sh

publish-aur:
	@if [ -z "$(AUR_REPO_DIR)" ]; then echo "usage: make publish-aur AUR_REPO_DIR=/path/to/aur-icey-server" >&2; exit 1; fi
	AUR_REPO_DIR="$(AUR_REPO_DIR)" ./scripts/publish-aur.sh
	git -C "$(AUR_REPO_DIR)" config user.name "$(PACKAGE_PUBLISH_GIT_NAME)"
	git -C "$(AUR_REPO_DIR)" config user.email "$(PACKAGE_PUBLISH_GIT_EMAIL)"
	git -C "$(AUR_REPO_DIR)" add PKGBUILD .SRCINFO
	@if git -C "$(AUR_REPO_DIR)" diff --cached --quiet; then exit 0; fi
	@version="$$(tr -d '[:space:]' < VERSION)"; \
	git -C "$(AUR_REPO_DIR)" commit -m "icey-server $$version"; \
	GIT_SSH_COMMAND='$(AUR_GIT_SSH_COMMAND)' git -C "$(AUR_REPO_DIR)" push

publish-apt-repo:
	./scripts/publish-apt-repo.sh

publish-apt-site:
	./scripts/publish-apt-repo.sh

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
