CC = cc
CFLAGS = -std=c17 -Wall -Wextra -Wpedantic -Werror -Iinclude
DBGFLAGS = -O0 -g3 -fsanitize=address,undefined
RELFLAGS = -O2 -DNDEBUG
LDFLAGS =

HAVE_SQLITE := $(shell printf '#include <sqlite3.h>\n' | $(CC) -x c - -fsyntax-only >/dev/null 2>&1 && echo 1 || echo 0)
HAVE_CURL := $(shell printf '#include <curl/curl.h>\n' | $(CC) -x c - -fsyntax-only >/dev/null 2>&1 && echo 1 || echo 0)

ifeq ($(HAVE_SQLITE),1)
CFLAGS += -DHERMES_WITH_SQLITE
LDFLAGS += -lsqlite3
endif

ifeq ($(HAVE_CURL),1)
CFLAGS += -DHERMES_WITH_CURL
LDFLAGS += -lcurl
endif

SRC = \
	src/main.c \
	src/config.c \
	src/db.c \
	src/openai.c \
	src/email.c \
	src/tool_exec.c

OBJ = $(SRC:src/%.c=build/%.o)
BIN = build/hermesd

TESTSRC = tests/test_config.c src/config.c
TESTBIN = build/test_config

.PHONY: all release run clean lint test fmt-check

all: CFLAGS += $(DBGFLAGS)
all: $(BIN)

release: CFLAGS += $(RELFLAGS)
release: $(BIN)

$(BIN): $(OBJ)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDFLAGS)

build/%.o: src/%.c include/hermes.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

run: all
	./$(BIN)

lint:
	@if command -v clang-tidy >/dev/null 2>&1; then \
		clang-tidy src/*.c -- -Iinclude -std=c17; \
	else \
		printf '%s\n' 'clang-tidy not found, skipping'; \
	fi

fmt-check:
	@if command -v clang-format >/dev/null 2>&1; then \
		clang-format --dry-run --Werror src/*.c include/*.h tests/*.c; \
	else \
		printf '%s\n' 'clang-format not found, skipping'; \
	fi

test: CFLAGS += $(DBGFLAGS)
test: $(TESTBIN)
	@if [ -n "$(TEST)" ]; then ./$(TESTBIN) "$(TEST)"; else ./$(TESTBIN); fi

$(TESTBIN): $(TESTSRC) include/hermes.h
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $(TESTSRC)

clean:
	rm -rf build
