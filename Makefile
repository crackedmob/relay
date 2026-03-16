# compiler and flags
CC	= gcc
CFLAGS = -Wall -Wextra -g
LIBS	= -lpthread


# final binary name
TARGET	= relay

# all source files
SRCS = main.c cache.c error.c request.c thread.c proxy_parse.c


# object files - same names but .o instead of .c
OBJS = $(SRCS:.c=.o)

# default target - builds everything
all: $(TARGET)

# link all object files into the final binary
$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LIBS)


# compile each .c file into a .o file
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# clean up coompiled files
clean:
	rm -f $(OBJS) $(TARGET)

# rebuild from scratch
rebuild: clean all



# $< means the source file
# $@ means the output file