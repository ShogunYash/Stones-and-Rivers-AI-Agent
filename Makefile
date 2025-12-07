# Makefile for building and testing the C++ Python module for the Stones & Rivers game.

# --- Variables ---
# Use python3 as the default interpreter.
PYTHON = python3

# Define the directory for build artifacts.
BUILD_DIR = build

# Automatically find the pybind11 CMake directory using Python.
# The '2>/dev/null' silences errors if pybind11 isn't installed yet.
PYBIND11_CMAKE_DIR := $(shell $(PYTHON) -c 'import pybind11; print(pybind11.get_cmake_dir())' 2>/dev/null)


# --- Targets ---
# Phony targets are actions that don't represent a file.
.PHONY: all build run clean install run2

# The default command when you just type "make".
# It will first run the 'build' target.
all: build

# Configure the project with CMake and compile the C++ module.
build: $(BUILD_DIR)/Makefile
	@echo "--- Building C++ Module ---"
	@$(MAKE) -C $(BUILD_DIR)

# This rule generates the CMake build files. It now passes the pybind11 path to CMake.
$(BUILD_DIR)/Makefile: CMakeLists.txt
	@echo "--- Configuring project with CMake... ---"
	@if [ -z "$(PYBIND11_CMAKE_DIR)" ]; then \
		echo "Error: pybind11 not found or not in PATH."; \
		echo "Please run 'make install' first, ensuring you are in the correct virtual environment."; \
		exit 1; \
	fi
	@echo "Found pybind11 at: $(PYBIND11_CMAKE_DIR)"
	@mkdir -p $(BUILD_DIR)
	@cmake -B $(BUILD_DIR) -S . -Dpybind11_DIR="$(PYBIND11_CMAKE_DIR)"


# Install the required pybind11 Python package.
install:
	@echo "--- Installing Python dependencies (pybind11)... ---"
	@$(PYTHON) -m pip install pybind11

# Clean up the project by removing the build directory.
clean:
	@echo "--- Cleaning up build artifacts ---"
	@rm -rf $(BUILD_DIR)

# Run AI (Random) vs AI (Student C++)
run: build
	@echo "--- Starting AI vs AI game---"
	@$(PYTHON) gameEngine.py --mode aivai --circle  friend_cpp --square student_cpp  --time 4.0 --board-size large 
run2: build
	@echo "--- Starting AI vs AI game ---"
	@$(PYTHON) gameEngine.py --mode aivai --circle friend_cpp --square student_cpp --time 2.0 --board-size small