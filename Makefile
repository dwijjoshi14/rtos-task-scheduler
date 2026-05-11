CXX     = gcc
CFLAGS  = -std=c11 -Wall -Wextra -O2 -Iinclude -lm -lpthread
SRC     = src/FreeRTOS_sim.c \
          src/sensor_task.c  \
          src/control_task.c \
          src/display_task.c \
          src/main.c
TARGET  = rtos_scheduler

all: $(TARGET)

$(TARGET):
	$(CXX) $(SRC) $(CFLAGS) -o $(TARGET)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all run clean
