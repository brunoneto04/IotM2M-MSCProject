# Compiler
CC = gcc

# Compiler flags
CFLAGS = -Iinclude -I/usr/include/x86_64-linux-gnu
CFLAGS += -DENABLE_HTTP
CFLAGS += -DENABLE_COAP
CFLAGS += -DENABLE_MQTT

# Libraries
LIBS = -L/usr/local/lib -lcoap-3 -lssl -lcrypto -lcurl -lpaho-mqtt3cs -lpaho-mqtt3c -ljson-c -lsqlite3 -lpthread

# Source files
SRCS = main.c \
       handlers/cseBase.c \
       handlers/ae.c \
       handlers/container.c \
       handlers/contentInstance.c \
       handlers/subscription.c \
       handlers/common.c \
       handlers/action.c \
       handlers/mqtt_client.c \
       handlers/mqtt_handler.c \


# Object files
OBJS = $(SRCS:.c=.o)

# Target executable
TARGET = iotm2m

# Default target
all: $(TARGET)

# Link the executable
$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LIBS)

# Compile source files into object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean up build artifacts
clean:
	rm -f $(TARGET) $(OBJS)

# Phony targets
.PHONY: all clean
