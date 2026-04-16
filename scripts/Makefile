PROJ_ROOT := ..

# --- Compiler and Flags ---
CXX      := g++
# Using $(shell ...) is the most robust way to handle pkg-config in Make
CXXFLAGS := -I$(PROJ_ROOT)/src -Wall -Wextra -std=c++17 $(shell pkg-config --cflags libdpdk)
LDFLAGS  := $(shell pkg-config --libs libdpdk) -pthread

# --- Directories ---
SRC_DIR  := $(PROJ_ROOT)/src
OBJ_DIR  := obj
BIN_DIR  := .


# -g: Adds debug symbols
# -rdynamic: Exports symbols to the dynamic table so backtrace can read names
CXXFLAGS += -g -rdynamic
LDFLAGS += -rdynamic

# --- Target ---
TARGET   := $(BIN_DIR)/vtb

# --- File Selection & Exclusions ---
# 1. Grab all files in src
ALL_SRCS := $(wildcard $(SRC_DIR)/*.cpp)

# 2. List the files you want to EXCLUDE here (relative to the src directory)
#EXCLUDE_SRCS := $(SRC_DIR)/vhost_controller.cpp

# 3. Filter them out
SRCS     := $(filter-out $(EXCLUDE_SRCS), $(ALL_SRCS))

# 4. Generate Object list (maps src/*.cpp to obj/*.o)
OBJS     := $(patsubst $(SRC_DIR)/%.cpp, $(OBJ_DIR)/%.o, $(SRCS))

# --- Formatting & Verbosity ---
# Use 'make V=1' to see the full raw compiler commands
ifndef V
    Q := @
else
    Q := 
endif

# Colors (0;32 is a deeper, solid green; 0;34 is a clean blue)
BLUE  := \033[0;34m
GREEN := \033[0;32m
RESET := \033[0m
RED   := \033[0;31m

# --- Rules ---

all: $(TARGET)

# Link the executable
$(TARGET): $(OBJS)
	@mkdir -p $(BIN_DIR)
	@printf "$(RED)Linking:   $(notdir $@)$(RESET)\n"
	$(Q)$(CXX) $(OBJS) -o $(TARGET) $(LDFLAGS)
	@printf "$(GREEN)Build Finished Successfully!$(RESET)\n"

# Compile source files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(OBJ_DIR)
	@printf "$(BLUE)Compiling:$(RESET) $(notdir $<)\n"
	$(Q)$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean up
clean:
	rm -rf $(OBJ_DIR) $(TARGET) *.log

purge:
	rm -rf /tmp/vhost-user.sock
	rm -rf /dev/hugepages/v*

# Run command for DPDK environment
run:
	@./vtb -l 1-2 --file-prefix=vhost_backend --no-pci --no-telemetry \
			--log-level=lib.vhost.config:3 \
			--log-level=lib.vhost:error \
			--log-level=pmd.net.vhost:error \
			--log-level=lib.eal:3 \
			--log-level=lib.ring:8 \
			--log-level=lib.mempool:8 \
			--huge-unlink \
			-- \
			--mode Loopback \
			--verbosity Info \
			--threading-mode EachQ-OneThread \
			--abstract-sockname absockname \
			--port-data-sockname datasockname \
			--port-control-sockname controlsockname \
			--vhost-sockname /tmp/vhost-user.sock

noh:
	@./vtb -l 1-2 --file-prefix=vhost_backend --no-pci --no-telemetry \
			--log-level=lib.vhost.config:3 \
			--log-level=lib.vhost:error \
			--log-level=pmd.net.vhost:error \
			--log-level=lib.eal:3 \
			--log-level=lib.ring:8 \
			--log-level=lib.mempool:8 \
			--no-huge \
			-- \
			--mode Loopback \
			--verbosity Trace \
			--threading-mode AllQ-TwoThread \
			--abstract-sockname absockname \
			--port-data-sockname datasockname \
			--port-control-sockname controlsockname \
			--vhost-sockname /tmp/vhost-user.sock

.PHONY: all clean run

			#--threading-mode EachQ-OneThread \
			#--threading-mode AllQ-TwoThread \
			#--threading-mode AllQ-OneThread \
			#--threading-mode EachQ-TwoThread \
