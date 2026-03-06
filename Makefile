CC      = clang
CFLAGS  = -std=c11 -Wall -Wextra -O2
LDFLAGS =

# Homebrew paths (macOS)
RAYLIB_PREFIX  = $(shell brew --prefix raylib 2>/dev/null || echo /opt/homebrew/opt/raylib)
CURL_PREFIX    = $(shell brew --prefix curl 2>/dev/null || echo /opt/homebrew/opt/curl)

CFLAGS  += -I$(RAYLIB_PREFIX)/include -I$(CURL_PREFIX)/include
LDFLAGS += -L$(RAYLIB_PREFIX)/lib -lraylib
LDFLAGS += -L$(CURL_PREFIX)/lib -lcurl
LDFLAGS += -framework Cocoa -framework IOKit -framework OpenGL -framework CoreVideo

SRC = src/main.c src/crawler.c
OUT = twilight-boxart

.PHONY: all clean run

all: $(OUT)

$(OUT): $(SRC) src/crawler.h
	$(CC) $(CFLAGS) $(SRC) -o $(OUT) $(LDFLAGS)

run: $(OUT)
	./$(OUT)

clean:
	rm -f $(OUT)
