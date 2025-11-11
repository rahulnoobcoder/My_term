setup:
	@echo "🔧 Installing required packages..."
	sudo apt update
	sudo apt install -y build-essential pkg-config libx11-dev libpango1.0-dev
	@echo "✅ All dependencies installed successfully."
# Compiler and flags
CXX = g++
# Base compiler flags
CXXFLAGS = -std=c++17 -Wall -Wextra -pthread -g

# Directories
SRCDIR = src
INCDIR = include
OBJDIR = obj

# Use pkg-config to get flags for libraries
PKG_PACKAGES = x11 pangocairo
PKG_CFLAGS = $(shell pkg-config --cflags $(PKG_PACKAGES))
PKG_LDFLAGS = $(shell pkg-config --libs $(PKG_PACKAGES))

# Add all flags together
CXXFLAGS += -I$(INCDIR) $(PKG_CFLAGS)
LDFLAGS = $(PKG_LDFLAGS) -lpthread

# Find all .cpp files in the source directory
SOURCES = $(wildcard $(SRCDIR)/*.cpp)

# Create a list of corresponding .o files in the obj directory
OBJECTS = $(patsubst $(SRCDIR)/%.cpp, $(OBJDIR)/%.o, $(SOURCES))

# Define the final executable name
TARGET = bat_term

# --- Build Rules ---

# Default rule: build everything
all: $(TARGET)

# Rule to link the final executable
$(TARGET): $(OBJECTS)
	@echo "Linking..."
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Build finished. Executable is at ./$(TARGET)"

# Rule to compile a .cpp file into a .o file
$(OBJDIR)/%.o: $(SRCDIR)/%.cpp
	@echo "Compiling $<..."
	@mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Rule to clean up build files
# Rule to clean up build files
clean:
	@echo "Cleaning up..."
	@rm -rf $(OBJDIR)
	@rm -f $(TARGET)

# include this line if you want to clear history on clean
# 	@rm .bat_term_history       
	@echo "Clean complete."

# Rule to run the program
run: all
	@echo "Starting terminal..."
	./$(TARGET)

# Phony targets are not files
.PHONY: all clean run