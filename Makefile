ifeq ($(OS),Windows_NT)
PYTHON ?= python
else
PYTHON ?= python3
endif
OUT ?= out
WSL ?=
ENV_BUILD_DIR ?=
ENV_DOWNLOAD_URL ?=
ENV_BACKING_DIR ?=

.PHONY: check doctor test-environment test-foundation test-bootstrap test check-ci
doctor:
	"$(PYTHON)" scripts/foundation.py doctor --cc "$(CC)" $(if $(WSL),--wsl "$(WSL)",) --report "$(OUT)/doctor.json"

test-environment:
	"$(PYTHON)" scripts/foundation.py test-environment --cc "$(CC)" $(if $(ENV_BUILD_DIR),--build-dir "$(ENV_BUILD_DIR)",) $(if $(ENV_BACKING_DIR),--backing-dir "$(ENV_BACKING_DIR)",) $(if $(ENV_DOWNLOAD_URL),--download-url "$(ENV_DOWNLOAD_URL)",) --report "$(OUT)/environment.json"

test-foundation:
	"$(PYTHON)" scripts/foundation.py test-foundation --cc "$(CC)" --report "$(OUT)/m0.json"

test-bootstrap:
	"$(PYTHON)" scripts/foundation.py test-foundation --profile bootstrap --cc "$(CC)" --report "$(OUT)/bootstrap.json"

test:
	"$(PYTHON)" -m unittest discover -s tests/contracts -v

check-ci:
	"$(PYTHON)" scripts/foundation.py check-ci --report "$(OUT)/check-ci.json"

check: check-ci test test-bootstrap
