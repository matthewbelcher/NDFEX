# NDFEX Common Makefile Settings
# Include this in component Makefiles: include ../common.mk

# Compiler settings
CXX ?= clang++
CXXSTD = -std=c++17

# Common warning flags
WARNINGS = -Wall -Wextra -Werror

# Include paths (relative to component directory)
COMMON_INCLUDES = -I$(PROJECT_ROOT)/3rdparty/spdlog/include \
                  -I$(PROJECT_ROOT) \
                  -I$(PROJECT_ROOT)/3rdparty/SPSCQueue \
                  -I$(PROJECT_ROOT)/order_entry

# Common flags
COMMON_CXXFLAGS = $(CXXSTD) $(WARNINGS) $(COMMON_INCLUDES)
COMMON_LDFLAGS = -pthread

# Build type flags
RELEASE_FLAGS = -O3 -DNDEBUG
DEBUG_FLAGS = -g -O0 -DDEBUG

# Default to release if not specified
BUILD_TYPE ?= release
ifeq ($(BUILD_TYPE),debug)
    CXXFLAGS += $(DEBUG_FLAGS)
else
    CXXFLAGS += $(RELEASE_FLAGS)
endif

# Output directory
OUT_DIR ?= out

# Create output directory
$(OUT_DIR):
	mkdir -p $(OUT_DIR)

# Common pattern rule for object files
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Phony targets
.PHONY: all clean release debug

