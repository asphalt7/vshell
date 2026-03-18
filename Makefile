CC      = gcc
CFLAGS  = -Wall -Wextra -Wno-unused-parameter -g
LDFLAGS = -lreadline

SRCS    = main.c parser.c executor.c builtins.c expand.c
OBJS    = $(SRCS:.c=.o)
TARGET  = vshell

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Header dependencies
main.o:     main.c     vshell.h parser.h executor.h expand.h
parser.o:   parser.c   parser.h vshell.h
executor.o: executor.c executor.h builtins.h expand.h vshell.h
builtins.o: builtins.c builtins.h vshell.h
expand.o:   expand.c   expand.h vshell.h

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
