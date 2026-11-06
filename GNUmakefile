# The pinned M0 recipe includes Makefile. Keep its bytes unchanged.
include Makefile

LOCAL_PROFILE ?= all
LOCAL_SDK_REPORT ?=
LOCAL_SDK_DIR ?=

.PHONY: check-fast test-engine test-loop test-work test-process test-tty test-ci
check-fast:
	"$(PYTHON)" scripts/dev.py check-fast --profile "$(LOCAL_PROFILE)" $(if $(LOCAL_SDK_REPORT),--supplier-report "$(LOCAL_SDK_REPORT)",) $(if $(LOCAL_SDK_DIR),--supplier-dir "$(LOCAL_SDK_DIR)",)

test-engine test-loop test-work test-process test-tty test-ci:
	"$(PYTHON)" scripts/dev.py check-fast --profile "$(patsubst test-%,%,$@)" $(if $(LOCAL_SDK_REPORT),--supplier-report "$(LOCAL_SDK_REPORT)",) $(if $(LOCAL_SDK_DIR),--supplier-dir "$(LOCAL_SDK_DIR)",)
