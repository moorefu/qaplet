ifeq ($(OS),Windows_NT)
PYTHON ?= python
else
PYTHON ?= python3
endif
OUT ?= out
WSL ?=

.PHONY: doctor test-foundation test-bootstrap test check-ci
doctor:
	"$(PYTHON)" scripts/foundation.py doctor --cc "$(CC)" $(if $(WSL),--wsl "$(WSL)",) --report "$(OUT)/doctor.json"

test-foundation:
	"$(PYTHON)" scripts/foundation.py test-foundation --cc "$(CC)" --report "$(OUT)/m0.json"

test-bootstrap:
	"$(PYTHON)" scripts/foundation.py test-foundation --profile bootstrap --cc "$(CC)" --report "$(OUT)/bootstrap.json"

test:
	"$(PYTHON)" -m unittest discover -s tests/contracts -v

check-ci:
	"$(PYTHON)" scripts/foundation.py check-ci --report "$(OUT)/check-ci.json"
