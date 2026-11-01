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
QUICKJS_CACHE ?=
TOOLCHAIN_CACHE ?=
TOOLCHAIN_ARCHIVE ?=
TOOLCHAIN_ACQUISITION_REPORT ?=
TOOLCHAIN_BACKING_DIR ?=
APE_BUILD_DIR ?=
APE_HELPER_SOURCE_CACHE ?=
APE_OUTPUT_DIR ?= $(OUT)/artifacts
OFFLINE_CACHE_ROOT ?= $(CURDIR)/.cache
OFFLINE_UID ?= 1000
OFFLINE_GID ?= 1000

.PHONY: check doctor test-environment test-foundation test-bootstrap test check-ci quickjs-fetch quickjs-bump quickjs-verify toolchain-fetch toolchain-bump toolchain-verify build-ape test-offline
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
	"$(PYTHON)" scripts/build_ape.py check-ci --report "$(OUT)/check-m0-ci.json"

check: check-ci test test-bootstrap

quickjs-fetch quickjs-bump quickjs-verify:
	"$(PYTHON)" scripts/quickjs_source.py $(patsubst quickjs-%,%,$@) $(if $(QUICKJS_CACHE),--cache "$(QUICKJS_CACHE)",) --report "$(OUT)/quickjs-source.json"

toolchain-fetch toolchain-bump:
	"$(PYTHON)" scripts/nightly_toolchain.py $(patsubst toolchain-%,%,$@) $(if $(TOOLCHAIN_CACHE),--cache-dir "$(TOOLCHAIN_CACHE)",) $(if $(TOOLCHAIN_ARCHIVE),--archive "$(TOOLCHAIN_ARCHIVE)",) $(if $(TOOLCHAIN_ACQUISITION_REPORT),--acquisition-report "$(TOOLCHAIN_ACQUISITION_REPORT)",) $(if $(TOOLCHAIN_BACKING_DIR),--backing-dir "$(TOOLCHAIN_BACKING_DIR)",) --report "$(OUT)/nightly-toolchain.json"

toolchain-verify:
	"$(PYTHON)" scripts/nightly_toolchain.py verify $(if $(TOOLCHAIN_CACHE),--cache-dir "$(TOOLCHAIN_CACHE)",) --report "$(OUT)/nightly-toolchain.json"

build-ape:
	"$(PYTHON)" scripts/build_ape.py build $(if $(QUICKJS_CACHE),--source-cache "$(QUICKJS_CACHE)",) $(if $(TOOLCHAIN_CACHE),--toolchain-cache "$(TOOLCHAIN_CACHE)",) $(if $(APE_HELPER_SOURCE_CACHE),--helper-source-cache "$(APE_HELPER_SOURCE_CACHE)",) $(if $(APE_BUILD_DIR),--build-dir "$(APE_BUILD_DIR)",) --output-dir "$(APE_OUTPUT_DIR)" --report "$(OUT)/build-info.json"

test-offline:
	"$(PYTHON)" scripts/offline_integrity.py run --cache-root "$(OFFLINE_CACHE_ROOT)" --uid "$(OFFLINE_UID)" --gid "$(OFFLINE_GID)" --report "$(OUT)/offline-integrity.json"
