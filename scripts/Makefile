PROJ_ROOT = ..

# Compiler and Flags
CXX      := g++
CXXFLAGS := -I$(PROJ_ROOT)/src -Wall -Wextra -std=c++17 `pkg-config --cflags libdpdk`
LDFLAGS  :=  `pkg-config --libs libdpdk` -pthread


# Directories
SRC_DIR  := $(PROJ_ROOT)/src
OBJ_DIR  := obj
BIN_DIR  := .

# Target executable name
TARGET   := $(BIN_DIR)/vtb

# Find all .cpp files in src/ and the main vtb.cpp in root
SRCS     := $(wildcard $(SRC_DIR)/*.cpp)
# Convert .cpp file paths to .o file paths in the obj/ directory
OBJS     := $(patsubst %.cpp, $(OBJ_DIR)/%.o, $(notdir $(SRCS)))

# Default rule
all: $(TARGET)

# Link the executable
$(TARGET): $(OBJS)
	@mkdir -p $(BIN_DIR)
	$(CXX) $(OBJS) -o $(TARGET) $(LDFLAGS)

# Compile source files from src/ into obj/
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Compile the main vtb.cpp (located in the parent root) into obj/
$(OBJ_DIR)/vtb.o: $(SRC_DIR)/vtb.cpp
	@mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean up
clean:
	rm -rf $(OBJ_DIR) $(TARGET) *.log

run:
	@./vtb -l 1-2 --file-prefix=vhost_backend --no-pci --no-telemetry \
		--log-level=lib.vhost.config:3 \
		--log-level=lib.eal:3 \
		-- --help \
		--mode Loopback \
		--mode-threads 1 \
		--abstract_sockname absockname \
		--port_data_sockname datasockname \
		--port_control_sockname controlsockname \
		--vhost_sockname /tmp/vhost-user.sock
	
.PHONY: all clean


		#--no-huge \

		#--log-level=lib.vhost.config:3 \
		#--log-level=lib.eal:3 \


		#--mode Emulator \
		#--mode Back2Back \
		#--mode Loopback \
