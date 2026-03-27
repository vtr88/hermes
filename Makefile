PYTHON ?= python3

.PHONY: all run lint test clean

all:
	$(PYTHON) -m compileall hermes tests

run:
	$(PYTHON) -m hermes

lint:
	@if command -v ruff >/dev/null 2>&1; then \
		ruff check hermes tests; \
	else \
		$(PYTHON) -m compileall hermes tests; \
	fi

test:
	$(PYTHON) -m unittest discover -s tests -p 'test_*.py' -v

clean:
	rm -rf .pytest_cache .ruff_cache build dist hermes_mail_agent.egg-info
	find hermes tests -name '__pycache__' -type d -prune -exec rm -rf {} +
