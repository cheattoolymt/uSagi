CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -g \
          -Wno-unused-function \
          -Wno-misleading-indentation \
          -Wno-format-truncation
TARGET  = usagi
SRCS    = src/main.c
HEADERS = src/lexer.h src/ast.h src/error.h src/parser.h src/typechecker.h src/codegen.h

all: $(TARGET)

$(TARGET): $(SRCS) $(HEADERS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)

clean:
	rm -f $(TARGET) /tmp/usagi_out.c

.PHONY: all clean
