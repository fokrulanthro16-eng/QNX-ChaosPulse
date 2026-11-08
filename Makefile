# ==============================================================================
# QNX-ChaosPulse: Top-Level Master Orchestration Makefile
# Track: Developer Tooling & Host Tools | BlackBerry QNX Global Hackathon 2026
# ==============================================================================

.PHONY: all host target qnx run clean lint

all: target

target:
	$(MAKE) -C target

host:
	node host/server.js

qnx:
	./scripts/build_qnx.sh all

run:
	node host/server.js

lint:
	node --check host/server.js

clean:
	$(MAKE) -C target clean
	rm -rf build
